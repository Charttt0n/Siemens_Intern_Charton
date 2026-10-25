#!/usr/bin/env python3
"""Cortex-M7 TX_FULL injection: exact poll budget and real caller failure paths."""
import hashlib,json,os,shutil,signal,socket,subprocess,tempfile,time
from datetime import datetime
from zoneinfo import ZoneInfo
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
os.environ['PATH']='/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:'+os.environ.get('PATH','')
def main():
 out=ROOT/'logs'/datetime.now(ZoneInfo('Asia/Shanghai')).strftime('tx-full-%Y%m%d-%H%M%S');out.mkdir(parents=True)
 report={'status':'FAIL','checks':[]}
 def run(cmd,name,cwd=ROOT,limit=120):
  p=subprocess.run(cmd,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=limit)
  text=p.stdout.decode(errors='replace');(out/(name+'.txt')).write_text('$ '+' '.join(cmd)+'\n'+text+f'\nEXIT={p.returncode}\n');assert p.returncode==0,name;return text
 def record(ident,expected,actual,evidence):
  report['checks'].append(dict(id=ident,expected=expected,actual=actual,evidence=evidence,status='PASS'));print('PASS',ident,flush=True)
 try:
  run(['qemu-system-arm','--version'],'qemu-version')
  with tempfile.TemporaryDirectory(prefix='tx-full-') as tmp:
   temp=Path(tmp);work=temp/'codes';shutil.copytree(ROOT,work,ignore=shutil.ignore_patterns('build','logs','__pycache__','.DS_Store'))
   for driver in ['week2','week3']:
    elf=temp/f'{driver}-contract.elf'
    cmd=['arm-none-eabi-gcc','-mcpu=cortex-m7','-mthumb','-mfloat-abi=soft','-g','-Og','-Wall','-Wextra','-Werror','-ffreestanding','-nostdlib','-DUART_TX_FAULT_TEST',f'-Isrc/{driver}','-Isrc/week2','-Itests','-Tsrc/week3/linker.ld','src/week3/startup.S',f'src/{driver}/uart.c','tests/tx_fault_state.c','tests/test_uart_tx_full.c','-o',str(elf)]
    if driver=='week2':
     # Week3 startup has UART0_RX_Handler in its table; use preserved Week2 startup instead.
     cmd[cmd.index('src/week3/startup.S')]='src/week2/startup.S'
    run(cmd,driver+'-driver-build',work)
    text=run(['qemu-system-arm','-M','mps2-an500','-display','none','-monitor','none','-serial','none','-semihosting-config','enable=on,target=native','-kernel',str(elf)],driver+'-driver',work,10)
    assert 'TX_FULL_CONTRACT_PASS' in text and text.count(' OK')==10 and ' FAIL' not in text
    record('TX-'+driver.upper()+'-DRIVER','flush/putc/write=-1; each 1000000 reads; DATA unchanged; recovery succeeds','10 assertions OK; QEMU exit=0',[driver+'-driver.txt'])
   with (work/'CMakeLists.txt').open('a') as f:
    f.write('''
add_week3_target(tx-week3.elf tests/tx_fault_state.c)
target_compile_definitions(tx-week3.elf PRIVATE UART_TX_FAULT_TEST)
target_include_directories(tx-week3.elf PRIVATE tests)
get_target_property(tx4_sources bootloader-week4.elf SOURCES)
add_executable(tx-week4.elf ${tx4_sources} tests/tx_fault_state.c)
target_compile_definitions(tx-week4.elf PRIVATE UART_TX_FAULT_TEST)
target_include_directories(tx-week4.elf PRIVATE src/week4 src/week3 src/week2 tests)
configure_stage4(tx-week4.elf src/week4/linker.ld)
get_target_property(txa_sources application.elf SOURCES)
add_executable(tx-application.elf ${txa_sources} tests/tx_fault_state.c)
target_compile_definitions(tx-application.elf PRIVATE UART_TX_FAULT_TEST)
target_include_directories(tx-application.elf PRIVATE src/week2 tests)
configure_stage4(tx-application.elf src/week4/app/linker.ld)
''')
   build=temp/'build'
   run(['cmake','-S',str(work),'-B',str(build),'-G','Unix Makefiles','-DCMAKE_TOOLCHAIN_FILE='+str(work/'cmake/arm-none-eabi.cmake')],'configure',work)
   run(['cmake','--build',str(build),'--target','tx-week3.elf','tx-week4.elf','tx-application.elf','bootloader-week3.elf','bootloader-week4.elf','application.elf','--parallel'],'build',work)
   for name in ['bootloader-week3','bootloader-week4','application']:
    symbols=run(['arm-none-eabi-nm',str(build/(name+'.elf'))],name+'-symbols',work)
    assert 'uart_tx_test_' not in symbols
   record('TX-PRODUCTION','normal target builds have no uart_tx_test_* symbols','three production ELF files verified',['bootloader-week3-symbols.txt','bootloader-week4-symbols.txt','application-symbols.txt'])
   report['elf_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in build.glob('*.elf')}
   appbin=build/'app.bin';run(['arm-none-eabi-objcopy','-O','binary',str(build/'application.elf'),str(appbin)],'app-binary',work)
   valid=build/'valid.bin';run(['python3','scripts/pack-week4.py','--app',str(appbin),'--output',str(valid)],'pack-valid',work)
   injected=build/'injected-app.bin';run(['arm-none-eabi-objcopy','-O','binary',str(build/'tx-application.elf'),str(injected)],'injected-app-binary',work)
   appflash=build/'injected-flash.bin';run(['python3','scripts/pack-week4.py','--app',str(injected),'--output',str(appflash)],'pack-injected',work)

   def integration(ident,boot,symbols,image,body,expected):
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    serial=out/(ident+'-uart.log');diag=(out/(ident+'-qemu.txt')).open('w')
    args=['qemu-system-arm','-M','mps2-an500','-cpu','cortex-m7','-display','none','-monitor','none','-serial','file:'+str(serial),'-S','-gdb',f'tcp:127.0.0.1:{port}','-kernel',str(boot)]
    if image:args+=['-device',f'loader,file={image},addr=0x20300000,force-raw=on']
    vm=subprocess.Popen(args,cwd=work,stdout=diag,stderr=subprocess.STDOUT)
    try:
     script=out/(ident+'.gdb');script.write_text('set pagination off\nset confirm off\nset remotetimeout 3\ntarget remote 127.0.0.1:'+str(port)+'\n'+body+'\npython print("TX_FAILURE_PATH_PASS")\ndetach\nquit\n')
     text=run(['arm-none-eabi-gdb','--batch','-q',str(symbols),'-x',str(script)],ident,work,20);assert 'TX_FAILURE_PATH_PASS' in text
    finally:
     if vm.poll() is None:
      vm.terminate()
      try:vm.wait(timeout=3)
      except subprocess.TimeoutExpired:vm.kill();vm.wait(timeout=3)
     diag.close()
    uart=serial.read_text();assert 'APP: RUNNING' not in uart and 'PONG' not in uart
    record(ident,expected,'GDB assertions PASS; no APP: RUNNING/PONG; QEMU reaped',[ident+'.txt',ident+'.gdb',ident+'-uart.log'])

   verify='''python
import gdb
reads=int(gdb.parse_and_eval('uart_tx_test_reads')); data=int(gdb.parse_and_eval('uart_tx_test_data'))
print('TX_STATE_READS=%d DATA=0x%x' % (reads,data))
assert reads==1000000 and data==0x55
end
'''
   for stage,idle,status in [(3,'fault_idle',3),(4,'boot_idle',0x101)]:
    ident=f'TX-W{stage}-PRINT'
    body=f'hbreak {idle}\ncontinue\n'+verify+f'''python
status=int(gdb.parse_and_eval('boot_status')); print('boot_status=0x%x' % status); assert status=={status}
end
'''
    integration(ident,build/f'tx-week{stage}.elf',build/f'tx-week{stage}.elf',valid if stage==4 else None,body,f'boot_status={status}; idle; 1000000 reads; no successful DATA write')
   integration('TX-W4-FLUSH',build/'tx-week4.elf',build/'tx-week4.elf',valid,'''hbreak main
continue
set var uart_tx_test_full=0
hbreak src/week4/main.c:55
continue
python
import gdb
before=int(gdb.parse_and_eval('uart_tx_test_reads'))
end
set var uart_tx_test_full=1
hbreak boot_idle
continue
python
import gdb
status=int(gdb.parse_and_eval('boot_status')); delta=int(gdb.parse_and_eval('uart_tx_test_reads'))-before
print('boot_status=0x%x final_flush_reads=%d' % (status,delta))
assert status==0x101 and delta==1000000
end
''','final uart_flush failure sets boot_status=0x101; no application jump')
   integration('TX-W4-APP',build/'bootloader-week4.elf',build/'tx-application.elf',appflash,'''hbreak app_idle
continue
python
import gdb
status=int(gdb.parse_and_eval('app_status')); handoff=int(gdb.parse_and_eval('app_handoff_ok'))
reads=int(gdb.parse_and_eval('uart_tx_test_reads'));data=int(gdb.parse_and_eval('uart_tx_test_data'))
print('app_status=%d handoff_ok=%d reads=%d DATA=0x%x' % (status,handoff,reads,data))
assert status==4 and handoff==1 and reads==1000000 and data==0x55
end
''','valid boot handoff; application TX failure sets app_status=4; no RUNNING')
  report['status']='PASS'
 finally:
  (out/'summary.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');(ROOT/'logs/tx-full-latest.txt').write_text(str(out)+'\n');print('Evidence:',out)
if __name__=='__main__': main()
