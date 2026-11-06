#!/usr/bin/env python3
"""Run the QEMU Week-5 lab, optionally send a firmware package, save NOR.

A successful sender ACK alone is insufficient: require COMMITTED, new program
output and metadata consistent with the package before reporting success.
"""
import argparse
import json
from pathlib import Path
import sys
from week5_session import Session
from week5_package import parse_package, SLOT_OFFSETS
from week5_meta import read_meta


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf', type=Path, default=Path('build/week5/bootloader-week5.elf'))
    p.add_argument('--image', type=Path, required=True, help='1 MiB persistent NOR lab image')
    p.add_argument('--package', type=Path, help='omit to boot the current application')
    p.add_argument('--recover', action='store_true', help='explicitly reset damaged metadata before upload')
    p.add_argument('--evidence', type=Path, help='UART/QMP/guest-error log directory')
    p.add_argument('--packet-size', type=int, choices=(128, 1024), default=1024)
    args = p.parse_args()
    if args.recover and not args.package:
        p.error('--recover requires --package')
    try:
        package = parse_package(args.package.read_bytes()) if args.package else None
        with Session(args.elf, args.image, args.evidence) as vm:
            if package:
                sent, text = vm.upload(args.package, b'r' if args.recover else b'u', args.packet_size)
                if not sent or b'UPGRADE: COMMITTED\r\n' not in text:
                    raise RuntimeError(f'upgrade not committed: {text!r}')
            else:
                vm.write(b'b')
                text = vm.boot()
            # The process has reached application idle, so ordinary checkpoint
            # semantics are valid. Save only after a complete verified result.
            vm.checkpoint()
        record = read_meta(args.image.read_bytes())
        if package and (record is None or record['state'] != 0):
            raise RuntimeError('committed IDLE metadata missing')
        if package:
            active = 'a' if record['active'] == 1 else 'b'
            expected = args.package.read_bytes()
            start = SLOT_OFFSETS[active]
            if args.image.read_bytes()[start:start + len(expected)] != expected:
                raise RuntimeError('active slot does not match the uploaded package')
            if f'IMAGE: VERSION 0x{package.version:08x}\r\n'.encode() not in text:
                raise RuntimeError('booted image version does not match upload')
            text = text[text.index(b'UPGRADE:'):]
        print(text.decode('ascii', errors='replace'), end='')
        print(json.dumps({'status': 'PASS', 'metadata': record,
                          'image': str(args.image.resolve())}, ensure_ascii=False))
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        print(f'Week5: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
