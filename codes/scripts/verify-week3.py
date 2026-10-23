#!/usr/bin/env python3
"""Fresh build, native sanitizers, real QEMU RX IRQ and checkpoint tests."""
from datetime import datetime
from zoneinfo import ZoneInfo
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
from unittest.mock import patch
from week3_session import Session, FLASH_SIZE

ROOT = Path(__file__).resolve().parents[1]
os.environ['PATH'] = '/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:' + os.environ.get('PATH', '')
STAMP = datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S-%f')
OUT = ROOT / 'logs' / ('week3-' + STAMP)
summary = {'status': 'RUNNING', 'started_at': STAMP, 'checks': [],
           'storage_model': 'SRAM NOR rules + explicit host checkpoint; not power-loss-safe Flash'}


def save():
    (OUT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


def record(name):
    summary['checks'].append(name)
    save()
    print('PASS:', name, flush=True)


def run(args, name, timeout=120):
    result = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=timeout)
    text = result.stdout.decode(errors='replace')
    (OUT / (name + '.txt')).write_text(text)
    if result.returncode:
        raise RuntimeError(f'{name}: exit {result.returncode}; see {OUT / (name + ".txt")}')
    return text


def gdb(elf, port, name, body):
    script = OUT / (name + '.gdb')
    script.write_text('set pagination off\nset confirm off\n' +
                      f'target remote 127.0.0.1:{port}\n' + body +
                      '\npython print("GDB_ASSERTIONS_PASS")\ndetach\nquit\n')
    output = run(['arm-none-eabi-gdb', '--batch', '-q', str(elf), '-x', str(script)], name, 30)
    assert 'GDB_ASSERTIONS_PASS' in output


def main():
    OUT.mkdir(parents=True)
    save()
    try:
        with tempfile.TemporaryDirectory(prefix='week3-build-') as build_dir:
            build = Path(build_dir)
            for tool in ['qemu-system-arm', 'arm-none-eabi-gcc', 'arm-none-eabi-gdb', 'cmake', 'cc']:
                run([tool, '--version'], tool + '-version')
            run(['cmake', '-S', str(ROOT), '-B', str(build), '-G', 'Unix Makefiles',
                 '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'cmake/arm-none-eabi.cmake')], 'configure')
            run(['cmake', '--build', str(build), '--parallel'], 'build')
            production = build / 'bootloader-week3.elf'
            test_elf = build / 'week3-test.elf'
            summary['elf_sha256'] = hashlib.sha256(production.read_bytes()).hexdigest()
            run(['arm-none-eabi-size', str(production), str(test_elf)], 'size')
            dis = run(['arm-none-eabi-objdump', '-d', str(production)], 'disassembly')
            assert 'bkpt' not in dis.lower()
            record('fresh cross-build; no semihosting BKPT')
            native = build / 'test_flash'
            run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-g',
                 '-fsanitize=address,undefined', '-DFLASH_NATIVE_TEST', '-Isrc/week3',
                 'src/week3/flash.c', 'tests/test_flash.c', '-o', str(native)], 'native-build')
            run([str(native)], 'native-flash')
            record('native flash API and boundary tests with ASan/UBSan')

            with socket.socket() as sock:
                sock.bind(('127.0.0.1', 0))
                port = sock.getsockname()[1]
            with Session(test_elf, build / 'test.bin', OUT / 'irq', port) as vm:
                assert vm.banner.startswith(b'SELFTEST PASS\r\nBOOT: week3')
                record('same flash assertions execute on Cortex-M7')
                gdb(test_elf, port, 'irq-setup', '''python
import gdb, struct
inf = gdb.selected_inferior()
def word(addr): return struct.unpack('<I', bytes(inf.read_memory(addr,4)))[0]
assert word(0x40004008) == 0xb
assert word(0x40004010) == 217
assert word(0xe000e100) & 1
assert word(64) == (int(gdb.parse_and_eval('(unsigned int)&UART0_RX_Handler')) | 1)
assert word(0) == 0x20300000
assert int(gdb.parse_and_eval('boot_status')) == 1
assert int(gdb.parse_and_eval('uart_rx_count')) == 0
end
set var test_pause_rx = 1
''')
                vm.quiescent = False
                vm.serial.sendall(bytes(range(200)))
                time.sleep(0.5)
                gdb(test_elf, port, 'irq-overflow', '''python
import gdb
inf = gdb.selected_inferior()
assert int(gdb.parse_and_eval('uart_rx_count')) == 200
assert int(gdb.parse_and_eval('uart_rx_dropped')) == 73
assert int(gdb.parse_and_eval('uart_rx_overruns')) == 0
assert bytes(inf.read_memory(int(gdb.parse_and_eval('(unsigned int)&rx')), 127)) == bytes(range(127))
end
set var tail = head
set var test_pause_rx = 0
''')
                vm.quiescent = True  # Debugger discarded the intentionally held queue.
                assert vm.command(b'p') == b'PONG\r\n> '
                record('real UART RX IRQ, binary byte order, 127-byte queue, drop-new overflow and recovery')
                for _ in range(300):
                    assert vm.command(b'p') == b'PONG\r\n> '
                assert vm.command(b'?') == b'ERROR command\r\n> '
                stats = vm.command(b's')
                assert b'DROP 0x00000049\r\n' in stats and b'OVERRUN 0x00000000\r\n' in stats
                record('300 serial command responses across ring wraps; unknown-command rejection')

            image = build / 'persistent.bin'
            with Session(production, image, OUT / 'write') as vm:
                assert vm.command(b'r') == b'PARAM 0xffffffff\r\n> '
                assert vm.command(b'w') == b'OK\r\n> '
                assert vm.command(b'r') == b'PARAM 0x4b4f3357\r\n> '
                vm.checkpoint()
                expected = bytearray(b'\xff' * FLASH_SIZE)
                expected[0xf0000:0xf0004] = b'W3OK'
                assert image.read_bytes() == expected
                try:
                    with Session(production, image):
                        raise AssertionError('second writer must be refused')
                except RuntimeError as error:
                    assert 'already in use' in str(error)
                record('exact 1 MiB checkpoint and exclusive image lock')
                assert vm.command(b'e') == b'OK\r\n> '
                with patch('week3_session.os.replace', side_effect=OSError('injected replace failure')):
                    try:
                        vm.checkpoint()
                        raise AssertionError('replacement should fail')
                    except OSError as error:
                        assert 'injected' in str(error)
                assert image.read_bytes() == expected
                assert vm.command(b'p') == b'PONG\r\n> '
                record('failed checkpoint preserves old image and resumes QEMU')
                vm.process.kill()
                vm.process.wait(timeout=3)
            with Session(production, image, OUT / 'restart') as vm:
                assert vm.command(b'r') == b'PARAM 0x4b4f3357\r\n> '
                record('new QEMU process restores checkpoint; unsaved erase lost after SIGKILL')
                assert vm.command(b'e') == b'OK\r\n> '
                vm.checkpoint()
            with Session(production, image, OUT / 'erased-restart') as vm:
                assert vm.command(b'r') == b'PARAM 0xffffffff\r\n> '
                assert image.read_bytes() == b'\xff' * FLASH_SIZE
                record('erasure persists after explicit checkpoint and restart')
            bad = build / 'invalid.bin'
            bad.write_bytes(b'bad')
            try:
                with Session(production, bad):
                    raise AssertionError('invalid image must fail')
            except ValueError as error:
                assert 'exactly' in str(error)
            assert bad.read_bytes() == b'bad'
            record('wrong-size image rejected without modification')
            for log in OUT.glob('*/qemu.log'):
                unexpected = [line for line in log.read_text().splitlines()
                              if line and 'terminating on signal' not in line]
                assert not unexpected, (log, unexpected)
            record('no QEMU guest-error diagnostics')
        summary['status'] = 'PASS'
    except Exception as error:
        summary['status'] = 'FAIL'
        summary['error'] = repr(error)
        raise
    finally:
        save()
        (ROOT / 'logs/week3-latest.txt').write_text(str(OUT) + '\n')
        print('Evidence:', OUT, flush=True)


if __name__ == '__main__':
    main()
