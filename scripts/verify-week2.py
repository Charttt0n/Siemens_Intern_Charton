#!/usr/bin/env python3
"""Reproducible UART-only QEMU acceptance, including GDB fault injection.

Each run builds a fresh ELF and captures serial bytes separately from QEMU
diagnostics. A deadline without the expected output is a failure, never a pass.
"""
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from datetime import datetime
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
os.environ['PATH'] = '/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:' + os.environ.get('PATH', '')
NOW = lambda: datetime.now(ZoneInfo('Asia/Shanghai')).isoformat()
STAMP = datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S')
OUT = ROOT / 'logs' / ('week2-' + STAMP)
HEADER = (b'BOOT: week2 mps2-an500 Cortex-M7\r\n'
          b'CLOCK: 25000000 Hz (QEMU fixed)\r\n'
          b'UART0: 115200 requested, divisor=217, 8N1\r\n')
GOOD = HEADER + b'PASS: C runtime; UART boot ready\r\n'
BAD = HEADER + b'FAIL: CPU or C runtime\r\n'
summary = {'started_at': NOW(), 'status': 'RUNNING', 'checks': [],
           'semihosting': False, 'platform': 'mps2-an500',
           'termination': 'host stops QEMU after output/state assertions; exit is not the verdict'}


def save():
    (OUT / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')


def run(args, name, timeout=120, input_data=None):
    try:
        proc = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=timeout, input=input_data)
        result = proc.stdout.decode(errors='replace')
    except subprocess.TimeoutExpired as error:
        result = (error.stdout or b'').decode(errors='replace')
        (OUT / (name + '.txt')).write_text(result + '\nTIMEOUT\n')
        raise RuntimeError(name + ': timeout') from error
    (OUT / (name + '.txt')).write_text('$ ' + ' '.join(map(str, args)) + '\n' + result +
                                     f'\nEXIT_CODE={proc.returncode}\n')
    if proc.returncode != 0:
        raise RuntimeError(f'{name}: exit {proc.returncode}')
    return result


def record(name, **details):
    summary['checks'].append({'name': name, 'status': 'PASS', **details})
    save()
    print('PASS:', name, flush=True)


GDB_HELPERS = '''python
import gdb, struct
inf = gdb.selected_inferior()
def addr(name):
    return int(gdb.parse_and_eval('(unsigned int)&' + name))
def word(where):
    return struct.unpack('<I', bytes(inf.read_memory(where, 4)))[0]
def require(ok, message):
    if not ok:
        raise gdb.GdbError(message)
end
'''


def qemu_case(name, elf, expected, body=None):
    serial = OUT / (name + '-uart.log')
    diagnostics = OUT / (name + '-qemu.log')
    args = ['qemu-system-arm', '-M', 'mps2-an500', '-cpu', 'cortex-m7',
            '-display', 'none', '-monitor', 'none', '-serial', 'file:' + str(serial),
            '-d', 'guest_errors', '-kernel', str(elf)]
    if body is not None:
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        args += ['-S', '-gdb', f'tcp:127.0.0.1:{port}']
        script = OUT / (name + '.gdb')
        script.write_text('set pagination off\nset confirm off\nset remotetimeout 5\n' +
                          f'target remote 127.0.0.1:{port}\n' + GDB_HELPERS + body +
                          f'\npython print("CHECK_PASS:{name}")\ndetach\nquit\n')
    (OUT / (name + '-command.txt')).write_text(json.dumps(args, ensure_ascii=False) + '\n')
    with diagnostics.open('wb') as stream:
        qemu = subprocess.Popen(args, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
        try:
            if body is not None:
                time.sleep(0.3)
                if qemu.poll() is not None:
                    raise RuntimeError(name + ': QEMU failed to start')
                gout = run(['arm-none-eabi-gdb', '--batch', '-q', str(elf), '-x', str(script)],
                           name + '-gdb', timeout=30)
                if 'CHECK_PASS:' + name not in gout:
                    raise RuntimeError(name + ': missing GDB assertions')
            deadline = time.monotonic() + 5
            while True:
                data = serial.read_bytes() if serial.exists() else b''
                if qemu.poll() is not None:
                    raise RuntimeError(name + ': QEMU exited unexpectedly')
                if len(data) >= len(expected):
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError(name + ': missing UART output before deadline')
                time.sleep(0.02)
            # Leave time to detect unexpected trailing output, then stop the
            # intentional idle loop. Negative cases also assert state via GDB.
            time.sleep(0.1)
        finally:
            if qemu.poll() is None:
                qemu.terminate()
                try:
                    qemu.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    qemu.kill()
                    qemu.wait(timeout=3)
    data = serial.read_bytes()
    if data != expected:
        raise RuntimeError(f'{name}: UART bytes differ; got {data!r}')
    diag = diagnostics.read_text()
    if any(line and 'terminating on signal' not in line for line in diag.splitlines()):
        raise RuntimeError(name + ': unexpected QEMU diagnostics; see log')
    record(name, uart_bytes=len(data), qemu_exit=qemu.returncode,
           evidence=[p.name for p in OUT.glob(name + '*')])


POISON = '''python
require(int(gdb.parse_and_eval('$sp')) == addr('_stack_top'), 'reset MSP')
require(int(gdb.parse_and_eval('$pc')) == (addr('Reset_Handler') & ~1), 'reset PC')
require(word(0) == addr('_stack_top'), 'vector MSP')
require(word(4) == (addr('Reset_Handler') | 1), 'vector entry Thumb bit')
reserved = {7,8,9,10,13}
for i in range(2,48):
    require(word(i*4) == (0 if i in reserved else addr('Default_Handler') | 1), 'vector %d' % i)
ds,de,bs,be = [addr(s) for s in ('_sdata','_edata','_sbss','_ebss')]
require(ds < de <= bs < be < addr('_stack_bottom'), 'section boundaries')
expected = bytes(inf.read_memory(addr('_sidata'), de-ds))
inf.write_memory(ds,b'\\xcc'*(de-ds))
inf.write_memory(bs,b'\\xa5'*(be-bs))
inf.write_memory(be,b'\\x78\\x56\\x34\\x12')
print('RESET: MSP=%#x PC=%#x VECTOR=%#x; data=%d BSS=%d; vectors=48' % (word(0),int(gdb.parse_and_eval('$pc')),word(4),de-ds,be-bs))
require(bytes(inf.read_memory(ds,de-ds)) == b'\\xcc'*(de-ds), 'data injection')
require(bytes(inf.read_memory(bs,be-bs)) == b'\\xa5'*(be-bs), 'BSS injection')
end
break *main
continue
python
require(bytes(inf.read_memory(ds,de-ds)) == expected, 'data copy')
require(bytes(inf.read_memory(bs,be-bs)) == bytes(be-bs), 'BSS clear')
require(word(be) == 0x12345678, 'upper sentinel')
require(int(gdb.parse_and_eval('$sp')) % 8 == 0, 'C-entry stack alignment')
print('DATA_RESTORED=' + expected.hex())
print('BSS_ZERO_BYTES=%d UPPER_SENTINEL=%#x' % (be-bs,word(be)))
end
delete breakpoints
break boot_idle
continue
python
require(int(gdb.parse_and_eval('boot_status')) == 1, 'success state')
require(word(0xE000ED08) == addr('vectors'), 'VTOR')
require(word(0x40004008) == 1, 'UART TX only')
require(word(0x40004010) == 217, 'UART divisor')
require(word(0x40004004) & 5 == 0, 'UART drained, no TX overrun')
print('BOOT_STATUS=1 VTOR=%#x UART_CTRL=%#x BAUDDIV=%d' % (word(0xE000ED08),word(0x40004008),word(0x40004010)))
end
'''


def main():
    OUT.mkdir(parents=True, exist_ok=False)
    save()
    try:
        for tool in ['qemu-system-arm', 'arm-none-eabi-gcc', 'arm-none-eabi-gdb', 'cmake', 'make', 'git']:
            run([tool, '--version'], tool + '-version')
        tree = run(['qemu-system-arm', '-M', 'mps2-an500', '-display', 'none',
                    '-serial', 'none', '-monitor', 'stdio', '-S'], 'model-clock',
                   timeout=10, input_data=b'info qtree\nquit\n')
        if 'clock-in "cpuclk" freq_hz=25 MHz' not in tree or tree.count('pclk-frq = 25000000') != 5:
            raise RuntimeError('Installed model clock differs from board assumptions')
        record('model-clock', evidence=['model-clock.txt'])
        (ROOT / 'build').mkdir(exist_ok=True)
        build = Path(tempfile.mkdtemp(prefix='week2-', dir=ROOT / 'build'))
        run(['cmake', '-S', str(ROOT), '-B', str(build), '-G', 'Unix Makefiles',
             '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT/'cmake/arm-none-eabi.cmake')], 'configure')
        run(['cmake', '--build', str(build), '--parallel'], 'build')
        elf = build / 'bootloader.elf'
        summary['elf'] = str(elf.relative_to(ROOT))
        summary['elf_sha256'] = hashlib.sha256(elf.read_bytes()).hexdigest()
        paths = list((ROOT/'src/week2').glob('*')) + [ROOT/'CMakeLists.txt', ROOT/'cmake/arm-none-eabi.cmake', Path(__file__).resolve()]
        summary['source_sha256'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
        run(['arm-none-eabi-size', str(elf)], 'size')
        run(['arm-none-eabi-size', '-A', str(elf)], 'sections')
        run(['arm-none-eabi-objdump', '-s', '-j', '.isr_vector', str(elf)], 'vectors')
        disassembly = run(['arm-none-eabi-objdump', '-d', str(elf)], 'disassembly')
        if 'bkpt' in disassembly:
            raise RuntimeError('Unexpected BKPT instruction in UART-only firmware')
        run(['arm-none-eabi-nm', '-n', str(elf)], 'symbols')
        record('clean-build-no-semihosting', evidence=['build.txt','size.txt','sections.txt','vectors.txt','disassembly.txt','symbols.txt'])
        # A separate retained target protects the first week's runnable example.
        old = run(['qemu-system-arm', '-M', 'mps2-an500', '-cpu', 'cortex-m7',
                   '-display', 'none', '-monitor', 'none', '-serial', 'none',
                   '-semihosting-config', 'enable=on,target=native', '-kernel', str(build/'demo.elf')],
                  'week1-regression', timeout=10)
        if 'PASS: Cortex-M7; .data initialized; .bss zero;' not in old:
            raise RuntimeError('Week-1 regression failed')
        record('week1-regression', evidence=['week1-regression.txt'])
        for n in range(1,4):
            qemu_case('uart-repeat-' + str(n), elf, GOOD)
        qemu_case('poisoned-startup-and-registers', elf, GOOD, POISON)
        parameters = '''break *main
continue
python
require(int(gdb.parse_and_eval('uart_putc(65)')) == -1, 'disabled TX must reject writes')
require(int(gdb.parse_and_eval('uart_init(25000000,115200)')) == 0, 'valid configuration')
require(word(0x40004010) == 217, 'rounded baud divisor')
for clk,baud in [(0,115200),(25000000,0),(25000000,2000000),(25000000,1)]:
    before = (word(0x40004008),word(0x40004010))
    require(int(gdb.parse_and_eval('uart_init(%d,%d)' % (clk,baud))) == -1, 'invalid baud accepted')
    require((word(0x40004008),word(0x40004010)) == before, 'invalid init changed registers')
    print('REJECTED: clock=%d baud=%d; registers unchanged' % (clk,baud))
require(int(gdb.parse_and_eval('uart_write((const char *)0)')) == -1, 'NULL string accepted')
print('VALID: divisor=217; DISABLED_TX/NULL rejected')
end
delete breakpoints
break boot_idle
continue
python
require(int(gdb.parse_and_eval('boot_status')) == 1, 'normal boot after parameter tests')
end
'''
        qemu_case('uart-parameter-boundaries', elf, GOOD, parameters)
        for name, variable in [('corrupt-data','runtime_data[4]'), ('corrupt-bss','runtime_bss[6]')]:
            body = f'''break *main
continue
set variable {variable} = 0xdeadbeef
python
require(int(gdb.parse_and_eval('{variable}')) == 0xdeadbeef, 'injection failed')
print('INJECTED: {variable}=0xdeadbeef')
end
delete breakpoints
break boot_idle
continue
python
require(int(gdb.parse_and_eval('boot_status')) == 2, 'must reject corrupt runtime')
print('BOOT_STATUS=2 (expected failure)')
end
'''
            qemu_case(name, elf, BAD, body)
        disabled = '''break uart_write
continue
set {unsigned int}0x40004008 = 0
delete breakpoints
break boot_idle
continue
python
require(int(gdb.parse_and_eval('boot_status')) == 3, 'disabled UART not detected')
print('BOOT_STATUS=3 (expected UART failure)')
end
'''
        qemu_case('disabled-uart', elf, b'', disabled)
        fault = '''break *main
continue
python
# UDF #0 forces a synchronous undefined-instruction exception. UsageFault
# is disabled after reset, so this must escalate to HardFault (IPSR=3).
inf.write_memory(addr('main') & ~1, b'\\x00\\xde')
print('INJECTED: UDF #0 at main')
end
delete breakpoints
break fault_idle
continue
python
require(int(gdb.parse_and_eval('exception_number')) == 3, 'HardFault not recorded')
require(int(gdb.parse_and_eval('$xpsr')) & 0x1ff == 3, 'not in HardFault context')
print('EXCEPTION_NUMBER=3 (HardFault); xPSR=%#x' % int(gdb.parse_and_eval('$xpsr')))
end
'''
        qemu_case('undefined-instruction', elf, b'', fault)
        summary['status'] = 'PASS'
    except Exception as error:
        summary['status'] = 'FAIL'
        summary['error'] = str(error)
        print('FAIL:', error, file=sys.stderr, flush=True)
    finally:
        summary['finished_at'] = NOW()
        save()
        (ROOT/'logs/week2-latest.txt').write_text(OUT.name + '\n')
        print('Evidence:', OUT, flush=True)
    return 0 if summary['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
