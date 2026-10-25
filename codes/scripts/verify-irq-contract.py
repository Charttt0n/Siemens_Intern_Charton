#!/usr/bin/env python3
"""Compile and execute real Cortex-M7 PRIMASK/UART contract checks."""
import os,json,subprocess,tempfile,hashlib
from pathlib import Path
from datetime import datetime
ROOT=Path(__file__).resolve().parents[1]
os.environ['PATH']='/opt/homebrew/bin:'+os.environ.get('PATH','')
def main():
 out=ROOT/'logs'/datetime.now().strftime('irq-contract-%Y%m%d-%H%M%S');out.mkdir(parents=True)
 summary={'status':'FAIL','checks':[]}
 def run(cmd,name):
  r=subprocess.run(cmd,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
  text=r.stdout.decode(errors='replace');(out/(name+'.txt')).write_text('$ '+' '.join(cmd)+'\n'+text+f'\nEXIT={r.returncode}\n');assert r.returncode==0,name;return text
 try:
  run(['qemu-system-arm','--version'],'qemu-version')
  with tempfile.TemporaryDirectory(prefix='irq-contract-') as tmp:
   elf=Path(tmp)/'irq.elf'
   run(['arm-none-eabi-gcc','-mcpu=cortex-m7','-mthumb','-mfloat-abi=soft','-g','-Og','-Wall','-Wextra','-Werror','-ffreestanding','-nostdlib','-Isrc/week3','-Isrc/week2','-Tsrc/week3/linker.ld','src/week3/startup.S','src/week3/uart.c','tests/test_irq.c','-o',str(elf)],'build')
   summary['elf_sha256']=hashlib.sha256(elf.read_bytes()).hexdigest()
   text=run(['qemu-system-arm','-M','mps2-an500','-display','none','-monitor','none','-serial','none','-semihosting-config','enable=on,target=native','-kernel',str(elf)],'qemu')
   assert text.count(' PASS')==2 and text.count(' OK')==22 and 'FAIL' not in text
   for initial in [0,1]:
    summary['checks'].append({'id':f'IRQ-INIT-{initial}','initial':initial,'expected_final':initial,'status':'PASS','assertions':11,'evidence':'qemu.txt'})
   summary['status']='PASS'
 finally:
  (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');(ROOT/'logs/irq-contract-latest.txt').write_text(str(out)+'\n');print(summary['status'],out)
if __name__=='__main__':main()
