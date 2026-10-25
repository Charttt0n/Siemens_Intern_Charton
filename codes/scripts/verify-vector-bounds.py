#!/usr/bin/env python3
"""Full vector-index boundary matrix plus CPU rejection and status paths."""
from pathlib import Path
from datetime import datetime
from zoneinfo import ZoneInfo
import hashlib,json,os,struct,subprocess,tempfile,zlib
from image_format import package
from week3_session import Session
import importlib.util
spec=importlib.util.spec_from_file_location('stage4_verification',Path(__file__).with_name('verify-week4.py'))
stage4=importlib.util.module_from_spec(spec);spec.loader.exec_module(stage4)
ROOT=Path(__file__).resolve().parents[1]
os.environ['PATH']='/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:'+os.environ.get('PATH','')
def main():
 out=ROOT/'logs'/datetime.now(ZoneInfo('Asia/Shanghai')).strftime('vector-bounds-%Y%m%d-%H%M%S');out.mkdir(parents=True)
 report={'status':'FAIL','checks':[]}
 def run(cmd,name):
  p=subprocess.run(cmd,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
  text=p.stdout.decode(errors='replace');(out/(name+'.txt')).write_text('$ '+' '.join(cmd)+'\n'+text+f'\nEXIT={p.returncode}\n');assert p.returncode==0,name;return text
 try:
  with tempfile.TemporaryDirectory(prefix='vector-bounds-') as temp:
   b=Path(temp)
   run(['cmake','-S','.', '-B',str(b),'-G','Unix Makefiles','-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake'],'configure')
   run(['cmake','--build',str(b),'--target','bootloader-week4.elf','application.elf','--parallel'],'build')
   boot=b/'bootloader-week4.elf';app=b/'application.elf'
   run(['arm-none-eabi-objcopy','-O','binary',str(app),str(b/'app.bin')],'objcopy')
   payload=(b/'app.bin').read_bytes();valid=package(payload);seed=b/'valid.bin';seed.write_bytes(valid)
   report['elf_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [boot,app]}
   native=b/'vector-test'
   run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Isrc/week4','-Isrc/week3','src/week4/image.c','src/week4/crc32.c','src/week3/flash.c','tests/test_vector_bounds.c','-o',str(native)],'native-build')
   text=run([str(native),str(seed)],'matrix')
   entries=[line for line in text.splitlines() if line.startswith('VEC-')];assert len(entries)==321 and all(x.endswith('PASS') for x in entries)
   report['matrix_assertions']=len(entries);report['checks'].append({'id':'VEC-MATRIX','status':'PASS','actual':len(entries),'evidence':'matrix.txt'})
   for name,index,value in [('MSP-AT-BASE',0,0x20010000),('RESERVED-13',13,0x100c1),('IRQ31-END',47,(0x10000+len(payload))|1),('RESET-BELOW',1,0x100bf)]:
    data=bytearray(valid);struct.pack_into('<I',data,0x10020+4*index,value);struct.pack_into('<I',data,0x10018,zlib.crc32(data[0x10020:0x10020+len(payload)]));struct.pack_into('<I',data,0x1001c,zlib.crc32(data[0x10000:0x1001c]));path=b/(name+'.bin');path.write_bytes(data)
    with Session(boot,path,out/name) as vm:assert vm.banner==b'BOOT: stage4 image validation\r\nREJECT: 0x00000007\r\n> '
    report['checks'].append({'id':name,'index':index,'value':value,'status':'PASS','actual_code':7,'evidence':name+'/uart.log'})
   stage4.OUT=out;stage4.summary={'checks':[]}
   for name,body,expected in [
    ('LOAD-VECTOR-MSP','set {unsigned int}0x10000 = 0x20010000',7),
    ('LOAD-VECTOR-IRQ31','set {unsigned int}0x100bc = 0',7)]:
    stage4.debug_case(boot,seed,name,"""break boot_verify_loaded
continue
"""+body+"""
python
import gdb,zlib
inf=gdb.selected_inferior();size=int(gdb.parse_and_eval('boot_image.size'))
crc=zlib.crc32(bytes(inf.read_memory(0x10000,size)))
gdb.execute('set var boot_image.crc = %d' % crc)
end
delete breakpoints
break boot_idle
continue
python
assert int(gdb.parse_and_eval('boot_status'))==7
end
""",b'BOOT: stage4 image validation\r\nIMAGE: CRC32 PASS\r\nREJECT: 0x00000007\r\n> ')
    report['checks'].append({'id':name,'status':'PASS','actual_code':expected,'evidence':name+'.txt'})
   stage4.debug_case(boot,seed,'STATUS-IO', """break image_validate
continue
set var dev->memory = 0
delete breakpoints
break boot_idle
continue
python
import gdb
assert int(gdb.parse_and_eval('boot_status'))==1
end
""",b'BOOT: stage4 image validation\r\nREJECT: 0x00000001\r\n> ')
   report['checks'].append({'id':'STATUS-IO','status':'PASS','actual_code':1,'evidence':'STATUS-IO.txt'})
   stage4.debug_case(boot,seed,'STATUS-HARDFAULT', """break main
continue
set {unsigned int}0xe000ed24 = 0
delete breakpoints
break boot_idle
set $pc = 0xfffffffe
continue
python
import gdb
status=int(gdb.parse_and_eval('boot_status'));number=int(gdb.parse_and_eval('exception_number'))
print('boot_status=0x%x exception_number=%d' % (status,number))
assert status==0x100 and number==3
end
""",b'')
   report['checks'].append({'id':'STATUS-HARDFAULT','status':'PASS','actual_code':256,'evidence':'STATUS-HARDFAULT.txt'})
  report['status']='PASS'
 finally:
  (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n');(ROOT/'logs/vector-bounds-latest.txt').write_text(str(out)+'\n');print(report['status'],out)
if __name__=='__main__':main()
