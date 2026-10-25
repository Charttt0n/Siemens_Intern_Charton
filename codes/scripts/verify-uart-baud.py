#!/usr/bin/env python3
"""Fresh Week3/4 production builds: clock model, UART divisor readback."""
import hashlib,json,os,socket,subprocess,tempfile
from pathlib import Path
from datetime import datetime
from week3_session import Session
ROOT=Path(__file__).resolve().parents[1]
os.environ['PATH']='/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:'+os.environ.get('PATH','')
def main():
 out=ROOT/'logs'/datetime.now().strftime('uart-baud-%Y%m%d-%H%M%S');out.mkdir(parents=True)
 result={'status':'FAIL','clock_hz':25000000,'target_baud':115200,'divisor':217,'calculated_baud':25000000/217,'error_percent':(25000000/217/115200-1)*100,'checks':[]}
 def run(args,name):
  p=subprocess.run(args,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
  text=p.stdout.decode(errors='replace');(out/(name+'.txt')).write_text('$ '+' '.join(args)+'\n'+text+f'\nEXIT={p.returncode}\n');assert p.returncode==0,name
  return text
 try:
  run(['qemu-system-arm','--version'],'qemu-version')
  with tempfile.TemporaryDirectory(prefix='baud-build-') as tmp:
   b=Path(tmp)
   run(['cmake','-S','.', '-B',str(b),'-G','Unix Makefiles','-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake'],'configure')
   run(['cmake','--build',str(b),'--target','bootloader-week3.elf','bootloader-week4.elf','--parallel'],'build')
   for stage in [3,4]:
    elf=b/f'bootloader-week{stage}.elf'
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    with Session(elf,b/f'flash{stage}.bin',out/f'week{stage}',port) as vm:
     tree=vm.execute('human-monitor-command',{'command-line':'info qtree'});(out/f'week{stage}-qtree.txt').write_text(tree)
     # Identify UART0 by its MMIO mapping, not CPU clock or another UART.
     blocks=tree.split('dev: cmsdk-apb-uart,')
     uart0=next(x for x in blocks[1:] if '0000000040004000' in x.split('dev:')[0])
     assert 'pclk-frq = 25000000' in uart0.split('dev:')[0]
     script=out/f'week{stage}-registers.gdb'
     script.write_text(f'''set pagination off
set confirm off
target remote 127.0.0.1:{port}
python
import gdb,struct
inf=gdb.selected_inferior()
def word(a): return struct.unpack('<I',bytes(inf.read_memory(a,4)))[0]
d=word(0x40004010); c=word(0x40004008)
print('BAUDDIV[0x40004010] = 0x%08x (%d)' % (d,d))
print('CTRL[0x40004008] = 0x%08x' % c)
assert d == 217
assert c == 0x0b
print('UART_BAUD_READBACK_PASS')
end
detach
quit
''')
     text=run(['arm-none-eabi-gdb','--batch','-q',str(elf),'-x',str(script)],f'week{stage}-registers');assert 'UART_BAUD_READBACK_PASS' in text
     result['checks'].append({'id':f'BAUD-W{stage}','status':'PASS','elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'clock_hz':25000000,'bauddiv':217,'ctrl':11,'phase':'Week3 banner' if stage==3 else 'Week4 rejection idle; blank image (no app UART re-init)','evidence':[f'week{stage}-qtree.txt',f'week{stage}-registers.txt',f'week{stage}/uart.log']})
     print('PASS',f'BAUD-W{stage}',flush=True)
  result['status']='PASS'
 finally:
  (out/'summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');(ROOT/'logs/uart-baud-latest.txt').write_text(str(out)+'\n');print(out)
if __name__=='__main__': main()
