"""QEMU acceptance of Week-5 upgrades and controlled NOR tears.

Each tear exports the *current* NOR with the vCPU stopped, then boots a new
production QEMU process from that snapshot. Test-only wrappers select precise
operation prefixes; no claim of physical power-loss testing is made.
"""
import binascii
import hashlib
from pathlib import Path
import re
import socket
import struct
import threading
import time
import zlib
from week5_session import Session, sender, PeerCancelled
from week5_package import APP_BASE, build_package, flash_seed, SLOT_OFFSETS, SLOT_SIZE, HEADER_SIZE
from week5_meta import read_meta, PARAMS_OFFSET, RECORD_SIZE, MAGIC, SCHEMA, MARKER


def metadata(seed, active=1, state=0, target=0xFFFFFFFF, expected=0, progress=0,
             version=0, error=0):
    data = bytearray(seed)
    body = struct.pack('<10I', MAGIC, SCHEMA, 1, active, target, state,
                       progress, expected, version, error)
    data[PARAMS_OFFSET:PARAMS_OFFSET+RECORD_SIZE] = body + struct.pack('<2I', zlib.crc32(body), MARKER)
    return bytes(data)


def frame(seq, data):
    size = 128 if len(data) <= 128 else 1024
    data = data.ljust(size, b'\x1a')
    return bytes((1 if size == 128 else 2, seq, 255-seq)) + data + struct.pack('>H', binascii.crc_hqx(data, 0))


def acceptance(build, out, packages, summary, record):
    boot = build / 'bootloader-week5.elf'
    fault_boot = build / 'bootloader-week5-fault.elf'
    package_paths = {}
    for version, data in packages.items():
        path = build / f'accept-v{version}.pkg'
        path.write_bytes(data)
        package_paths[version] = path
    summary['fault_cases'] = []
    summary['boot_cases'] = []
    summary['upgrades'] = []
    summary['negative_cases'] = []
    base = metadata(flash_seed({'a': packages[1]}))

    def path_for(name, data):
        path = build / f'{name}.bin'
        path.write_bytes(data)
        return path

    def require_app(text, version, active):
        assert f'APP: VERSION v{version}\r\n'.encode() in text, text
        assert f'IMAGE: SLOT {active} CRC32 PASS\r\n'.encode() in text, text
        assert b'APP: HANDOFF PASS\r\n' in text and b'APP: SVC PASS\r\n' in text, text

    def reboot(name, data, version, active, automatic=False):
        image = path_for(name, data)
        with Session(boot, image, out / name) as vm:
            if not automatic:
                vm.write(b'b')
            text = vm.boot()
            require_app(text, version, active)
            vm.checkpoint()
        summary['boot_cases'].append({'name': name, 'version': version, 'active': active,
                                      'nor_sha256': hashlib.sha256(image.read_bytes()).hexdigest()})
        record('QEMU persisted boot ' + name)
        return image.read_bytes()

    def recovery_boot(name, data, marker=b'RECOVERY: IMAGE '):
        image = path_for(name, data)
        with Session(boot, image, out / name) as vm:
            vm.write(b'b')
            text = vm.read_until(marker)
            assert b'APP: RUNNING' not in text, text
        summary['boot_cases'].append({'name': name, 'result': 'RECOVERY'})
        record('QEMU recovery entry ' + name)

    # No request boots A after the actual monotonic startup wait. Persisted B
    # is honored even with a valid A. Incomplete/READY/FAILED transfers keep A.
    reboot('blank-meta-default-a', flash_seed({'a': packages[1]}), 1, 'A', automatic=True)
    both = flash_seed({'a': packages[1], 'b': packages[2]})
    reboot('metadata-selects-b', metadata(both, active=2), 2, 'B')
    for state in (1, 2, 3, 4):
        seed = metadata(both, state=state, target=2, expected=len(packages[2])-HEADER_SIZE,
                        progress=len(packages[2])-HEADER_SIZE, version=2,
                        error=9 if state == 4 else 0)
        reboot(f'uncommitted-state-{state}', seed, 1, 'A')
    damaged = bytearray(metadata(both))
    damaged[SLOT_OFFSETS['a'] + HEADER_SIZE + 500] ^= 1
    recovery_boot('bad-active-does-not-auto-fallback', bytes(damaged))
    recovery_boot('no-active-image', metadata(flash_seed({})))
    # Single damaged copy is tolerated; double damaged metadata must be reset
    # explicitly. Both records below are invalid, not merely erased.
    damaged = bytearray(base)
    damaged[PARAMS_OFFSET] ^= 1
    damaged[PARAMS_OFFSET+0x1000] = 0
    recovery_boot('double-metadata-damage', bytes(damaged), b'RECOVERY: META ')
    valid_second = bytearray(metadata(both, active=2))
    valid_second[PARAMS_OFFSET+0x1000:PARAMS_OFFSET+0x1000+48] = valid_second[PARAMS_OFFSET:PARAMS_OFFSET+48]
    valid_second[PARAMS_OFFSET] ^= 1
    reboot('one-valid-metadata-copy', bytes(valid_second), 2, 'B')
    ambiguous = bytearray(metadata(both))
    ambiguous[PARAMS_OFFSET+0x1000:PARAMS_OFFSET+0x1000+48] = ambiguous[PARAMS_OFFSET:PARAMS_OFFSET+48]
    recovery_boot('ambiguous-metadata', bytes(ambiguous), b'RECOVERY: META ')

    # Full cycle and reverse cycle, byte comparisons, old-slot hashes, reboot.
    image = path_for('continuous-upgrades', base)
    for version, active, old in ((2, 'B', 'a'), (3, 'A', 'b')):
        before = image.read_bytes()
        with Session(boot, image, out / f'upload-v{version}') as vm:
            sent, text = vm.upload(package_paths[version])
            assert sent is True and b'UPGRADE: COMMITTED\r\n' in text, text
            require_app(text, version, active)
            vm.checkpoint()
        after = image.read_bytes()
        old_start = SLOT_OFFSETS[old]
        assert after[old_start:old_start+SLOT_SIZE] == before[old_start:old_start+SLOT_SIZE]
        start = SLOT_OFFSETS[active.lower()]
        package = packages[version]
        assert after[start:start+len(package)] == package
        assert after[start+len(package):start+SLOT_SIZE] == b'\xff'*(SLOT_SIZE-len(package))
        meta = read_meta(after)
        assert meta['active'] == (1 if active == 'A' else 2) and meta['state'] == 0, meta
        assert meta['target'] == 0xFFFFFFFF, meta
        summary['upgrades'].append({'version': version, 'active': active, 'metadata': meta,
                                    'old_slot_sha256': hashlib.sha256(before[old_start:old_start+SLOT_SIZE]).hexdigest()})
        record(f'QEMU v{version} upload committed; {active} active, old slot unchanged')
        reboot(f'reboot-v{version}', after, version, active)

    # Recover corrupt metadata through the explicit r command, then upgrade.
    image = path_for('explicit-recovery', damaged)
    with Session(boot, image, out / 'explicit-recovery') as vm:
        sent, text = vm.upload(package_paths[2], command=b'r')
        assert sent and b'UPGRADE: COMMITTED' in text, text
        require_app(text, 2, 'B')
        vm.checkpoint()
    record('QEMU explicit metadata recovery and re-upload')

    maximum = build_package(packages[2][HEADER_SIZE:].ljust(SLOT_SIZE - HEADER_SIZE, b'\xff'), 2)
    maximum_path = path_for('maximum-slot-package', maximum)
    image = path_for('maximum-slot-upload', base)
    with Session(boot, image, out / 'maximum-slot-upload') as vm:
        sent, text = vm.upload(maximum_path)
        assert sent and b'UPGRADE: COMMITTED\r\n' in text, text
        require_app(text, 2, 'B')
        vm.checkpoint()
    after = image.read_bytes()
    old_start, target_start = SLOT_OFFSETS['a'], SLOT_OFFSETS['b']
    assert len(maximum) == SLOT_SIZE
    assert after[old_start:old_start + SLOT_SIZE] == base[old_start:old_start + SLOT_SIZE]
    assert after[target_start:target_start + SLOT_SIZE] == maximum
    meta = read_meta(after)
    assert meta['active'] == 2 and meta['state'] == 0, meta
    summary['maximum_slot_upload'] = {'bytes': len(maximum), 'active': 'B', 'metadata': meta}
    record('QEMU maximum-size package fills B; A unchanged and B committed')
    reboot('reboot-maximum-slot-upload', after, 2, 'B')

    bad_crc = bytearray(packages[2]); bad_crc[HEADER_SIZE+500] ^= 1
    bad_vector = bytearray(packages[2]); struct.pack_into('<I', bad_vector, HEADER_SIZE+4, 0x100C0)
    struct.pack_into('<I', bad_vector, 24, zlib.crc32(bad_vector[HEADER_SIZE:]))
    struct.pack_into('<I', bad_vector, 28, zlib.crc32(bad_vector[:28]))
    # Both CRCs and all lengths agree, but Reset points to the lone final byte.
    bad_vector_tail = bytearray(packages[2]) + b'\x00'
    tail = len(bad_vector_tail) - HEADER_SIZE - 1
    assert tail % 2 == 0
    struct.pack_into('<I', bad_vector_tail, HEADER_SIZE + 4, (APP_BASE + tail) | 1)
    struct.pack_into('<I', bad_vector_tail, 12, tail + 1)
    struct.pack_into('<I', bad_vector_tail, 24, zlib.crc32(bad_vector_tail[HEADER_SIZE:]))
    struct.pack_into('<I', bad_vector_tail, 28, zlib.crc32(bad_vector_tail[:28]))
    negative = [('bad-crc32', bytes(bad_crc)), ('bad-vector', bytes(bad_vector)),
                ('bad-vector-tail-halfword', bytes(bad_vector_tail)),
                ('truncated-file', packages[2][:-7]), ('extra-file-tail', packages[2]+b'extra'),
                ('file-over-capacity', b'x'*(SLOT_SIZE+1))]
    for name, data in negative:
        source = build / f'{name}.pkg'; source.write_bytes(data)
        image = path_for(name, base)
        with Session(boot, image, out / name) as vm:
            sent, text = vm.upload(source)
            assert b'UPGRADE: FAIL' in text and b'UPGRADE: COMMITTED' not in text, (sent, text)
            require_app(text, 1, 'A')
            vm.checkpoint()
        after = image.read_bytes(); meta = read_meta(after)
        assert meta['active'] == 1, meta
        if name == 'bad-vector-tail-halfword':
            assert meta['state'] == 4 and meta['error'] == 4, meta
        assert after[0x10000:0x80000] == base[0x10000:0x80000]
        summary['negative_cases'].append({'name': name, 'sender_result': sent, 'metadata': meta})
        record('QEMU rejects ' + name + '; old program runs')

    # Bad CRC16, duplicate ACK-loss frames, and byte-at-a-time fragments over
    # the actual Cortex-M7 UART. Native independent-sender tests cover wrap.
    image = path_for('uart-retransmit', base)
    package = packages[2]
    with Session(boot, image, out / 'uart-retransmit') as vm:
        vm.write(b'u'); vm.read_until(b'C')
        header = frame(0, b'fw.pkg\x00'+str(len(package)).encode()+b'\x00')
        for byte in header: vm.write(bytes((byte,)))
        vm.read_until(b'\x06C')
        first = frame(1, package[:1024]); bad = bytearray(first); bad[500] ^= 1
        vm.write(bad); vm.read_until(b'\x15')
        vm.write(first); vm.read_until(b'\x06')
        vm.write(first); vm.read_until(b'\x06')  # repeat after ACK loss
        vm.write(frame(2, package[1024:])); vm.read_until(b'\x06')
        vm.write(b'\x04'); vm.read_until(b'\x06C')
        vm.write(frame(0, b'\x00')); vm.read_until(b'\x06')
        text = vm.boot(); require_app(text, 2, 'B'); vm.checkpoint()
    assert image.read_bytes()[0x80000:0x80000+len(package)] == package
    record('QEMU UART fragmentation, CRC16 retry, duplicate frame -> one correct image')

    for name, action in (('cancel', 'cancel'), ('timeout', 'timeout')):
        image = path_for(name, base)
        with Session(boot, image, out / name) as vm:
            vm.write(b'u'); vm.read_until(b'C')
            vm.write(frame(0, b'fw.pkg\x001036\x00')); vm.read_until(b'\x06C')
            started = time.monotonic()
            if action == 'cancel': vm.write(b'\x18\x18')
            text = vm.boot(timeout=20)
            assert b'UPGRADE: FAIL' in text, text
            require_app(text, 1, 'A'); vm.checkpoint()
            elapsed = time.monotonic()-started
            assert elapsed < 18, elapsed
        assert read_meta(image.read_bytes())['active'] == 1
        summary['negative_cases'].append({'name': name, 'seconds': round(elapsed, 3)})
        record(f'QEMU {name} exits with old program')

    # Test-only injected erase/program/commit IO and UART loss indication.
    for id, name in ((10, 'erase-io'), (11, 'program-io'), (12, 'activation-meta-io'),
                     (13, 'rx-loss-indication')):
        data = bytearray(base); struct.pack_into('<I', data, 0xff000, id)
        image = path_for(name, data)
        with Session(fault_boot, image, out / name) as vm:
            sent, text = vm.upload(package_paths[2])
            assert b'UPGRADE: COMMITTED' not in text, text
            require_app(text, 1, 'A'); vm.checkpoint()
        after = image.read_bytes(); meta = read_meta(after)
        assert meta['active'] == 1 and after[0x10000:0x80000] == base[0x10000:0x80000], meta
        summary['negative_cases'].append({'name': name, 'metadata': meta})
        record('QEMU injected ' + name + ' preserves old selection')

    # Physical socket disconnect has no UART-level EOF indication. The guest
    # must time out, persist failure, and remain bootable from a new process.
    image = path_for('uart-disconnect', base)
    with Session(boot, image, out/'uart-disconnect') as vm:
        vm.write(b'u'); vm.read_until(b'C')
        vm.write(frame(0, b'fw.pkg\x00'+str(len(packages[2])).encode()+b'\x00'))
        vm.read_until(b'\x06C')
        vm.serial.shutdown(socket.SHUT_RDWR)
        vm.serial.close(); vm.serial = None
        deadline = time.monotonic()+20
        scratch = Path(vm.temp.name)/'params.bin'
        while time.monotonic() < deadline:
            vm.execute('pmemsave', {'val': 0x203f0000, 'size': 8192, 'filename': str(scratch)})
            sample = bytearray(base); sample[PARAMS_OFFSET:PARAMS_OFFSET+8192] = scratch.read_bytes()
            current = read_meta(sample)
            if current and current['state'] == 4: break
            time.sleep(.05)
        else: raise TimeoutError('disconnect did not reach FAILED')
        snapshot = out/'uart-disconnect/nor-after-timeout.bin'
        vm.fault_snapshot(snapshot)
    assert read_meta(snapshot.read_bytes())['active'] == 1
    reboot('restart-after-disconnect', snapshot.read_bytes(), 1, 'A')
    record('QEMU real socket disconnect -> bounded receiver timeout -> old boot')

    # Fill the real interrupt-driven ring while a test-only foreground storage
    # delay is active. This is distinct from injecting just a loss flag.
    data = bytearray(base); struct.pack_into('<I', data, 0xff000, 14)
    image = path_for('real-rx-overflow', data)
    with Session(fault_boot, image, out/'real-rx-overflow') as vm:
        vm.write(b'u'); vm.read_until(b'C')
        vm.write(frame(0, b'fw.pkg\x00'+str(len(packages[2])).encode()+b'\x00'))
        vm.write(b'\x00'*8192)  # deliberately violates YMODEM stop-and-wait
        text = vm.boot(); require_app(text, 1, 'A')
        assert b'UPGRADE: FAIL 0x00000001' in text, text
        vm.checkpoint()
    assert read_meta(image.read_bytes())['active'] == 1
    record('QEMU actual UART RX ring overflow aborts transfer and boots old program')

    # Precise tears. Start with non-erased inactive bytes to make partial erase
    # observable; these snapshots are intentionally kept as durable evidence.
    symbols = {}
    import subprocess
    for line in subprocess.check_output(['arm-none-eabi-nm', '-n', str(fault_boot)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3: symbols[fields[2]] = int(fields[0], 16)
    for id, name in ((1, 'target-erase-prefix73'), (2, 'target-word-prefix2'),
                     (3, 'activation-erase-prefix24'), (4, 'activation-body-prefix21'),
                     (5, 'activation-crc-prefix2'), (6, 'activation-marker-prefix2'),
                     (7, 'activation-marker-complete'), (8, 'ready-not-activated'),
                     (9, 'verifying-body-prefix21')):
        data = bytearray(base); data[0x80000:0xf0000] = b'\x00'*SLOT_SIZE
        struct.pack_into('<I', data, 0xff000, id)
        image = path_for(name, data)
        result = {}
        with Session(fault_boot, image, out / name) as vm:
            vm.write(b'u')
            def upload():
                try: result['sent'] = sender(vm).send([str(package_paths[2])])
                except PeerCancelled: result['cancelled'] = True
                except Exception as error: result['error'] = repr(error)
            worker = threading.Thread(target=upload, daemon=True); worker.start()
            deadline = time.monotonic()+20
            while time.monotonic() < deadline:
                value = vm.execute('human-monitor-command', {'command-line': f'xp /1wx 0x{symbols["fault_latched"]:x}'})
                match = re.search(r':\s*0x([0-9a-fA-F]+)', value)
                if match and int(match.group(1), 16) == id: break
                time.sleep(.02)
            else: raise TimeoutError(f'{name}: fault hook not reached, sender={result}')
            snapshot = out / name / 'nor-at-fault.bin'
            vm.fault_snapshot(snapshot)
            # End the host sender promptly; hardware UART disconnect is not an
            # immediate receiver error, and this process will not be resumed.
            vm.cancel_sender = True
            vm.serial.shutdown(socket.SHUT_RDWR)
            worker.join(timeout=3)
            assert not worker.is_alive(), (name, result)
        captured = snapshot.read_bytes(); meta = read_meta(captured)
        active = 2 if id == 7 else 1
        assert meta['active'] == active, (name, meta)
        assert captured[0x10000:0x80000] == base[0x10000:0x80000], name
        if id == 1:
            assert captured[0x80000:0x80000+73] == b'\xff'*73
            assert captured[0x80000+73:0xf0000] == b'\x00'*(SLOT_SIZE-73)
        if id == 2:
            assert captured[0x80000:0x80000+66] == packages[2][:66]
            assert captured[0x80000+66:0xf0000] == b'\xff'*(SLOT_SIZE-66)
        reboot('restart-'+name, captured, 2 if active == 2 else 1, 'B' if active == 2 else 'A')
        summary['fault_cases'].append({'name': name, 'fault_id': id, 'model': 'latched NOR prefix tear',
                                       'snapshot': str(snapshot.relative_to(out)), 'metadata': meta,
                                       'expected_version': 2 if active == 2 else 1,
                                       'sha256': hashlib.sha256(captured).hexdigest()})
        record('QEMU tear snapshot and cold restart ' + name)
    # Verify re-upload from a torn data write succeeds, using the captured NOR.
    damaged_data = (out/'target-word-prefix2/nor-at-fault.bin').read_bytes()
    image = path_for('retry-after-tear', damaged_data)
    with Session(boot, image, out/'retry-after-tear') as vm:
        sent, text = vm.upload(package_paths[2]); require_app(text, 2, 'B')
        assert sent and b'UPGRADE: COMMITTED' in text, text
        vm.checkpoint()
    record('QEMU full re-upload after torn target write succeeds')
