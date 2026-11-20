#!/usr/bin/env python3
"""Compare original Week 6, fixed -Og and optimized -Os with identical NOR.
CMSDK TIMER0 is configured by GDB at main entry (25 MHz model), then sampled
at try_boot and boot_handoff. Fixed QEMU icount makes this a simulation cost
comparison, not Cortex-M7 cycle-accurate or wall-clock hardware performance.
"""
from pathlib import Path
from datetime import datetime
from zoneinfo import ZoneInfo
import argparse, hashlib, json, re, statistics, struct, subprocess, tempfile, time
from week6_package import flash_seed, build_signed_package
ROOT=Path(__file__).resolve().parents[1]

def measure(elf,seed,out):
    out.mkdir(parents=True,exist_ok=True)
    nm=subprocess.check_output(['arm-none-eabi-nm','-n',str(elf)],text=True)
    trial=re.search(r'^([0-9a-f]+) [tT] try_boot[^\n]*$',nm,re.M)
    hand=re.search(r'^([0-9a-f]+) [tT] boot_handoff$',nm,re.M)
    assert trial and hand
    with tempfile.TemporaryDirectory(prefix='w7bench-',dir='/tmp') as temp:
        socket=Path(temp)/'gdb.sock'
        args=['qemu-system-arm','-M','mps2-an500','-cpu','cortex-m7','-display','none','-monitor','none',
              '-serial',f'file:{out}/uart.log','-S','-icount','shift=3,align=off,sleep=off',
              '-chardev',f'socket,path={socket},server=on,wait=off,id=gdb0','-gdb','chardev:gdb0',
              '-kernel',str(elf),'-device',f'loader,file={seed},addr=0x20300000,force-raw=on']
        script=f'''set pagination off
set confirm off
set remotetimeout 20
target remote {socket}
tbreak main
continue
set {{unsigned int}}0x40000000 = 0
set {{unsigned int}}0x40000008 = 0xffffffff
set {{unsigned int}}0x40000000 = 1
tbreak *0x{trial[1]}
continue
printf "W7_BEFORE_TICKS=%u\\n", 0xffffffff - *(unsigned int*)0x40000004
set $before = *(unsigned int*)0x40000004
tbreak *0x{hand[1]}
continue
printf "W7_TOTAL_TICKS=%u\\n", 0xffffffff - *(unsigned int*)0x40000004
printf "W7_VERIFY_TICKS=%u\\n", $before - *(unsigned int*)0x40000004
info registers pc sp
print boot_image
print boot_status
detach
quit
'''
        (out/'measure.gdb').write_text(script);(out/'qemu-command.json').write_text(json.dumps(args))
        with (out/'qemu.log').open('w') as log:
            proc=subprocess.Popen(args,stdout=log,stderr=subprocess.STDOUT)
            try:
                for _ in range(200):
                    if socket.exists():break
                    if proc.poll() is not None:raise RuntimeError('QEMU failed')
                    time.sleep(.01)
                r=subprocess.run(['arm-none-eabi-gdb','-q','-nx','-batch',str(elf),'-x',str(out/'measure.gdb')],
                                  capture_output=True,text=True,timeout=60)
                text=r.stdout+r.stderr;(out/'gdb.txt').write_text(text)
                if r.returncode:raise RuntimeError(text[-2000:])
                ticks={key:int(re.search('W7_'+key+'_TICKS=(\\d+)',text)[1]) for key in ['BEFORE','TOTAL','VERIFY']}
                assert ticks['TOTAL']>ticks['BEFORE']>0 and ticks['VERIFY']>0,ticks
                return {**ticks,**{key.lower()+'_ms':value/25000 for key,value in ticks.items()}}
            finally:
                proc.terminate()
                try:proc.wait(3)
                except subprocess.TimeoutExpired:proc.kill();proc.wait()

def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,default=ROOT/'build/week7');p.add_argument('--out',type=Path)
    p.add_argument('--runs',type=int,default=3);a=p.parse_args();build=a.build.resolve()
    out=(a.out or ROOT/'logs'/('week7-benchmark-'+datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S'))).resolve();out.mkdir(parents=True,exist_ok=True)
    if a.runs<1:raise ValueError('--runs must be positive')
    result={'status':'RUNNING','runs':a.runs,'model':'QEMU icount shift=3, 8 ns per emulated instruction; TIMER0 25 MHz',
            'window_ms':200,'note':'Simulation only, no cycle-accurate hardware performance claim','variants':{},'measurements':[]}
    try:
        for name,target in [('baseline','bootloader-week6.elf'),('fixed-debug','bootloader-week7-debug.elf'),('optimized','bootloader-week7.elf')]:
            elf=build/target;size=subprocess.check_output(['arm-none-eabi-size',str(elf)],text=True);(out/(name+'-size.txt')).write_text(size)
            row=size.splitlines()[1].split();result['variants'][name]={'elf':str(elf),'sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),
                 'text':int(row[0]),'data':int(row[1]),'bss_including_stack':int(row[2]),'load_bytes':int(row[0])+int(row[1])}
        payload=(build/'application-week7-v1.bin').read_bytes()
        for label,data in [('demo',payload),('128KiB',payload+bytes(128*1024-len(payload)))]:
            raw=bytearray(flash_seed({'a':build_signed_package(data,1)}))
            # Explicit valid cfg record. Both variants receive byte-identical NOR.
            import zlib
            fields=[0x4643364d,1,1,200,1,0xffffffff,0xffffffff,0,0,0,0,0]
            body=struct.pack('<12I',*fields);cfg=body+struct.pack('<II',zlib.crc32(body),0x54494d43)
            raw[0xf2000:0xf2000+56]=cfg
            seed=out/(label+'.bin');seed.write_bytes(raw)
            for variant,v in result['variants'].items():
                samples=[]
                for n in range(a.runs):
                    sample=measure(Path(v['elf']),seed,out/f'{label}-{variant}-{n+1}');samples.append(sample)
                    print('MEASURE',label,variant,n+1,sample,flush=True)
                result['measurements'].append({'image':label,'payload_bytes':len(data),'variant':variant,'seed_sha256':hashlib.sha256(raw).hexdigest(),
                    'samples':samples,'median_verify_ms':statistics.median(x['verify_ms'] for x in samples),
                    'median_total_ms':statistics.median(x['total_ms'] for x in samples)})
        baseline=result['variants']['baseline']['load_bytes'];opt=result['variants']['optimized']['load_bytes']
        result['load_reduction_bytes']=baseline-opt;result['load_reduction_percent']=100*(baseline-opt)/baseline
        assert opt<baseline and opt<=65536
        for label in ['demo','128KiB']:
            rows={m['variant']:m for m in result['measurements'] if m['image']==label}
            assert rows['optimized']['median_verify_ms']<rows['baseline']['median_verify_ms'],rows
        result['status']='PASS'
    finally:
        (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n');(ROOT/'logs/week7-benchmark-latest.txt').write_text(out.name+'\n')
    print('SUMMARY',result['status'],out)
if __name__=='__main__':main()
