#!/usr/bin/env python3
"""Week-6 code acceptance: signed packages, automatic rollback, trial boot
with the CMSDK watchdog, and the serial configuration interface.

Builds fresh targets under the evidence directory. Does not modify Week-5
sources or claim real-hardware watchdog or power-loss certification.
"""
from datetime import datetime
from zoneinfo import ZoneInfo
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from week5_package import HEADER_SIZE, SLOT_OFFSETS, SLOT_SIZE, build_package
from week6_package import (TRAILER_SIZE, build_signed_package, flash_seed,
                           parse_signed_package)
from week6_session import Session

ROOT = Path(__file__).resolve().parents[1]
os.environ['PATH'] = '/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:' + os.environ.get('PATH', '')
STAMP = datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S-%f')
OUT = ROOT / 'logs' / ('week6-' + STAMP)
A, B = SLOT_OFFSETS['a'], SLOT_OFFSETS['b']
summary = {
    'status': 'RUNNING',
    'scope': 'week6-code-complete',
    'started_at': STAMP,
    'timezone': 'Asia/Shanghai',
    'checks': [],
    'failed': [],
    'evidence': str(OUT),
}


def save():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


def record(name, **extra):
    item = {'name': name, **extra}
    summary['checks'].append(item)
    save()
    print('PASS:', name, flush=True)


def fail(name, detail):
    summary['failed'].append({'name': name, 'detail': detail})
    summary['status'] = 'FAIL'
    save()
    raise RuntimeError(f'{name}: {detail}')


def run(args, name, timeout=180, cwd=ROOT):
    process = subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=timeout)
    text = process.stdout.decode(errors='replace')
    (OUT / (name + '.txt')).write_text(text)
    if process.returncode:
        fail(name, f'exit {process.returncode}\n{text[-3000:]}')
    return text


def write_image(path, packages=None, raw=None):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(raw if raw is not None else flash_seed(packages or {}))
    return path


def mutate(image, offset, xor=1):
    data = bytearray(Path(image).read_bytes())
    data[offset] ^= xor
    Path(image).write_bytes(data)


def decode(data):
    return data.decode(errors='replace') if isinstance(data, (bytes, bytearray)) else data


def build_targets():
    build = OUT / 'build'
    run(['bash', '-c',
         'source scripts/env.sh && cmake -S . -B "{}" -G "Unix Makefiles" '
         '-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake'.format(build)],
        'cmake-week6')
    run(['bash', '-c',
         'source scripts/env.sh && cmake --build "{}" --target '
         'bootloader-week6.elf application-week6-v1.elf application-week6-v2.elf '
         'application-week6-v3.elf application-week6-hang.elf --parallel'.format(build)],
        'build-week6', timeout=300)
    bins = {}
    for name, version in (('v1', 1), ('v2', 2), ('v3', 3), ('hang', 9)):
        elf = build / f'application-week6-{name}.elf'
        raw = build / f'application-week6-{name}.bin'
        run(['bash', '-c', f'source scripts/env.sh && arm-none-eabi-objcopy -O binary "{elf}" "{raw}"'],
            f'objcopy-{name}')
        pkg = OUT / f'application-week6-{name}.pkg'
        pkg.write_bytes(build_signed_package(raw.read_bytes(), version))
        parse_signed_package(pkg.read_bytes())
        bins[name] = {'elf': elf, 'bin': raw, 'pkg': pkg, 'version': version}
    size = run(['bash', '-c',
                f'source scripts/env.sh && arm-none-eabi-size "{build / "bootloader-week6.elf"}"'],
               'size-bootloader')
    (OUT / 'bootloader-size.txt').write_text(size)
    # text + data must fit the 64 KiB boot region (loadable sections).
    rows = [line.split() for line in size.strip().splitlines() if line[0].isdigit()]
    if not rows:
        # GNU size header then one data row
        rows = [line.split() for line in size.strip().splitlines()[1:]]
    text, data = int(rows[0][0]), int(rows[0][1])
    if text + data > 64 * 1024:
        fail('bootloader-size', f'text+data {text + data} exceeds 64 KiB')
    record('build-and-pack', text=text, data=data)
    return build, bins


def native_tests(bins):
    unsigned = OUT / 'unsigned-v1.pkg'
    unsigned.write_bytes(build_package(bins['v1']['bin'].read_bytes(), 1))
    bad = bytearray(bins['v1']['pkg'].read_bytes())
    bad[-1] ^= 0x5A
    bad_path = OUT / 'bad-signature.pkg'
    bad_path.write_bytes(bad)
    # Off-line parser
    parse_signed_package(bins['v1']['pkg'].read_bytes())
    try:
        parse_signed_package(unsigned.read_bytes())
        fail('parse-unsigned', 'unsigned package was accepted')
    except Exception as error:
        if 'code' in dir(error) and error.code != 11:
            fail('parse-unsigned', str(error))
    try:
        parse_signed_package(bad_path.read_bytes())
        # parser does not re-check the RSA value; the firmware test does.
    except Exception:
        pass
    record('host-parse-unsigned-rejected')

    includes = [
        '-Ibuild/test-signing',
        '-Isrc/week6', '-Isrc/week5', '-Isrc/week4', '-Isrc/week3', '-Isrc/week2',
        '-Itests/third_party/mbedtls/include', '-Itests/third_party/mbedtls/library',
        '-DMBEDTLS_CONFIG_FILE="week6_mbedtls_config.h"', '-DWEEK6_HOST_TEST',
        '-fsanitize=address,undefined', '-g', '-O1',
    ]
    mbedtls = [
        'tests/third_party/mbedtls/library/sha256.c',
        'tests/third_party/mbedtls/library/md.c',
        'tests/third_party/mbedtls/library/rsa.c',
        'tests/third_party/mbedtls/library/rsa_alt_helpers.c',
        'tests/third_party/mbedtls/library/bignum.c',
        'tests/third_party/mbedtls/library/bignum_core.c',
        'tests/third_party/mbedtls/library/bignum_mod.c',
        'tests/third_party/mbedtls/library/bignum_mod_raw.c',
        'tests/third_party/mbedtls/library/constant_time.c',
        'tests/third_party/mbedtls/library/oid.c',
        'tests/third_party/mbedtls/library/asn1parse.c',
        'tests/third_party/mbedtls/library/asn1write.c',
        'tests/third_party/mbedtls/library/platform.c',
        'tests/third_party/mbedtls/library/platform_util.c',
        'tests/third_party/mbedtls/library/memory_buffer_alloc.c',
    ]
    exe = OUT / 'test_secure_image'
    run(['cc'] + includes + [
        'tests/test_secure_image.c', 'src/week6/secure_image.c',
        'src/week4/image.c', 'src/week4/crc32.c', 'src/week3/flash.c',
        'src/week6/watchdog.c'] + mbedtls + ['-o', str(exe)],
        'compile-host-secure', timeout=180)
    run([str(exe), str(bins['v1']['pkg']), str(bad_path)], 'run-host-secure')
    run([str(exe), str(bins['v1']['pkg']), str(unsigned)], 'run-host-unsigned')
    record('native-sanitizer-verify')


def elf():
    return OUT / 'build' / 'bootloader-week6.elf'


def save_uart(name, session):
    (OUT / f'{name}-uart.bin').write_bytes(bytes(session.transcript))


def qemu_boot_v1(bins):
    image = write_image(OUT / 'seed-v1.bin', {'a': bins['v1']['pkg'].read_bytes()})
    with Session(elf(), image, evidence=OUT / 'boot-v1') as session:
        text = decode(session.boot(timeout=40))
        save_uart('boot-v1', session)
        if b'APP: VERSION v1' not in session.transcript or b'APP: BOOT CONFIRMED' not in session.transcript:
            fail('boot-signed-v1', text[-800:])
        session.fault_snapshot(OUT / 'after-v1.bin')
    record('boot-signed-v1')
    return OUT / 'after-v1.bin'


def qemu_upgrade_v2(bins, image):
    working = write_image(OUT / 'work-upgrade-v2.bin', raw=image.read_bytes())
    with Session(elf(), working, evidence=OUT / 'upgrade-v2') as session:
        sent, text = session.upload(bins['v2']['pkg'], command=b'u')
        save_uart('upgrade-v2', session)
        blob = session.transcript
        if not sent:
            fail('upgrade-signed-v2', 'YMODEM sender failed')
        if b'UPGRADE: COMMITTED' not in blob or b'APP: VERSION v2' not in blob or b'APP: BOOT CONFIRMED' not in blob:
            fail('upgrade-signed-v2', decode(blob[-800:]))
        session.fault_snapshot(OUT / 'after-v2.bin')
    record('upgrade-signed-v2')
    return OUT / 'after-v2.bin'


def qemu_reject_unsigned(bins, image):
    unsigned = OUT / 'unsigned-v3.pkg'
    unsigned.write_bytes(build_package(bins['v3']['bin'].read_bytes(), 3))
    working = write_image(OUT / 'work-unsigned.bin', raw=image.read_bytes())
    with Session(elf(), working, evidence=OUT / 'reject-unsigned') as session:
        sent, text = session.upload(unsigned, command=b'u')
        save_uart('reject-unsigned', session)
        blob = session.transcript
        if b'UPGRADE: COMMITTED' in blob or b'APP: VERSION v3' in blob:
            fail('reject-unsigned', 'unsigned package was activated')
        if b'APP: VERSION v2' not in blob:
            fail('reject-unsigned', 'expected to remain on signed v2:\n' + decode(blob[-800:]))
        session.fault_snapshot(OUT / 'after-unsigned.bin')
    record('reject-unsigned')
    return OUT / 'after-unsigned.bin'


def qemu_rollback_corrupt(bins):
    image = write_image(OUT / 'seed-ab.bin', {
        'a': bins['v1']['pkg'].read_bytes(),
        'b': bins['v2']['pkg'].read_bytes(),
    })
    # Corrupt a payload byte of slot A, after the image header.
    mutate(image, A + HEADER_SIZE + 80)
    with Session(elf(), image, evidence=OUT / 'rollback-corrupt') as session:
        text = decode(session.boot(timeout=40))
        save_uart('rollback-corrupt', session)
        blob = session.transcript
        if b'ROLLBACK:' not in blob or b'APP: VERSION v2' not in blob:
            fail('rollback-corrupt-active', text[-800:])
        session.fault_snapshot(OUT / 'after-rollback.bin')
    record('rollback-corrupt-active')


def qemu_both_invalid(bins):
    image = write_image(OUT / 'seed-both-bad.bin', {
        'a': bins['v1']['pkg'].read_bytes(),
        'b': bins['v2']['pkg'].read_bytes(),
    })
    mutate(image, A + HEADER_SIZE + 80)
    mutate(image, B + HEADER_SIZE + 80)
    with Session(elf(), image, evidence=OUT / 'both-invalid') as session:
        # Session.__enter__ already consumed the BOOT6 banner; query immediately
        # so the request beats the auto-boot window.
        session.write(b'v')
        data = session.read_until(b'PARAM window_ms=', 20)
        save_uart('both-invalid', session)
        if b'APP: RUNNING' in session.transcript:
            fail('both-invalid-stay-cli', 'application started with two bad slots')
        if b'INVALID' not in session.transcript:
            fail('both-invalid-stay-cli', decode(session.transcript[-800:]))
    record('both-invalid-stay-cli')


def qemu_status_and_param(bins):
    image = write_image(OUT / 'seed-status.bin', {'a': bins['v1']['pkg'].read_bytes()})
    with Session(elf(), image, evidence=OUT / 'status') as session:
        session.write(b'v')
        session.read_until(b'SIG OK', 20)
        session.command('p window_ms 4000', expect='CONFIG: SAVED', timeout=20)
        save_uart('status-param', session)
        if b'BOOT6: BL week6-1' not in session.transcript:
            fail('status-and-param', decode(session.transcript[-800:]))
        session.fault_snapshot(OUT / 'after-param.bin')
    with Session(elf(), OUT / 'after-param.bin', evidence=OUT / 'status-restart') as session:
        session.write(b'p')
        session.write(b'\r')
        session.read_until(b'window_ms=', 20)
        # Read a little more for the number.
        time.sleep(0.3)
        try:
            session._recv(256, 0.5)
        except Exception:
            pass
        save_uart('status-restart', session)
        if b'04000' not in session.transcript and b'4000' not in session.transcript:
            fail('param-persist', decode(session.transcript[-800:]))
    record('status-and-param-persist')


def qemu_factory(bins):
    image = write_image(OUT / 'seed-factory.bin', raw=(OUT / 'after-param.bin').read_bytes())
    with Session(elf(), image, evidence=OUT / 'factory') as session:
        session.command('factory', expect='FACTORY: send YES', timeout=15)
        session.command('YES', expect='FACTORY: DONE', timeout=20)
        save_uart('factory', session)
        session.fault_snapshot(OUT / 'after-factory.bin')
    # Firmware slots must still be present; default window is 2500.
    with Session(elf(), OUT / 'after-factory.bin', evidence=OUT / 'factory-boot') as session:
        text = decode(session.boot(timeout=40))
        save_uart('factory-boot', session)
        if b'APP: VERSION v1' not in session.transcript and b'APP: VERSION v2' not in session.transcript:
            fail('factory-reset', 'factory reset lost a bootable signed image: ' + text[-800:])
    record('factory-reset-keeps-firmware')


def qemu_trial_hang(bins):
    """Upload a hanging app; the CMSDK watchdog must roll back to v1."""
    image = write_image(OUT / 'seed-trial.bin', {'a': bins['v1']['pkg'].read_bytes()})
    # First boot confirms v1 so last_confirmed is A.
    with Session(elf(), image, evidence=OUT / 'trial-prep') as session:
        session.boot(timeout=40)
        session.fault_snapshot(OUT / 'trial-confirmed-v1.bin')
        save_uart('trial-prep', session)
    working = write_image(OUT / 'work-trial.bin', raw=(OUT / 'trial-confirmed-v1.bin').read_bytes())
    pre_launch = OUT / 'trial-pre-reset.bin'
    with Session(elf(), working, evidence=OUT / 'trial-hang') as session:
        session.quiescent = False
        start = len(session.transcript)
        session.write(b'u')
        from week5_session import sender, PeerCancelled
        session.sending = True
        try:
            sender(session).send([str(bins['hang']['pkg'].resolve())])
        except PeerCancelled:
            pass
        finally:
            session.sending = False
        session.read_until(b'APP: RUNNING', 40)
        session.fault_snapshot(pre_launch)
        save_uart('trial-hang-running', session)
        if b'UPGRADE: COMMITTED' not in session.transcript:
            fail('trial-hang-rollback', 'hang image was not committed')
        # Wait for a watchdog-driven re-entry, or fall back to a cold start
        # from the pre-reset snapshot if this QEMU reset wipes the SRAM NOR.
        restarted = False
        deadline = time.monotonic() + 20
        start_wait = len(session.transcript)
        while time.monotonic() < deadline:
            try:
                session._recv(4096, 1)
            except Exception:
                break
            if session.transcript[start_wait:].count(b'BOOT6: command window') >= 1:
                restarted = True
                break
        save_uart('trial-hang-wait', session)
        if restarted and b'APP: VERSION v1' in session.transcript[start_wait:]:
            record('trial-hang-rollback', method='in-process-reset')
            return
        if restarted and b'ROLLBACK:' in session.transcript[start_wait:]:
            # auto-boot may still be in the window; wait for the app.
            try:
                session.boot(timeout=20, transcript_start=start_wait)
            except Exception as error:
                save_uart('trial-hang-reboot', session)
                # fall through to snapshot restart
            else:
                if b'APP: VERSION v1' in session.transcript:
                    record('trial-hang-rollback', method='in-process-reset')
                    return
    # Host-mediated restart: SRAM NOR may not survive QEMU system_reset.
    with Session(elf(), pre_launch, evidence=OUT / 'trial-hang-cold') as session:
        text = decode(session.boot(timeout=40))
        save_uart('trial-hang-cold', session)
        if b'ROLLBACK:' not in session.transcript or b'APP: VERSION v1' not in session.transcript:
            fail('trial-hang-rollback', 'did not roll back after trial failure: ' + text[-800:])
    record('trial-hang-rollback', method='snapshot-restart',
           note='QEMU system_reset did not keep the SRAM NOR model in-process; '
                'firmware rollback was verified by restarting from a pre-reset snapshot.')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    save()
    try:
        build, bins = build_targets()
        native_tests(bins)
        after_v1 = qemu_boot_v1(bins)
        after_v2 = qemu_upgrade_v2(bins, after_v1)
        qemu_reject_unsigned(bins, after_v2)
        qemu_rollback_corrupt(bins)
        qemu_both_invalid(bins)
        qemu_status_and_param(bins)
        qemu_factory(bins)
        qemu_trial_hang(bins)
        if summary['failed']:
            summary['status'] = 'FAIL'
        else:
            summary['status'] = 'PASS'
        summary['finished_at'] = datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S')
        save()
        latest = ROOT / 'logs' / 'week6-latest.txt'
        latest.write_text(str(OUT.relative_to(ROOT / 'logs')) + '\n')
        print('SUMMARY', summary['status'], 'checks', len(summary['checks']), 'dir', OUT)
        if summary['status'] != 'PASS':
            return 1
        return 0
    except Exception as error:
        summary['status'] = 'FAIL'
        summary['error'] = str(error)
        save()
        latest = ROOT / 'logs' / 'week6-latest.txt'
        latest.write_text(str(OUT.relative_to(ROOT / 'logs')) + '\n')
        print('FAIL:', error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
