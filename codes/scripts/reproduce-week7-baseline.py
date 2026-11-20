#!/usr/bin/env python3
"""Retain pre-fix evidence. Expected bad baseline behavior is NOT acceptance."""
from pathlib import Path
import argparse,json,re,socket,struct,subprocess,threading,time
from week6_session import Session
from week5_session import sender,PeerCancelled
from week6_package import flash_seed
from week5_meta import read_meta
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--build',type=Path,default=ROOT/'build/week7');p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 build=a.build.resolve();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
 r=subprocess.run(['cmake','-S',str(ROOT),'-B',str(build),'-G','Unix Makefiles','-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake'],capture_output=True,text=True);assert r.returncode==0,r.stderr
 r=subprocess.run(['cmake','--build',str(build),'--target','bootloader-week6-review-fault.elf','--parallel'],capture_output=True,text=True)
 (out/'build.txt').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
 base=flash_seed({'a':(build/'application-week7-v1.pkg').read_bytes()})
 image=out/'factory-seed.bin';image.write_bytes(base)
 with Session(build/'bootloader-week6.elf',image,out/'bare-YES') as vm:
  vm.command('YES',expect='FACTORY: DONE')
 raw=bytearray(base);struct.pack_into('<I',raw,len(raw)-4,7);image=out/'gap-seed.bin';image.write_bytes(raw)
 elf=build/'bootloader-week6-review-fault.elf';nm=subprocess.check_output(['arm-none-eabi-nm','-n',str(elf)],text=True)
 address=int(re.search(r'^([0-9a-f]+) \w fault_latched$',nm,re.M)[1],16)
 with Session(elf,image,out/'gap') as vm:
  vm.write(b'u');vm.sending=True;result={}
  def transfer():
   try:result['sent']=sender(vm).send([str(build/'application-week7-v2.pkg')])
   except PeerCancelled:result['cancelled']=True
   except Exception as e:result['error']=repr(e)
  worker=threading.Thread(target=transfer,daemon=True);worker.start();deadline=time.monotonic()+25
  while time.monotonic()<deadline:
   v=vm.execute('human-monitor-command',{'command-line':f'xp /1wx 0x{address:x}'})
   m=re.search(r':\s*0x([0-9a-fA-F]+)',v)
   if m and int(m[1],16)==7:break
   time.sleep(.03)
  else:raise AssertionError(result)
  snapshot=out/'activation-before-trial.bin';vm.fault_snapshot(snapshot)
  vm.cancel_sender=True;vm.serial.shutdown(socket.SHUT_RDWR);worker.join(3);assert not worker.is_alive()
 captured=snapshot.read_bytes();assert read_meta(captured)['active']==2
 # Initial config lives on page 2, and page 3 has never been programmed.
 pending=struct.unpack_from('<I',captured,0xf201c)[0];assert pending==0
 assert captured[0xf3000:0xf3038]==b'\xff'*56
 with Session(build/'bootloader-week6.elf',snapshot,out/'gap-restart') as vm:
  text=vm.boot();assert b'APP: VERSION v2' in text and b'TRIAL: LAUNCH' not in text,text
 source=(ROOT/'src/week6/main.c').read_text();start=source.index('static uint32_t parse_u32(');end=source.index('static void set_param',start)
 probe=out/'parser-probe.c';probe.write_text('#include <stdint.h>\n#include <stdio.h>\n'+source[start:end]+ '\nint main(void) {int ok; uint32_t n=parse_u32("5000000000",&ok);printf("ok=%d value=%u\\n",ok,n);return !(ok && n==705032704);}\n')
 subprocess.run(['cc',str(probe),'-o',str(out/'parser-probe')],check=True)
 r=subprocess.run([str(out/'parser-probe')],capture_output=True,text=True);assert r.returncode==0;(out/'parser.txt').write_text(r.stdout)
 report={'status':'REPRODUCED','purpose':'pre-fix failure evidence, not passing product acceptance',
   'findings':{'factory_without_confirmation':True,'active_slot_B_without_trial_intent':True,'cold_boot_without_trial_watchdog':True,
               'parser_overflow_accepted':705032704,'parser_note':'CLI range checks still reject this value; parser itself accepted overflow'}}
 (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n');print('REPRODUCED baseline defects',out)
if __name__=='__main__':main()
