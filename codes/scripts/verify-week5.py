#!/usr/bin/env python3
"""Week-5 code acceptance: native sanitizers, independent YMODEM sender,
actual UART upgrades, persisted A/B boot, failures and controlled NOR tears.
Builds fresh targets in a temporary directory; keeps evidence under logs/.
"""
from datetime import datetime
from zoneinfo import ZoneInfo
import hashlib
import json
import os
from pathlib import Path
import random
import socket
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from week5_package import (FLASH_SIZE, HEADER_SIZE, MAX_PAYLOAD, SLOT_OFFSETS,
                           SLOT_SIZE, build_package, flash_seed, parse_package)
from week5_session import PeerCancelled

ROOT = Path(__file__).resolve().parents[1]
os.environ['PATH'] = '/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:' + os.environ.get('PATH', '')
STAMP = datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S-%f')
OUT = ROOT / 'logs' / ('week5-' + STAMP)
A, B = SLOT_OFFSETS['a'], SLOT_OFFSETS['b']
summary = {'status': 'RUNNING', 'scope': 'week5-code-complete', 'started_at': STAMP,
           'checks': [], 'boot_cases': [], 'interop': [], 'upgrades': []}
VENDOR = ROOT / 'tests/third_party'


def save():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


def record(name):
    summary['checks'].append(name)
    save()
    print('PASS:', name, flush=True)


def run(args, name, timeout=120):
    try:
        process = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        (OUT / (name + '.txt')).write_bytes((error.stdout or b'') + b'\nTIMEOUT\n')
        raise
    text = process.stdout.decode(errors='replace')
    (OUT / (name + '.txt')).write_text(text)
    if process.returncode:
        raise RuntimeError(f'{name}: exit {process.returncode}\n{text[-2000:]}')
    return text


def run_fail(args, name, code=2):
    process = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=120)
    text = process.stdout.decode(errors='replace')
    (OUT / (name + '.txt')).write_text(text)
    if process.returncode != code:
        raise RuntimeError(f'{name}: expected exit {code}, got {process.returncode}\n{text[-2000:]}')
    return text


def symbols(elf, name):
    result = {}
    for line in run(['arm-none-eabi-nm', '-n', str(elf)], name).splitlines():
        fields = line.split()
        if len(fields) == 3:
            result[fields[2]] = int(fields[0], 16)
    return result


class SocketLink:
    """Minimal byte channel handed to the third-party YMODEM sender."""

    def __init__(self, sock):
        self.sock = sock
        self.stopped = False

    def read(self, size, timeout=1):
        if self.stopped:
            raise PeerCancelled()
        self.sock.settimeout(timeout)
        try:
            return self.sock.recv(size)
        except socket.timeout:
            return b''

    def write(self, data, timeout=1):
        if self.stopped:
            raise PeerCancelled()
        try:
            self.sock.sendall(bytes(data))
            return len(data)
        except OSError:
            return None


def vendor_sender():
    if str(VENDOR) not in sys.path:
        sys.path.insert(0, str(VENDOR))
    import logging
    logging.disable(logging.CRITICAL)
    from ymodem.Protocol import ProtocolType
    from ymodem.Socket import ModemSocket
    return ProtocolType, ModemSocket


def crc16_cross_check(build, host):
    if str(VENDOR) not in sys.path:
        sys.path.insert(0, str(VENDOR))
    from ymodem.CRC import calc_crc16
    rng = random.Random(20261031)
    sizes = (0, 9, 1, 17, 128, 1024, 4096)
    cases = [b'123456789', b'A', bytes(range(256))]
    cases += [bytes(rng.randrange(256) for _ in range(size)) for size in sizes]
    for index, data in enumerate(cases):
        path = build / f'crc16-{index}.bin'
        path.write_bytes(data)
        with path.open('rb') as stream:
            process = subprocess.run([str(host), 'crc16'], stdin=stream,
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE)
        assert process.returncode == 0, process.stderr
        assert int(process.stdout.strip(), 16) == calc_crc16(data), index
    summary['crc16_cases'] = len(cases)
    record(f'CRC16 matches independent ymodem 1.5.3 implementation on {len(cases)} inputs')


class FaultLink(SocketLink):
    """Socket link between the independent sender and the C receiver that can
    drop one ACK, corrupt one data frame or stall the data stream."""

    def __init__(self, sock):
        super().__init__(sock)
        self.drop_after = None       # 'block0' | 'data' | 'eot'
        self.drop_pending = False
        self.drop_done = False
        self.corrupt_next_data = False
        self.corrupted = 0
        self.stall_data = False
        self.stalled = 0
        self.terminator_delay_ms = 0
        self.terminator_scheduled = False
        self.timers = []

    def read(self, size, timeout=1):
        data = super().read(size, timeout)
        if data and self.drop_pending and data == b'\x06':
            self.drop_pending = False
            self.drop_done = True
            return b''
        return data

    def write(self, data, timeout=1):
        data = bytes(data)
        head = data[:1]
        if head in (b'\x01', b'\x02') and len(data) > 11:
            seq = data[1]
            if self.stall_data and seq != 0:
                self.stalled += 1
                return len(data)  # dropped by the faulty link
            if self.corrupt_next_data and seq != 0:
                frame = bytearray(data)
                frame[3] ^= 0x01  # corrupt the first payload byte
                data = bytes(frame)
                self.corrupt_next_data = False
                self.corrupted += 1
            if self.drop_after == 'block0' and seq == 0 and not self.drop_done:
                self.drop_pending = True
            elif self.drop_after == 'data' and seq == 1 and not self.drop_done:
                self.drop_pending = True
            elif (self.terminator_delay_ms and seq == 0 and
                  not self.terminator_scheduled and data[3:11] == b'\x00' * 8):
                self.terminator_scheduled = True
                timer = threading.Timer(self.terminator_delay_ms / 1000.0,
                                        self._late_terminator, args=(data,))
                timer.daemon = True
                timer.start()
                self.timers.append(timer)
        elif head == b'\x04' and self.drop_after == 'eot' and not self.drop_done:
            self.drop_pending = True
        return super().write(data, timeout)

    def _late_terminator(self, data):
        try:
            self.sock.sendall(data)
        except OSError:
            pass


def interop_transfer(build, host, name, packet_size, payload, fault=None, *,
                     timeout_ms=300, retries=8, no_progress=30000,
                     expect_ok=True, max_seconds=5):
    ProtocolType, ModemSocket = vendor_sender()
    source = build / f'{name}-source.bin'
    source.write_bytes(payload)
    output = build / f'{name}-received.bin'
    log_path = OUT / f'{name}.stderr'
    a, b = socket.socketpair()
    if fault is None:
        link = SocketLink(a)
    else:
        link = FaultLink(a)
        link.drop_after = fault.get('drop_after')
        link.corrupt_next_data = fault.get('corrupt_next_data', False)
        link.stall_data = fault.get('stall_data', False)
        link.terminator_delay_ms = fault.get('terminator_delay_ms', 0)
    started = time.monotonic()
    with log_path.open('wb') as err:
        process = subprocess.Popen(
            [str(host), 'receive', '--output', str(output),
             '--max-size', '65536', '--timeout', str(timeout_ms),
             '--retries', str(retries),
             '--no-progress-timeout', str(no_progress)],
            stdin=b, stdout=b, stderr=err)
        b.close()
        result = {}

        def sender_job():
            try:
                modem = ModemSocket(link.read, link.write,
                                    protocol_type=ProtocolType.YMODEM,
                                    packet_size=packet_size,
                                    style_id='UNIX_RZ_SZ')
                result['sent'] = modem.send([str(source)])
            except PeerCancelled:
                result['cancelled'] = True
            except Exception as error:  # surfaced by the assertions below
                result['error'] = error

        worker = threading.Thread(target=sender_job, daemon=True)
        worker.start()
        try:
            code = process.wait(timeout=120)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise
        elapsed = time.monotonic() - started
        if not expect_ok:
            link.stopped = True
            try:
                a.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass  # receiver may already have closed its socket
        worker.join(timeout=3)
        assert not worker.is_alive(), f'{name}: sender did not stop'
        leftover = b''
        if fault and fault.get('terminator_delay_ms'):
            a.settimeout(0.2)
            for _ in range(20):
                try:
                    chunk = a.recv(256)
                except socket.timeout:
                    continue
                if not chunk:
                    break
                leftover += chunk
                if leftover.count(b'\x06') >= 2:
                    break
        a.close()
    lines = [line for line in log_path.read_text().splitlines() if line.strip()]
    info = json.loads(lines[-1])
    entry = {'name': name, 'packet_size': packet_size, 'bytes': len(payload),
             'fault': fault or {}, 'sender': 'ymodem 1.5.3 (MIT)',
             'receiver': info, 'seconds': round(elapsed, 3)}
    summary['interop'].append(entry)
    save()
    if expect_ok:
        assert result.get('sent') is True, f'{name}: sender {result}'
        assert code == 0, f'{name}: receiver exit {code}'
        assert output.read_bytes() == payload, f'{name}: payload mismatch'
        assert info['result'] == 'OK', info
        assert info['received'] == len(payload), info
        if fault and fault.get('terminator_delay_ms'):
            entry['late_acks'] = leftover.count(b'\x06')
            assert entry['late_acks'] >= 2, (name, leftover)
            assert info['blocks'] >= 1, info
        record(f'YMODEM interop {name}: independent sender, {packet_size}B frames')
    else:
        assert code != 0, f'{name}: expected receiver failure'
        assert info['result'] in ('TIMEOUT', 'RETRY_LIMIT', 'IO'), info
        assert elapsed < max_seconds, (name, elapsed)
        record(f'YMODEM stalled link {name}: bounded exit in {elapsed:.2f}s')
    return entry



def main():
    OUT.mkdir(parents=True)
    save()
    try:
        with tempfile.TemporaryDirectory(prefix='stage5-') as temp:
            build = Path(temp)
            for tool in ['qemu-system-arm', 'arm-none-eabi-gcc', 'cmake', 'cc']:
                run([tool, '--version'], tool + '-version')
            run(['cmake', '-S', str(ROOT), '-B', str(build), '-G', 'Unix Makefiles',
                 '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'cmake/arm-none-eabi.cmake')],
                'configure')
            run(['cmake', '--build', str(build), '--target', 'bootloader-week5.elf',
                 'bootloader-week5-upgrade.elf', 'bootloader-week5-fault.elf',
                 'application-v1.elf', 'application-v2.elf', 'application-v3.elf', '--parallel'], 'build')
            boot = build / 'bootloader-week5.elf'
            apps = {version: build / f'application-{version}.elf'
                    for version in ('v1', 'v2', 'v3')}
            for version, app in apps.items():
                run(['arm-none-eabi-objcopy', '-O', 'binary', str(app),
                     str(build / f'application-{version}.bin')], 'objcopy-' + version)
            summary['elf_sha256'] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                                     for path in (boot, build / 'bootloader-week5-fault.elf',
                                                  build / 'bootloader-week5-upgrade.elf', *apps.values())}
            run(['arm-none-eabi-size', str(boot), *map(str, apps.values())], 'size')
            boot_symbols = symbols(boot, 'boot-symbols')
            assert boot_symbols['_stack_top'] == 0x20010000
            for version, app in apps.items():
                app_symbols = symbols(app, version + '-symbols')
                assert app_symbols['app_vectors'] == 0x10000
                assert app_symbols['_stack_top'] == 0x20200000
            record('fresh Week-5 build; boot stack below APP_RAM_BASE; app linked at 0x10000')

            payloads, packages = {}, {}
            for version in (1, 2, 3):
                name = f'v{version}'
                payload = (build / f'application-{name}.bin').read_bytes()
                assert 192 <= len(payload) < FLASH_SIZE
                package = build_package(payload, version)
                assert len(package) == HEADER_SIZE + len(payload)
                parsed = parse_package(package)
                assert parsed.version == version and parsed.payload == payload
                payloads[name], packages[version] = payload, package
                (build / f'application-{name}.pkg').write_bytes(package)
            assert payloads['v1'] != payloads['v2'], 'v1/v2 payloads must differ'
            assert len(packages[1]) != FLASH_SIZE and len(packages[2]) != FLASH_SIZE
            summary['payload_bytes'] = {name: len(data) for name, data in payloads.items()}
            summary['package_bytes'] = {name: len(packages[v])
                                        for name, v in (('v1', 1), ('v2', 2))}
            record('v1/v2 payloads differ; packages are exactly header + payload')

            run([sys.executable, str(ROOT / 'scripts/test_week5_package.py')],
                'package-unit-tests')
            record('package framing, CRC, vector, capacity and seed boundaries')

            elf_package = build / 'elf-rejected.pkg'
            run_fail([sys.executable, str(ROOT / 'scripts/pack-week5.py'), '--app',
                      str(apps['v1']), '--output', str(elf_package)], 'packer-rejects-elf')
            assert not elf_package.exists()
            flash_dump = build / 'flash-dump.bin'
            flash_dump.write_bytes(b'\xff' * FLASH_SIZE)
            run_fail([sys.executable, str(ROOT / 'scripts/pack-week5.py'), '--app',
                      str(flash_dump), '--output', str(build / 'flash.pkg')],
                     'packer-rejects-flash-dump')
            record('packer rejects ELF input and a 1 MiB flash dump')

            native = build / 'test_image_slot'
            run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-g',
                 '-fsanitize=address,undefined', '-Isrc/week4', '-Isrc/week3',
                 'src/week4/image.c', 'src/week4/crc32.c', 'src/week3/flash.c',
                 'tests/test_image_slot.c', '-o', str(native)], 'native-slot-build')
            run([str(native)], 'native-slot-tests')
            record('native slot API: A/B, illegal slots, bad CRC/length/vector, wrappers')

            native_meta = build / 'test_boot_meta'
            run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-g',
                 '-fsanitize=address,undefined', '-Isrc/week5', '-Isrc/week4',
                 '-Isrc/week3', 'src/week5/boot_meta.c', 'src/week4/crc32.c',
                 'src/week3/flash.c', 'tests/test_boot_meta.c',
                 '-o', str(native_meta)], 'native-meta-build')
            run([str(native_meta)], 'native-meta-tests')
            record('native boot_meta: transactions, damage, IO states, torn writes, wrap')

            host = build / 'ymodem_host'
            run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-O2',
                 '-Isrc/week5', 'src/week5/ymodem.c', 'src/week5/crc16.c',
                 'tests/ymodem_host.c', '-o', str(host)], 'ymodem-host-build')
            native_ymodem = build / 'test_ymodem'
            run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-g',
                 '-fsanitize=address,undefined', '-Isrc/week5',
                 'src/week5/ymodem.c', 'src/week5/crc16.c',
                 'tests/test_ymodem.c', '-o', str(native_ymodem)],
                'native-ymodem-build')
            run([str(native_ymodem)], 'native-ymodem-tests')
            record('native YMODEM: frames, block0 bounds, CRC/retry, duplicates, '
                   'cancel, timeouts, sink errors')
            native_update = build / 'test_update'
            run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-g',
                 '-fsanitize=address,undefined', '-Isrc/week5', '-Isrc/week4',
                 '-Isrc/week3', 'src/week5/update.c', 'src/week5/boot_meta.c',
                 'src/week5/ymodem.c', 'src/week5/crc16.c', 'src/week4/image.c',
                 'src/week4/crc32.c', 'src/week3/flash.c', 'tests/test_update.c',
                 '-o', str(native_update)], 'native-update-build')
            run([str(native_update)], 'native-update-tests')
            record('native update: RECEIVING/erase/write/READY/failure and YMODEM end-to-end')
            duplicate_bound = build / 'test_duplicate_bound'
            run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-g',
                 '-Isrc/week5', 'tests/test_ymodem_duplicate_bound.c',
                 'src/week5/ymodem.c', 'src/week5/crc16.c',
                 '-o', str(duplicate_bound)], 'duplicate-bound-build')
            run([str(duplicate_bound)], 'duplicate-bound-test')
            record('review reproduction: repeated frame hits RETRY_LIMIT, reads bounded')
            crc16_cross_check(build, host)
            for relative in ('tests/third_party/README.md',
                             'tests/third_party/LICENSE.ymodem',
                             'tests/third_party/LICENSE.ordered-set',
                             'tests/third_party/ymodem/Socket.py',
                             'tests/third_party/ordered_set/__init__.py'):
                assert (ROOT / relative).is_file(), relative
            interop_payload = bytes((index * 37 + 11) & 0xFF for index in range(3001))
            small_payload = interop_payload[:1500]
            interop_transfer(build, host, 'interop-1024', 1024, interop_payload)
            interop_transfer(build, host, 'interop-128', 128, interop_payload)
            interop_transfer(build, host, 'interop-block0-ack-loss', 128,
                             small_payload, {'drop_after': 'block0'})
            interop_transfer(build, host, 'interop-data-ack-loss', 128,
                             small_payload, {'drop_after': 'data'})
            interop_transfer(build, host, 'interop-eot-ack-loss', 128,
                             small_payload, {'drop_after': 'eot'})
            interop_transfer(build, host, 'interop-bad-crc', 128,
                             small_payload, {'corrupt_next_data': True})
            wrap_payload = bytes((index * 29 + 7) & 0xFF for index in range(34000))
            interop_transfer(build, host, 'interop-wrap-128', 128, wrap_payload)
            interop_transfer(build, host, 'interop-delayed-terminator', 128,
                             small_payload, {'terminator_delay_ms': 700})
            interop_transfer(build, host, 'interop-stalled-link', 128,
                             small_payload, {'stall_data': True},
                             timeout_ms=200, retries=20, no_progress=600,
                             expect_ok=False, max_seconds=4)
            record('vendored sender provenance and licences present')
            from week5_acceptance import acceptance
            acceptance(build, OUT, packages, summary, record)

            inspector = [sys.executable, str(ROOT / 'scripts/week5_meta.py')]
            failed = json.loads(run([*inspector, str(build / 'bad-vector-tail-halfword.bin')],
                                    'metadata-inspect-failed'))
            assert failed['active'] == 1 and failed['state_name'] == 'FAILED' and failed['error'] == 4
            run_fail([*inspector, str(build / 'ambiguous-metadata.bin')],
                     'metadata-inspect-ambiguous')
            record('metadata inspection CLI reports persisted failure and rejects ambiguous records')

            for path in OUT.glob('*/qemu.log'):
                unexpected = [line for line in path.read_text().splitlines()
                              if line and 'terminating on signal' not in line]
                assert not unexpected, (path, unexpected)
            record('no QEMU guest errors in any boot case')
            summary['status'] = 'PASS'
    except Exception as error:
        summary['status'] = 'FAIL'
        summary['error'] = repr(error)
        raise
    finally:
        save()
        (ROOT / 'logs/week5-latest.txt').write_text(str(OUT) + '\n')
        print('Evidence:', OUT, flush=True)


if __name__ == '__main__':
    main()
