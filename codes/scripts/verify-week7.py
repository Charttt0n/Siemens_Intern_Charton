#!/usr/bin/env python3
"""Fresh Week 7 Unity + Cortex-M7 + signed upgrade/fault acceptance.
Every PASS has assertions and retained raw evidence; a failure exits nonzero.
"""
from pathlib import Path
from datetime import datetime
from zoneinfo import ZoneInfo
import argparse, hashlib, importlib.util, json, re, socket, struct, subprocess, threading, time, traceback
from week7_session import Session
from week5_session import sender, PeerCancelled
from week6_package import flash_seed, build_signed_package
from week5_package import build_package
from week5_meta import read_meta
from week7_unit import run_units
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path);p.add_argument('--build-dir',type=Path)
    a=p.parse_args();stamp=datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S-%f')
    out=(a.out or ROOT/'logs'/('week7-'+stamp)).resolve();out.mkdir(parents=True,exist_ok=True)
    build=(a.build_dir or out/'build').resolve()
    summary={'status':'RUNNING','timezone':'Asia/Shanghai','started_at':stamp,'checks':[],'unity':[],
             'scope':'Week 7 QEMU AN500 simulation','evidence':str(out)}
    def save(): (out/'summary.json').write_text(json.dumps(summary,indent=2,ensure_ascii=False)+'\n')
    def record(name,**detail):
        summary['checks'].append({'name':name,'status':'PASS',**detail});save();print('PASS:',name,flush=True)
    def run(cmd,name,timeout=300):
        r=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True,timeout=timeout)
        (out/(name+'.txt')).write_text('$ '+' '.join(map(str,cmd))+'\n'+r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+': '+r.stderr[-2000:])
        return r.stdout+r.stderr
    def image(name,raw):
        f=out/(name+'.bin');f.write_bytes(raw);return f
    try:
        save();run(['bash','scripts/build-week7.sh',str(build)],'build')
        run(['cmake','--build',str(build),'--target','unity-week7.elf','--parallel'],'build-target-unity')
        for tool in ['arm-none-eabi-gcc','qemu-system-arm','cc','arm-none-eabi-gdb']:
            run([tool,'--version'],'version-'+tool)
        bins={name:{'pkg':build/f'application-week7-{name}.pkg','bin':build/f'application-week7-{name}.bin'} for name in ['v1','v2','v3','hang']}
        pkgs={n:x['pkg'].read_bytes() for n,x in bins.items()};boot=build/'bootloader-week7.elf';fault=build/'bootloader-week7-fault.elf'
        record('fresh-build')
        summary['unity']=run_units(out/'unity',bins['v1']['pkg'])
        target=run(['qemu-system-arm','-M','mps2-an500','-display','none','-serial','none','-monitor','none',
                    '-semihosting-config','enable=on,target=native','-kernel',str(build/'unity-week7.elf')],'unity-target',60)
        assert '11 Tests 0 Failures 0 Ignored' in target,target
        summary['unity'].append({'suite':'cortex-m7','tests':11,'failures':0,'ignored':0,'log':'unity-target.txt'})
        record('unity-all',tests=sum(s['tests'] for s in summary['unity']))
        syms=run(['arm-none-eabi-nm','-n',str(boot)],'production-symbols')
        assert '__wrap_flash_' not in syms and 'fault_latched' not in syms
        record('production-excludes-fault-hooks')
        base=flash_seed({'a':pkgs['v1']})
        def boot_image(name,raw,version,markers=()):
            f=image(name,raw)
            with Session(boot,f,out/name) as vm:
                text=vm.boot(timeout=30)
                assert f'APP: VERSION v{version}'.encode() in text,text
                assert b'APP: BOOT CONFIRMED' in text,text
                for marker in (b'APP: CRT PASS',b'APP: HANDOFF PASS',b'APP: SVC PASS'): assert marker in text,text
                for m in markers: assert m in text,text
                snap=out/name/'after.bin';vm.fault_snapshot(snap)
            return snap.read_bytes()
        base=boot_image('signed-boot',base,1);record('signed-boot')
        def upload(name,raw,pkg,elf=boot,version=1,markers=(),success=False):
            f=image(name,raw)
            with Session(elf,f,out/name) as vm:
                sent,text=vm.upload(pkg)
                assert f'APP: VERSION v{version}'.encode() in text,text
                for m in markers:assert m in text,text
                if success:assert sent and b'UPGRADE: COMMITTED' in text,text
                else:assert b'UPGRADE: COMMITTED' not in text,text
                snap=out/name/'after.bin';vm.fault_snapshot(snap)
            return snap.read_bytes()
        upgraded=upload('upgrade-v2',base,bins['v2']['pkg'],version=2,markers=(b'TRIAL: LAUNCH',b'APP: BOOT CONFIRMED'),success=True)
        assert read_meta(upgraded)['active']==2;record('signed-upgrade-and-confirm')
        for kind in ['unsigned','bad-signature','corrupt-payload','signed-header-tamper']:
            bad=bytearray(pkgs['v3'])
            if kind=='unsigned':bad=build_package(bins['v3']['bin'].read_bytes(),3)
            elif kind=='bad-signature':bad[-1]^=1
            elif kind=='corrupt-payload':bad[32+200]^=1
            else:
                import zlib
                struct.pack_into('<I',bad,20,99);struct.pack_into('<I',bad,28,zlib.crc32(bad[:28]))
            path=out/(kind+'.pkg');path.write_bytes(bad)
            got=upload(kind,upgraded,path,version=2,markers=(b'UPGRADE: FAIL',))
            assert read_meta(got)['active']==2
            assert got[0x80000:0xf0000]==upgraded[0x80000:0xf0000]
            record(kind+'-rejected')
        large_payload=bins['v2']['bin'].read_bytes()
        large_payload+=bytes(0x70000-32-272-len(large_payload))
        maximum=out/'maximum-signed.pkg';maximum.write_bytes(build_signed_package(large_payload,2))
        got=upload('maximum-signed-upgrade',base,maximum,version=2,markers=(b'TRIAL: LAUNCH',),success=True)
        assert read_meta(got)['active']==2
        record('maximum-signed-upgrade',package_bytes=maximum.stat().st_size,payload_bytes=len(large_payload))
        ab=bytearray(flash_seed({'a':pkgs['v1'],'b':pkgs['v2']}));ab[0x10000+232]^=1
        boot_image('corrupt-active-fallback',ab,2,(b'ROLLBACK: SELECT B',));record('corrupt-active-fallback')
        ab[0x80000+232]^=1
        with Session(boot,image('both-invalid',ab),out/'both-invalid') as vm:
            vm.command('boot',expect='ROLLBACK: NO VALID BACKUP')
            assert b'APP: VERSION' not in vm.transcript
        record('both-invalid-remain-cli')
        # Full text commands, decimal overflow, truncation, confirmation gate.
        with Session(boot,image('cli',base),out/'cli') as vm:
            text=vm.command('version',expect='auto_boot=1\r\n');assert b'BOOT7: BL week7-1' in text
            vm.command('p auto_boot 0',expect='CONFIG: SAVED')
            vm.command('p window_ms 18446744073709555616',expect='CONFIG: BAD window_ms')
            vm.command('p window_ms 4000'+' '*50,expect='CONFIG: LINE TOO LONG')
            vm.command('YES',expect='unknown command');assert b'FACTORY: DONE' not in vm.transcript
            vm.command('factory',expect='FACTORY: send YES');vm.command('help',expect='BOOT7: commands')
            vm.command('YES',expect='unknown command');assert b'FACTORY: DONE' not in vm.transcript
            vm.command('p window_ms 4000',expect='CONFIG: SAVED')
            vm.command('p',expect='PARAM auto_boot=0\r\n')
            vm.fault_snapshot(out/'params.bin')
        with Session(boot,out/'params.bin',out/'params-restart') as vm:
            text=vm.command('param',expect='PARAM auto_boot=0\r\n');assert b'04000' in text
            vm.command('factory',expect='FACTORY: send YES');vm.command('YES',expect='FACTORY: DONE')
            vm.fault_snapshot(out/'factory.bin')
        factory=(out/'factory.bin').read_bytes()
        assert factory[0x10000:0xf0000]==base[0x10000:0xf0000]
        boot_image('factory-boot',factory,1);record('cli-validation-persistence-factory')
        cfgbad=bytearray(base);cfgbad[0xf2000:0xf4000]=bytes(0x2000)
        with Session(boot,image('cfg-corrupt',cfgbad),out/'cfg-corrupt') as vm:
            vm.command('boot',expect='RECOVERY: CFG RESET REQUIRED')
            assert b'APP: VERSION' not in vm.transcript
        record('corrupt-config-fails-closed')
        # Test-only low-level NOR errors still pass through the production
        # update, validation, metadata and recovery functions.
        for fid,name in [(10,'erase-error'),(11,'program-error'),(12,'readback-error'),(14,'trial-arm-error')]:
            raw=bytearray(base);struct.pack_into('<I',raw,len(raw)-4,fid)
            got=upload(name,raw,bins['v2']['pkg'],elf=fault,markers=(b'UPGRADE:',))
            assert read_meta(got)['active']==1 and got[0x10000:0x80000]==base[0x10000:0x80000]
            record(name+'-keeps-old')
        raw=bytearray(base);struct.pack_into('<I',raw,len(raw)-4,15)
        with Session(fault,image('param-write-error',raw),out/'param-write-error') as vm:
            vm.command('p window_ms 4000',expect='CONFIG: SAVE FAIL')
            text=vm.command('p',expect='PARAM auto_boot=1\r\n');assert b'02500' in text
        record('param-write-error-keeps-runtime')
        raw=bytearray(flash_seed({'a':pkgs['v1'],'b':pkgs['v2']}));struct.pack_into('<I',raw,len(raw)-4,13)
        with Session(fault,image('active-read-error',raw),out/'active-read-error') as vm:
            text=vm.boot();assert b'ROLLBACK: SELECT B' in text and b'APP: VERSION v2' in text
        record('active-read-error-fallback')
        nm=run(['arm-none-eabi-nm','-n',str(fault)],'fault-symbols')
        address=int(re.search(r'^([0-9a-f]+) \w fault_latched$',nm,re.M)[1],16)
        for fid,name in [(1,'target-erase-tear'),(2,'target-write-tear'),(3,'trial-body-tear'),(4,'trial-marker-complete'),
                          (5,'activation-body-tear'),(6,'activation-marker-tear'),(7,'activation-marker-complete')]:
            raw=bytearray(base);raw[0x80000:0xf0000]=bytes(0x70000);struct.pack_into('<I',raw,len(raw)-4,fid)
            result={}
            with Session(fault,image(name,raw),out/name) as vm:
                vm.write(b'u');vm.sending=True
                def transfer():
                    try:result['sent']=sender(vm).send([str(bins['v2']['pkg'])])
                    except PeerCancelled:result['cancelled']=True
                    except Exception as e:result['error']=repr(e)
                worker=threading.Thread(target=transfer,daemon=True);worker.start()
                deadline=time.monotonic()+25
                while time.monotonic()<deadline:
                    value=vm.execute('human-monitor-command',{'command-line':f'xp /1wx 0x{address:x}'})
                    m=re.search(r':\s*0x([0-9a-fA-F]+)',value)
                    if m and int(m[1],16)==fid:break
                    time.sleep(.03)
                else:raise TimeoutError(f'{name} hook not hit: {result}')
                snap=out/name/'nor-at-fault.bin';vm.fault_snapshot(snap)
                vm.cancel_sender=True;vm.serial.shutdown(socket.SHUT_RDWR);worker.join(3)
                assert not worker.is_alive()
            captured=snap.read_bytes();active=2 if fid==7 else 1
            assert read_meta(captured)['active']==active
            assert captured[0x10000:0x80000]==base[0x10000:0x80000]
            if fid==1:assert captured[0x80000:0x80049]==b'\xff'*73 and captured[0x80049:0xf0000]==bytes(0x70000-73)
            if fid==2:assert captured[0x80000:0x80042]==pkgs['v2'][:66]
            markers=(b'TRIAL: LAUNCH',) if fid==7 else ()
            boot_image('restart-'+name,captured,2 if active==2 else 1,markers)
            record(name+'-cold-restart',fault_id=fid,active_slot=active,snapshot=str(snap.relative_to(out)))
        # Watchdog: snapshot pauses the VM. Resume explicitly and require a
        # QMP RESET event before making any claim about watchdog operation.
        with Session(boot,image('watchdog-trial',base),out/'watchdog-trial') as vm:
            vm.write(b'u');vm.sending=True
            try:assert sender(vm).send([str(bins['hang']['pkg'])])
            finally:vm.sending=False
            vm.read_until(b'APP: RUNNING',30)
            assert b'TRIAL: LAUNCH' in vm.transcript and b'APP: BOOT CONFIRMED' not in vm.transcript
            vm.fault_snapshot(out/'trial-launched.bin');vm.execute('cont')
            deadline=time.monotonic()+12
            while time.monotonic()<deadline:
                try:vm._recv(4096,.2)
                except socket.timeout:pass
                vm.execute('query-status')
                if any(e.get('event')=='RESET' for e in vm.events):break
            else:raise AssertionError('no actual watchdog RESET event')
            summary['watchdog_events']=[e for e in vm.events if e.get('event')=='RESET']
        record('watchdog-reset-observed')
        boot_image('watchdog-snapshot-restart',(out/'trial-launched.bin').read_bytes(),1,(b'ROLLBACK: TRIAL UNCONFIRMED',))
        record('unconfirmed-trial-rollback',method='explicit pre-reset NOR snapshot and cold restart')
        summary['status']='PASS'
    except BaseException as e:
        summary['status']='FAIL';summary['error']=repr(e);(out/'failure.txt').write_text(traceback.format_exc());print(traceback.format_exc(),flush=True)
    finally:
        summary['finished_at']=datetime.now(ZoneInfo('Asia/Shanghai')).isoformat()
        summary['source_sha256']={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest()
            for folder in ['src','cmake','tests/week7','scripts'] for f in (ROOT/folder).rglob('*') if f.is_file() and '__pycache__' not in f.parts}
        save();(ROOT/'logs/week7-latest.txt').write_text(out.name+'\n')
    print('SUMMARY',summary['status'],len(summary['checks']),out,flush=True)
    return summary['status']!='PASS'
if __name__=='__main__':raise SystemExit(main())
