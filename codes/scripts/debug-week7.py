#!/usr/bin/env python3
"""GDB evidence for the trial intent/activation transaction ordering."""
from pathlib import Path
import argparse,json,socket,subprocess,threading,time
from week7_session import Session
from week5_session import sender
from week6_package import flash_seed
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--build',type=Path,default=ROOT/'build/week7');p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);build=a.build.resolve()
 image=out/'seed.bin';image.write_bytes(flash_seed({'a':(build/'application-week7-v1.pkg').read_bytes()}))
 with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 elf=build/'bootloader-week7-debug.elf';result={}
 with Session(elf,image,out/'session',gdb_port=port) as vm:
  vm.execute('stop')
  script=f'''set pagination off
set confirm off
target remote 127.0.0.1:{port}
tbreak update_activate
continue
set $ctx = (update_context*)$r0
printf "W7_CFG_PENDING=%u,%u\\n", *(unsigned int*)0x203f201c, *(unsigned int*)0x203f301c
printf "W7_CFG_STAGE=%u,%u\\n", *(unsigned int*)0x203f2020, *(unsigned int*)0x203f3020
printf "W7_OLD_ACTIVE=%u\\n", $ctx->record.active_slot
backtrace
finish
printf "W7_ACTIVATE_RESULT=%u\\n", $r0
printf "W7_NEW_ACTIVE=%u\\n", $ctx->record.active_slot
print $ctx->record
detach
quit
'''
  path=out/'activation.gdb';path.write_text(script)
  def debug():
   r=subprocess.run(['arm-none-eabi-gdb','-nx','-q','-batch',str(elf),'-x',str(path)],capture_output=True,text=True,timeout=40)
   result['code']=r.returncode;result['text']=r.stdout+r.stderr;(out/'gdb.txt').write_text(result['text'])
  worker=threading.Thread(target=debug);worker.start();time.sleep(.2)
  vm.write(b'u');vm.sending=True
  try: assert sender(vm).send([str(build/'application-week7-v2.pkg')])
  finally:vm.sending=False
  vm.boot(transcript_start=0);worker.join(40);assert not worker.is_alive()
  assert result['code']==0,result
  for marker in ['W7_CFG_PENDING=0,1','W7_CFG_STAGE=0,1','W7_OLD_ACTIVE=1','W7_ACTIVATE_RESULT=0','W7_NEW_ACTIVE=2']:
   assert marker in result['text'],result['text']
  assert b'APP: BOOT CONFIRMED' in vm.transcript
 (out/'summary.json').write_text(json.dumps({'status':'PASS','transport':'QEMU gdbstub (no physical JTAG/OpenOCD adapter)',
   'checks':['trial intent persisted before activation','old active slot A before commit','new active slot B after commit','trial app confirmed']},indent=2)+'\n')
 print('PASS GDB activation ordering',out)
if __name__=='__main__':main()
