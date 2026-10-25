#!/usr/bin/env python3
"""Week-1 evidence: clean build, reset contract, poisoned RAM, negative paths.

Every execution creates a separate evidence directory and an isolated build.
Only the debugger changes emulated RAM; source firmware is unchanged.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time
from datetime import datetime
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
os.environ['PATH'] = '/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:' + os.environ.get('PATH', '')
STAMP = datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S')
OUT = ROOT / 'logs' / ('week1-review-' + STAMP)
OUT.mkdir(parents=True, exist_ok=False)
summary = {'started_at': datetime.now(ZoneInfo('Asia/Shanghai')).isoformat(), 'checks': [], 'status': 'RUNNING'}

def save():
    (OUT / 'summary.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n')

def run(args, name, expected=0, timeout=120):
    try:
        result = subprocess.run(args, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b''
        if isinstance(output, bytes):
            output = output.decode(errors='replace')
        (OUT / (name + '.txt')).write_text(output + '\nTIMEOUT\n')
        raise RuntimeError(f'{name}: timeout after {timeout}s') from error
    (OUT / (name + '.txt')).write_text('$ ' + ' '.join(map(str, args)) + '\n' + result.stdout + f'\nEXIT_CODE={result.returncode}\n')
    if result.returncode != expected:
        raise RuntimeError(f'{name}: expected exit {expected}, got {result.returncode}')
    return result.stdout

def record(name, evidence, **details):
    summary['checks'].append({'name': name, 'status': 'PASS', 'evidence': evidence, **details})
    save()
    print('PASS:', name, flush=True)


def qemu_command(elf):
    return ['qemu-system-arm', '-M', 'mps2-an500', '-cpu', 'cortex-m7',
            '-nographic', '-monitor', 'none', '-serial', 'none',
            '-semihosting-config', 'enable=on,target=native', '-kernel', str(elf)]


def debugger_case(name, elf, body, expected_exit, expected_text):
    # The socket is released before QEMU binds. A conflict causes this test to fail,
    # rather than silently attaching to an unknown instance.
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    commands = OUT / (name + '.gdb')
    commands.write_text('set pagination off\nset confirm off\nset remotetimeout 5\n'
                        f'target remote 127.0.0.1:{port}\n' + body + '\ndetach\nquit\n')
    qlog = OUT / (name + '-qemu.txt')
    with qlog.open('w') as stream:
        qemu = subprocess.Popen(qemu_command(elf) + ['-S', '-gdb', f'tcp:127.0.0.1:{port}'],
                                cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
        try:
            time.sleep(0.8)
            if qemu.poll() is not None:
                raise RuntimeError(f'{name}: QEMU failed to start')
            gout = run(['arm-none-eabi-gdb', '--batch', '-q', str(elf), '-x', str(commands)], name + '-gdb', timeout=30)
            if f'CHECK_PASS:{name}' not in gout:
                raise RuntimeError(f'{name}: missing GDB assertion marker')
            code = qemu.wait(timeout=10)
            if code != expected_exit:
                raise RuntimeError(f'{name}: expected QEMU exit {expected_exit}, got {code}')
        finally:
            if qemu.poll() is None:
                qemu.terminate()
                try:
                    qemu.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    qemu.kill()
                    qemu.wait(timeout=5)
    output = qlog.read_text()
    if expected_text not in output or (expected_exit != 0 and 'PASS:' in output):
        raise RuntimeError(f'{name}: unexpected firmware output')
    with qlog.open('a') as stream:
        stream.write(f'\nEXIT_CODE={code}\n')
    record(name, [commands.name, name + '-gdb.txt', qlog.name], qemu_exit=code)


save()
try:
    versions = []
    for name, minimum in [('qemu-system-arm', (7, 0)), ('arm-none-eabi-gcc', (12, 3)),
                           ('cmake', (3, 20)), ('arm-none-eabi-gdb', None), ('make', None), ('git', None)]:
        first = run([name, '--version'], name + '-version').splitlines()[0]
        match = re.search(r'(\d+)\.(\d+)', first)
        if minimum and (not match or tuple(map(int, match.groups())) < minimum):
            raise RuntimeError(f'{name}: minimum version {minimum} not met')
        versions.append(first)
    summary['versions'] = versions
    record('tool-versions', [name + '-version.txt' for name in ['qemu-system-arm','arm-none-eabi-gcc','cmake','arm-none-eabi-gdb','make','git']])
    (ROOT / 'build').mkdir(exist_ok=True)
    build = Path(tempfile.mkdtemp(prefix='week1-review-', dir=ROOT / 'build'))
    run(['cmake', '-S', str(ROOT), '-B', str(build), '-G', 'Unix Makefiles',
         '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'cmake/arm-none-eabi.cmake')], 'configure')
    run(['cmake', '--build', str(build), '--target', 'demo.elf', '--parallel'], 'build')
    elf = build / 'demo.elf'
    summary['elf'] = str(elf.relative_to(ROOT))
    summary['elf_sha256'] = hashlib.sha256(elf.read_bytes()).hexdigest()
    summary['source_sha256'] = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in
                               ['src/main.c','src/startup.S','src/linker.ld','CMakeLists.txt','cmake/arm-none-eabi.cmake','scripts/verify-startup.py']}
    run(['arm-none-eabi-size', str(elf)], 'size')
    run(['arm-none-eabi-objdump', '-s', '-j', '.isr_vector', str(elf)], 'vectors')
    run(['arm-none-eabi-nm', '-n', str(elf)], 'symbols')
    record('clean-build', ['configure.txt','build.txt','size.txt','vectors.txt','symbols.txt'])
    output = run(qemu_command(elf), 'nominal-qemu', timeout=10)
    if 'PASS: Cortex-M7; .data initialized; .bss zero; bare-metal C99' not in output:
        raise RuntimeError('nominal run did not report PASS')
    record('nominal-run', ['nominal-qemu.txt'], qemu_exit=0)
    poison = '''python
import gdb
import struct
inf = gdb.selected_inferior()
def addr(name):
    return int(gdb.parse_and_eval('(unsigned int)&' + name))
def word(where):
    return struct.unpack('<I', bytes(inf.read_memory(where, 4)))[0]
def require(condition, message):
    if not condition:
        raise gdb.GdbError(message)
stack = addr('_stack_top')
reset = addr('Reset_Handler') & ~1
require(int(gdb.parse_and_eval('$sp')) == stack, 'Wrong initial MSP')
require(int(gdb.parse_and_eval('$pc')) == reset, 'Wrong reset PC')
require(word(0) == stack and word(4) == (reset | 1), 'Wrong reset vectors')
print('RESET_MSP=0x%x RESET_PC=0x%x RESET_VECTOR=0x%x' % (stack, reset, word(4)))
ds, de = addr('_sdata'), addr('_edata')
bs, be = addr('_sbss'), addr('_ebss')
require(ds < de <= bs < be, 'Unexpected section layout')
expected = bytes(inf.read_memory(addr('_sidata'), de-ds))
inf.write_memory(ds, b'\\xCC' * (de-ds))
inf.write_memory(bs, b'\\xA5' * (be-bs))
inf.write_memory(be, b'\\x78\\x56\\x34\\x12')
require(bytes(inf.read_memory(ds,de-ds)) == b'\\xCC'*(de-ds), 'Data injection failed')
require(bytes(inf.read_memory(bs,be-bs)) == b'\\xA5'*(be-bs), 'BSS injection failed')
print('INJECTED_DATA=' + bytes(inf.read_memory(ds,de-ds)).hex())
print('INJECTED_BSS=' + bytes(inf.read_memory(bs,be-bs)).hex())
end
break main
continue
python
require(int(gdb.parse_and_eval('$pc')) == (addr('main') & ~1), 'main breakpoint not reached')
actual = bytes(inf.read_memory(ds,de-ds))
zeros = bytes(inf.read_memory(bs,be-bs))
require(actual == expected, '.data copy failed')
require(zeros == bytes(be-bs), '.bss clear failed')
require(word(be) == 0x12345678, 'Upper-bound sentinel overwritten')
print('RESTORED_DATA=' + actual.hex())
print('CLEARED_BSS=' + zeros.hex())
print('UPPER_SENTINEL=0x%x' % word(be))
end
stepi
python
require(int(gdb.parse_and_eval('$pc')) != (addr('main') & ~1), 'Single step did not advance PC')
print('STEP_PC=0x%x' % int(gdb.parse_and_eval('$pc')))
print('CHECK_PASS:poisoned-startup')
end
'''
    debugger_case('poisoned-startup', elf, poison, 0, 'PASS: Cortex-M7;')
    for name, variable in [('corrupt-data','initialized_data'), ('corrupt-bss','zero_data')]:
        value = '0xdeadbeef'
        body = f'''break main
continue
set variable {variable} = {value}
python
import gdb
if int(gdb.parse_and_eval('{variable}')) != 0xdeadbeef:
    raise gdb.GdbError('Fault injection failed')
print('INJECTED_{variable}=0x%x' % int(gdb.parse_and_eval('{variable}')))
end
break semihost
continue
python
if int(gdb.parse_and_eval('demo_result')) != 0xdead:
    raise gdb.GdbError('Firmware did not detect injected corruption')
print('DEMO_RESULT=0x%x' % int(gdb.parse_and_eval('demo_result')))
print('CHECK_PASS:{name}')
end
'''
        debugger_case(name, elf, body, 1, 'FAIL: Cortex-M7 or C runtime validation')
    summary['status'] = 'PASS'
except Exception as error:
    summary['status'] = 'FAIL'
    summary['error'] = str(error)
    print('FAIL:', error, file=sys.stderr, flush=True)
finally:
    summary['finished_at'] = datetime.now(ZoneInfo('Asia/Shanghai')).isoformat()
    save()
    (ROOT/'logs/week1-review-latest.txt').write_text(OUT.name + '\n')
    print('Evidence:', OUT, flush=True)
if summary['status'] != 'PASS':
    sys.exit(1)
