#!/usr/bin/env python3
"""Read back the Week4 handoff contract and exercise real Cortex-M exceptions.

Fault instructions run in disposable QEMU RAM, never in shipped firmware.
Each case starts a fresh CPU. A fault test passes only if the expected handler,
status registers and exception frame are observed, not merely a timeout.
"""
from datetime import datetime
from zoneinfo import ZoneInfo
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

from image_format import package

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('stage4', Path(__file__).with_name('verify-week4.py'))
stage4 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage4)

GDB_HELPERS = '''
import gdb, json, struct
inf = gdb.selected_inferior()
def word(addr): return struct.unpack('<I', bytes(inf.read_memory(addr, 4)))[0]
def reg(name): return int(gdb.parse_and_eval('$' + name)) & 0xffffffff
def snapshot():
    data = {name: reg(name) for name in ('sp','msp','psp','control','primask','basepri','faultmask','xpsr','lr')}
    for name, addr in {'vtor':0xe000ed08,'aircr':0xe000ed0c,'shpr1':0xe000ed18,
                       'shpr2':0xe000ed1c,'shpr3':0xe000ed20,'shcsr':0xe000ed24,
                       'cfsr':0xe000ed28,'hfsr':0xe000ed2c,'cpacr':0xe000ed88,
                       'fpccr':0xe000ef34,'ccr':0xe000ed14}.items(): data[name] = word(addr)
    data['ipsr'] = data['xpsr'] & 0x1ff
    return data
'''


def main():
    out = ROOT / 'logs' / datetime.now(ZoneInfo('Asia/Shanghai')).strftime('handoff-contract-%Y%m%d-%H%M%S')
    out.mkdir(parents=True)
    stage4.OUT = out
    stage4.summary = {'status':'RUNNING','checks':[]}
    observations = []
    try:
        with tempfile.TemporaryDirectory(prefix='handoff-contract-') as temp:
            build = Path(temp)
            stage4.run(['cmake','-S',str(ROOT),'-B',str(build),'-G','Unix Makefiles',
                        '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/arm-none-eabi.cmake')], 'configure')
            stage4.run(['cmake','--build',str(build),'--target','bootloader-week4.elf',
                        'application.elf','--parallel'], 'build')
            boot, app = build/'bootloader-week4.elf', build/'application.elf'
            stage4.run(['arm-none-eabi-objcopy','-O','binary',str(app),str(build/'app.bin')], 'objcopy')
            seed = build/'valid.bin'
            seed.write_bytes(package((build/'app.bin').read_bytes()))
            bs = stage4.symbols(boot, 'boot-symbols')
            aps = stage4.symbols(app, 'app-symbols')
            stage4.summary['elf_sha256'] = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (boot,app)}
            # Production path: actual CPSID, dirty pending state, and distinct
            # priorities. This intentionally proves preservation, not reset.
            body = f'''
break *{bs['boot_handoff']}
continue
python
{GDB_HELPERS}
before = snapshot()
assert before['ipsr'] == 0 and not (before['control'] & 3)
print('BEFORE ' + json.dumps(before, sort_keys=True))
for _ in range(8):
    if reg('primask') == 1: break
    gdb.execute('stepi')
assert reg('primask') == 1
end
set {{unsigned int}}0xe000e100 = 0x80
set {{unsigned int}}0xe000e200 = 0x80
set {{unsigned int}}0xe000e014 = 999999
set {{unsigned int}}0xe000e010 = 7
set {{unsigned int}}0xe000ed04 = 0x14000000
set {{unsigned int}}0xe000ed18 = 0x00604020
set {{unsigned int}}0xe000ed1c = 0x80000000
python
assert word(0xe000e100) & 0x80 and word(0xe000e200) & 0x80
assert word(0xe000ed04) & 0x14000000 == 0x14000000
assert word(0xe000e010) & 7 == 7
assert word(0xe000e300) == 0
print('DIRTY pending and enable state confirmed')
end
delete breakpoints
break *{aps['App_Reset_Handler']}
continue
python
entry = snapshot()
assert entry['ipsr'] == 0 and entry['control'] == 0
assert entry['msp'] == 0x20200000 and entry['psp'] == 0 and entry['sp'] == entry['msp']
assert entry['primask'] == 1 and entry['basepri'] == entry['faultmask'] == 0
assert entry['vtor'] == 0x10000
assert entry['shpr1'] == 0x00604020 and entry['shpr2'] == 0x80000000
assert ((entry['aircr'] >> 8) & 7) == ((before['aircr'] >> 8) & 7)
assert entry['shcsr'] == before['shcsr'] == 0
assert entry['cpacr'] & 0x00f00000 == 0 and entry['fpccr'] & 1 == 0
assert word(0xe000e100) == word(0xe000e200) == word(0xe000e300) == 0
assert word(0xe000e010) & 7 == 0 and word(0xe000ed04) & 0x14000000 == 0
print('ENTRY ' + json.dumps(entry, sort_keys=True))
end
delete breakpoints
break *{aps['App_SVC_Handler']}
continue
python
svc = snapshot()
assert svc['ipsr'] == 11 and svc['sp'] == svc['msp'] and svc['psp'] == 0
assert svc['primask'] == svc['basepri'] == svc['faultmask'] == 0
assert svc['lr'] == 0xfffffff9
print('SVC ' + json.dumps(svc, sort_keys=True))
end
delete breakpoints
break *{aps['app_idle']}
continue
python
final = snapshot()
assert final['ipsr'] == 0 and final['control'] == 0 and final['primask'] == 0
assert word({aps['app_status']}) == word({aps['app_handoff_ok']}) == word({aps['app_svc_count']}) == 1
assert word({aps['app_exception']}) == 0
print('FINAL ' + json.dumps(final, sort_keys=True))
end
'''
            stage4.debug_case(boot, seed, 'HANDOFF-REGISTERS', body, stage4.GOOD)
            for line in (out/'HANDOFF-REGISTERS.txt').read_text().splitlines():
                for label in ('BEFORE','ENTRY','SVC','FINAL'):
                    if line.startswith(label+' '): observations.append({'point':label,**json.loads(line[len(label)+1:])})

            common = 'cpsie i\nmovs r0, #0\nmsr basepri, r0\nmsr faultmask, r0\nisb\n'
            psp = 'ldr r0, =0x201ff000\nmsr psp, r0\nmovs r0, #2\nmsr control, r0\nisb\n'
            cases = [
                ('USAGE-ENABLED', 1<<18, common+'udf #0', 6, 1<<16, False),
                ('USAGE-DISABLED-ESCALATION', 0, common+'udf #0', 3, 1<<16, False),
                ('MEMMANAGE-XN', 1<<16, common+'ldr r0, =0xe0000001\nbx r0', 4, 1, False),
                ('BUSFAULT-PRECISE', 1<<17, common+'ldr r0, =0xfffffff0\nldr r1, [r0]', 5, 1<<9, False),
                ('SVC-MASKED-ESCALATION', 0, 'cpsid i\nsvc #0', 3, 0, False),
                ('SVC-PSP-FRAME', 0, common+psp+'svc #0', 11, 0, True),
            ]
            for name, shcsr, instructions, number, cfsr_bit, uses_psp in cases:
                source = build/(name+'.S')
                source.write_text('.syntax unified\n.cpu cortex-m7\n.thumb\n.global probe\n.thumb_func\nprobe:\n'+instructions+'\nb .\n.ltorg\n')
                obj, binary = build/(name+'.o'), build/(name+'.bin')
                stage4.run(['arm-none-eabi-gcc','-mcpu=cortex-m7','-mthumb','-mfloat-abi=soft','-c',str(source),'-o',str(obj)], name+'-assemble')
                stage4.run(['arm-none-eabi-objcopy','-O','binary','-j','.text',str(obj),str(binary)], name+'-binary')
                code = binary.read_bytes()
                body = f'''
break *{bs['main']}
continue
python
{GDB_HELPERS}
assert reg('xpsr') & 0x1ff == 0
inf.write_memory(0x70000, bytes.fromhex('{code.hex()}'))
end
set {{unsigned int}}0xe000ed24 = {shcsr}
set {{unsigned int}}0xe000ed28 = 0xffffffff
set {{unsigned int}}0xe000ed2c = 0xffffffff
delete breakpoints
break *{bs['Default_Handler']}
set $pc = 0x70000
continue
python
state = snapshot()
assert state['ipsr'] == {number}, state
assert state['sp'] == state['msp']
assert state['lr'] == {0xfffffffd if uses_psp else 0xfffffff9}
assert state['cfsr'] & {cfsr_bit} == {cfsr_bit}, state
if {number} == 3: assert state['hfsr'] & (1<<30)
if {uses_psp!r}:
    assert state['msp'] == 0x20010000 and state['psp'] == 0x201fefe0
    assert word(state['psp']+28) & 0x1ff == 0
print('FAULT ' + json.dumps(state, sort_keys=True))
end
delete breakpoints
break *{bs['boot_idle']}
continue
python
assert word({bs['boot_status']}) == 0x100
assert word({bs['exception_number']}) == {number}
end
'''
                stage4.debug_case(boot, seed, name, body, b'')
                for line in (out/(name+'.txt')).read_text().splitlines():
                    if line.startswith('FAULT '): observations.append({'case':name,**json.loads(line[6:])})
            stage4.summary['status'] = 'PASS'
    except Exception as error:
        stage4.summary['status'] = 'FAIL'
        stage4.summary['error'] = repr(error)
        raise
    finally:
        stage4.summary['observations'] = observations
        stage4.save()
        (ROOT/'logs/handoff-contract-latest.txt').write_text(str(out)+'\n')
        print(stage4.summary['status'], out, flush=True)


if __name__ == '__main__':
    main()
