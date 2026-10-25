#!/usr/bin/env python3
"""Teacher-feedback regression. Real CLI entry points run in a clean copy."""
import copy, hashlib, json, os, shutil, signal, subprocess, tempfile, sys
from datetime import datetime
from zoneinfo import ZoneInfo
from pathlib import Path
from delivery_manifest import generate, validate
ROOT=Path(__file__).resolve().parents[1]
os.environ['PATH']='/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:'+os.environ.get('PATH','')
OUT=ROOT/'logs'/('feedback-'+datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S'))
OUT.mkdir(); records=[]
def run(args,cwd,limit=120,input=None):
    p=subprocess.Popen(args,cwd=cwd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
    timed=False
    try: b,_=p.communicate(input,timeout=limit)
    except subprocess.TimeoutExpired:
        timed=True;os.killpg(p.pid,signal.SIGTERM);b,_=p.communicate(timeout=5)
    return p.returncode,b.decode(errors='replace'),timed
def case(id,method,inputs,expected,args,cwd,judge,limit=120,input=None):
    code,text,timed=run(args,cwd,limit,input)
    (OUT/(id+'.log')).write_text('$ '+' '.join(map(str,args))+'\n'+text+f'\nEXIT={code} TIMEOUT={timed}\n')
    ok=judge(code,text,timed)
    records.append(dict(id=id,method=method,input=inputs,expected=expected,actual=f'exit={code}, timeout={timed}; '+text[-1800:],evidence=id+'.log',status='PASS' if ok else 'FAIL'))
    assert ok,id
    print('PASS',id,flush=True)
try:
 with tempfile.TemporaryDirectory(prefix='feedback-') as temp:
    work=Path(temp)/'codes';shutil.copytree(ROOT,work,ignore=shutil.ignore_patterns('build','logs','__pycache__','.DS_Store'))
    native=Path(temp)/'uart-test'
    case('FB-TX-01','compile real driver with fake volatile MMIO','UART_NATIVE_TEST','compiler exit=0',['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DUART_NATIVE_TEST','-Isrc/week2','src/week2/uart.c','tests/test_uart_timeout.c','-o',str(native)],work,lambda c,t,x:c==0 and not x)
    case('FB-TX-02','native driver assertions','CTRL=1 STATE=1 DATA=0x55 then STATE=0','bounded -1, no DATA write; recovery writes 0x41',[str(native)],work,lambda c,t,x:c==0 and not x and 'DATA=0x41 PASS' in t,limit=5)
    elf=Path(temp)/'irq.elf'
    case('FB-IRQ-01','cross compile production IRQ/UART','nested masks 0 and 1','build succeeds',['arm-none-eabi-gcc','-mcpu=cortex-m7','-mthumb','-mfloat-abi=soft','-g','-Og','-ffreestanding','-nostdlib','-Isrc/week3','-Isrc/week2','-Tsrc/week3/linker.ld','src/week3/startup.S','src/week3/uart.c','tests/test_irq.c','-o',str(elf)],work,lambda c,t,x:c==0 and not x)
    case('FB-IRQ-02','real Cortex-M7 instructions under QEMU','initial mask=0/1; nested save/restore, uart_init, empty uart_getc','both original masks restored; exit=0',['qemu-system-arm','-M','mps2-an500','-display','none','-monitor','none','-serial','none','-semihosting-config','enable=on,target=native','-kernel',str(elf)],work,lambda c,t,x:c==0 and not x and t.count(' PASS')==2,limit=10)
    for stage,build,runner in [(1,'scripts/build.sh','scripts/run.sh'),(2,'scripts/build-week2.sh','scripts/run-week2.sh'),(3,'scripts/build-week3.sh','scripts/run-week3.py'),(4,'scripts/build-week4.sh','scripts/run-week4.py')]:
        cmd=['bash' if runner.endswith('.sh') else sys.executable,runner]
        case(f'CLI-W{stage}-MISSING','run before build','clean copy, ELF/image absent','nonzero error, no PASS',cmd,work,lambda c,t,x:c!=0 and not x and 'PASS:' not in t,limit=10)
        case(f'CLI-W{stage}-BUILD','actual build entry','clean stage output','exit=0',['bash',build],work,lambda c,t,x:c==0 and not x)
        wanted={1:'PASS: Cortex-M7',2:'PASS: C runtime',3:'PONG',4:'APP: RUNNING'}[stage]
        case(f'CLI-W{stage}-RUN','actual run entry','p, save, quit' if stage==3 else 'default demo','expected firmware marker; W2 host stops intentional idle',cmd,work,lambda c,t,x:wanted in t and (x if stage==2 else c==0 and not x),limit=3 if stage==2 else 15,input=b'p\nsave\nquit\n' if stage==3 else None)
    case('CLI-W4-MISSING-IMAGE','actual runner missing image','missing explicit image path','exit=1 and image missing',[sys.executable,'scripts/run-week4.py','--image',str(Path(temp)/'blank.bin')],work,lambda c,t,x:c==1 and not x and 'image missing' in t)
    blank=Path(temp)/'blank.bin';blank.write_bytes(b'\xff'*0x100000)
    case('CLI-W4-BLANK','actual runner rejected image','1 MiB all ff','exit=1 and REJECT: 0x00000002',[sys.executable,'scripts/run-week4.py','--image',str(blank)],work,lambda c,t,x:c==1 and not x and 'REJECT: 0x00000002' in t)
    # Missing output cannot be mistaken for firmware success.
    case('CLI-W2-PAUSED','paused emulator deadline','-S; CPU never starts','no PASS; watchdog terminates process',['bash','scripts/run-week2.sh','-S'],work,lambda c,t,x:x and 'PASS:' not in t,limit=1)
    with (work/'scripts/env.sh').open('a') as f: f.write('\ncmake() { return 42; }\n')
    case('CLI-BUILD-FAIL','controlled tool failure','cmake returns 42','build script returns 42; no stale success',['bash','scripts/build-week2.sh'],work,lambda c,t,x:c==42 and not x)
    bundle=Path(temp)/'bundle';bundle.mkdir();(bundle/'firmware.bin').write_bytes(b'test')
    good=generate(bundle,'week3-4');validate(good,bundle)
    records.append(dict(id='MANIFEST-valid',method='schema and file verification',input='valid v1 manifest and file bytes test',expected='accepted',actual='accepted',status='PASS',evidence='manifest-example.json'))
    for name,mut in [('missing',lambda d:d.pop('files')),('unknown',lambda d:d.update(extra=1)),('version',lambda d:d.update(schema_version=2)),('hash',lambda d:d['files'][0].update(sha256='xyz')),('traversal',lambda d:d['files'][0].update(path='../escape')),('size',lambda d:d['files'][0].update(size=-1)),('boolean-size',lambda d:d['files'][0].update(size=True)),('duplicate',lambda d:d['files'].append(copy.deepcopy(d['files'][0])))]:
        data=copy.deepcopy(good);mut(data)
        try:validate(data,bundle)
        except ValueError as e:
            records.append(dict(id='MANIFEST-'+name,method='schema and file verification',input=data,expected='ValueError',actual=str(e),status='PASS',evidence='manifest-cases.json'))
        else:raise AssertionError(name)
    (bundle/'firmware.bin').write_bytes(b'bad')
    try:validate(good,bundle)
    except ValueError as e: records.append(dict(id='MANIFEST-tampered-file',method='hash verification',input='file bytes test -> bad',expected='file mismatch',actual=str(e),status='PASS',evidence='manifest-cases.json'))
    else:raise AssertionError('hash tampering accepted')
    (OUT/'manifest-example.json').write_text(json.dumps(good,indent=2))
    (OUT/'manifest-cases.json').write_text(json.dumps(records[-9:],indent=2))
 status='PASS'
except BaseException:
 status='FAIL';raise
finally:
 (OUT/'summary.json').write_text(json.dumps({'status':status,'checks':records},ensure_ascii=False,indent=2)+'\n')
 (ROOT/'logs/feedback-latest.txt').write_text(str(OUT)+'\n')
 print('Evidence:',OUT)
