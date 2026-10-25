#!/usr/bin/env python3
"""Stage-4 acceptance: CRC oracle, malformed images, actual CPU handoff."""
from datetime import datetime
from zoneinfo import ZoneInfo
import hashlib
import json
import os
from pathlib import Path
import random
import socket
import struct
import subprocess
import tempfile
import time
import zlib
from image_format import package, APP_OFFSET, HEADER_SIZE, SLOT_SIZE
from week3_session import Session

ROOT=Path(__file__).resolve().parents[1]
os.environ['PATH']='/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:'+os.environ.get('PATH','')
STAMP=datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S-%f')
OUT=ROOT/'logs'/('week4-'+STAMP)
GOOD=(b'BOOT: stage4 image validation\r\nIMAGE: CRC32 PASS\r\n'
      b'LOAD: verified at 0x00010000\r\nJUMP: application\r\n'
      b'APP: CRT PASS\r\nAPP: HANDOFF PASS\r\nAPP: SVC PASS\r\nAPP: RUNNING\r\n> ')
summary={'status':'RUNNING','plan_group':'week3-4','plan_stage':4,'started_at':STAMP,'checks':[]}


def save():
    (OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')


def record(name):
    summary['checks'].append(name); save(); print('PASS:',name,flush=True)


def run(args,name,timeout=120):
    try:
        p=subprocess.run(args,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
    except subprocess.TimeoutExpired as error:
        (OUT/(name+'.txt')).write_bytes((error.stdout or b'')+b'\nTIMEOUT\n')
        raise
    text=p.stdout.decode(errors='replace')
    (OUT/(name+'.txt')).write_text(text)
    if p.returncode: raise RuntimeError(f'{name}: exit {p.returncode}')
    return text


def symbols(elf,name):
    result={}
    for line in run(['arm-none-eabi-nm','-n',str(elf)],name).splitlines():
        fields=line.split()
        if len(fields)==3: result[fields[2]]=int(fields[0],16)
    return result


def debug_case(elf,seed,name,body,expected):
    with socket.socket() as s:
        s.bind(('127.0.0.1',0)); port=s.getsockname()[1]
    serial=OUT/(name+'-uart.log')
    script=OUT/(name+'.gdb')
    script.write_text('set pagination off\nset confirm off\nset remotetimeout 5\n'+
        f'target remote 127.0.0.1:{port}\n'+body+'\npython print("DEBUG_PASS")\ndetach\nquit\n')
    args=['qemu-system-arm','-M','mps2-an500','-cpu','cortex-m7','-display','none',
          '-monitor','none','-serial','file:'+str(serial),'-d','guest_errors','-S',
          '-gdb',f'tcp:127.0.0.1:{port}','-kernel',str(elf),'-device',
          f'loader,file={seed},addr=0x20300000,force-raw=on']
    with (OUT/(name+'-qemu.log')).open('wb') as log:
        p=subprocess.Popen(args,stdout=log,stderr=subprocess.STDOUT)
        try:
            time.sleep(0.2)
            assert 'DEBUG_PASS' in run(['arm-none-eabi-gdb','--batch','-q',str(elf),'-x',str(script)],name,30)
        finally:
            if p.poll() is None:
                p.terminate()
                try: p.wait(timeout=3)
                except subprocess.TimeoutExpired: p.kill(); p.wait(timeout=3)
    assert serial.read_bytes()==expected, serial.read_bytes()
    record(name)


def main():
    OUT.mkdir(parents=True); save()
    try:
        with tempfile.TemporaryDirectory(prefix='stage4-') as temp:
            build=Path(temp)
            for tool in ['qemu-system-arm','arm-none-eabi-gcc','arm-none-eabi-gdb','cmake','cc']:
                run([tool,'--version'],tool+'-version')
            run(['cmake','-S',str(ROOT),'-B',str(build),'-G','Unix Makefiles',
                 '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/arm-none-eabi.cmake')],'configure')
            run(['cmake','--build',str(build),'--target','bootloader-week4.elf','application.elf','--parallel'],'build')
            boot=build/'bootloader-week4.elf'; app=build/'application.elf'
            run(['arm-none-eabi-objcopy','-O','binary',str(app),str(build/'application.bin')],'objcopy')
            payload=(build/'application.bin').read_bytes()
            summary['elf_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (boot,app)}
            summary['payload_bytes']=len(payload)
            run(['arm-none-eabi-size',str(boot),str(app)],'size')
            for elf in (boot,app):
                dis=run(['arm-none-eabi-objdump','-d',str(elf)],elf.stem+'-disassembly')
                assert 'bkpt' not in dis.lower()
            bs=symbols(boot,'boot-symbols'); aps=symbols(app,'app-symbols')
            assert bs['_stack_top']==0x20010000 and aps['_stack_top']==0x20200000
            assert aps['app_vectors']==0x10000
            record('fresh build; distinct boot/app memory; no semihosting')
            cc=['cc','-std=c99','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',
                '-Isrc/week4','-Isrc/week3']
            native=build/'test_crc'
            run(cc+['src/week4/crc32.c','tests/test_crc32.c','-o',str(native)],'crc-native-build')
            rng=random.Random(20261022)
            for size in [0,1,9,255,256,257,4096,SLOT_SIZE-HEADER_SIZE]:
                data=bytes(rng.randrange(256) for _ in range(size))
                file=build/'crc.bin'; file.write_bytes(data)
                value=run([str(native),str(file)],f'crc-{size}').strip()
                assert int(value,16)==zlib.crc32(data)
            record('CRC known vectors and 8 deterministic lengths match independent zlib oracle under sanitizers')
            native_image=build/'test_image'
            run(cc+['src/week4/image.c','src/week4/crc32.c','src/week3/flash.c',
                    'tests/test_image.c','-o',str(native_image)],'image-native-build')
            base=package(payload)
            cases=[('valid',base,0),('blank',b'\xff'*0x100000,2)]
            def header_case(name,index,value,expected,repair=True):
                data=bytearray(base); struct.pack_into('<I',data,APP_OFFSET+index*4,value)
                if repair: struct.pack_into('<I',data,APP_OFFSET+28,zlib.crc32(data[APP_OFFSET:APP_OFFSET+28]))
                cases.append((name,bytes(data),expected))
            header_case('bad-magic',0,0,2)
            header_case('unsupported-format',1,2,2)
            header_case('bad-header-size',2,64,2)
            header_case('bad-header-crc',5,8,3,False)
            header_case('empty-payload',3,0,4)
            header_case('oversize-payload',3,0xffffffff,4)
            header_case('wrong-load-address',4,0x20000000,5)
            corrupt=bytearray(base); corrupt[APP_OFFSET+32+200]^=1
            cases.append(('payload-corruption',bytes(corrupt),6))
            for name,index,value in [('unaligned-msp',0,0x20200004),('boot-ram-msp',0,0x20010000),
                                     ('even-reset',1,0x100c0),('outside-reset',1,0x80001),
                                     ('vector-table-reset',1,0x10001),('bad-irq-vector',16,0),
                                     ('reserved-vector',7,0x100c1), ('msp-above-top',0,0x20200008), ('msp-below-base',0,0x2000fff8), ('reset-at-end',1,0x10000+len(payload)|1), ('irq-before-code',16,0xffff)]:
                data=bytearray(base); struct.pack_into('<I',data,APP_OFFSET+32+index*4,value)
                struct.pack_into('<I',data,APP_OFFSET+24,zlib.crc32(data[APP_OFFSET+32:APP_OFFSET+32+len(payload)]))
                struct.pack_into('<I',data,APP_OFFSET+28,zlib.crc32(data[APP_OFFSET:APP_OFFSET+28]))
                cases.append((name,bytes(data),7))
            cases.append(('maximum-payload',package(payload+b'\xff'*(SLOT_SIZE-HEADER_SIZE-len(payload))),0))
            summary['image_cases'] = []
            for name,data,expected in cases:
                seed=build/(name+'.bin'); seed.write_bytes(data)
                run([str(native_image),str(seed),str(expected)],name+'-native')
                with Session(boot,seed,OUT/name) as vm:
                    wanted=GOOD if expected==0 else f'BOOT: stage4 image validation\r\nREJECT: 0x{expected:08x}\r\n> '.encode()
                    assert vm.banner==wanted, (name,vm.banner)
                summary['image_cases'].append({'name':name,'sha256':hashlib.sha256(data).hexdigest(),'expected_code':expected,'actual_uart':vm.banner.decode('ascii'),'header_hex':data[APP_OFFSET:APP_OFFSET+32].hex(),'vectors_hex':data[APP_OFFSET+32:APP_OFFSET+32+192].hex(),'status':'PASS'})
                record('native and QEMU image '+name)
            seed=build/'valid.bin'
            debug_case(boot,seed,'dirty-state-handoff-and-app-crt',f'''
break *{bs['boot_handoff']}
continue
python
import gdb
# QEMU exposes these registers but this backend ignores GDB writes to PRIMASK.
# Execute the actual CPSID I before injecting pending interrupts.
for _ in range(8):
    if int(gdb.parse_and_eval('$primask')) == 1: break
    gdb.execute('stepi')
assert int(gdb.parse_and_eval('$primask')) == 1
end
set {{unsigned int}}0xe000e100 = 0x80
set {{unsigned int}}0xe000e200 = 0x80
set {{unsigned int}}0xe000e014 = 999999
set {{unsigned int}}0xe000e010 = 7
set {{unsigned int}}0xe000ed04 = 0x14000000
delete breakpoints
break *{aps['App_Reset_Handler']}
continue
python
import gdb, struct
inf=gdb.selected_inferior()
def word(addr): return struct.unpack('<I',bytes(inf.read_memory(addr,4)))[0]
assert int(gdb.parse_and_eval('$sp'))==0x20200000
assert int(gdb.parse_and_eval('$primask'))==1
for reg in ('basepri','faultmask','control'): assert int(gdb.parse_and_eval('$'+reg))==0
assert word(0xe000ed08)==0x10000
assert word(0xe000e100)==0 and word(0xe000e200)==0
assert word(0xe000e010)&7==0
assert word(0xe000ed04)&0x14000000==0
assert word(0x40004008)==0
inf.write_memory({aps['_sdata']},b'\\xcc'*{aps['_edata']-aps['_sdata']})
inf.write_memory({aps['_sbss']},b'\\xa5'*{aps['_ebss']-aps['_sbss']})
inf.write_memory({aps['_ebss']},b'\\x78\\x56\\x34\\x12')
end
delete breakpoints
break *{aps['app_idle']}
continue
python
assert word({aps['app_status']})==1
assert word({aps['app_handoff_ok']})==1
assert word({aps['app_svc_count']})==1
assert word({aps['app_exception']})==0
assert bytes(inf.read_memory({aps['app_data']},12))==struct.pack('<3I',0x12345678,0xa55aa55a,0x11223344)
assert bytes(inf.read_memory({aps['app_bss']},20))==bytes(20)
assert word({aps['_ebss']})==0x12345678
print('APP MSP VTOR masks NVIC SysTick CRT sentinel SVC: PASS')
end
''',GOOD)
            debug_case(boot,seed,'reject-corrupted-loaded-copy',f'''
break *{bs['boot_verify_loaded']}
continue
python
import gdb
inf=gdb.selected_inferior()
byte=bytes(inf.read_memory(0x100c8,1))[0]
inf.write_memory(0x100c8,bytes([byte^1]))
end
delete breakpoints
break *{bs['boot_idle']}
continue
python
import gdb
assert int(gdb.parse_and_eval('boot_status'))==8
end
''',b'BOOT: stage4 image validation\r\nIMAGE: CRC32 PASS\r\nREJECT: 0x00000008\r\n> ')
            for path in list(OUT.glob('*/qemu.log'))+list(OUT.glob('*-qemu.log')):
                unexpected=[line for line in path.read_text().splitlines() if line and 'terminating on signal' not in line]
                assert not unexpected,(path,unexpected)
            record('no QEMU guest errors in all boot and rejection cases')
        summary['status']='PASS'
    except Exception as error:
        summary['status']='FAIL'; summary['error']=repr(error); raise
    finally:
        save(); (ROOT/'logs/week4-latest.txt').write_text(str(OUT)+'\n')
        print('Evidence:',OUT,flush=True)


if __name__=='__main__': main()
