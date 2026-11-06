#!/usr/bin/env python3
"""Pack one raw application binary into a Week-5 single-firmware package.

The output is exactly 32 + payload bytes: the existing image header followed by
the payload. A flash dump or an ELF is rejected instead of being packaged.
"""
import argparse
from pathlib import Path
from week5_package import MAX_PAYLOAD, PackageError, build_package


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True,
                        help='raw application payload from arm-none-eabi-objcopy -O binary')
    parser.add_argument('--output', type=Path, required=True,
                        help='package path; exactly 32 + payload bytes are written')
    parser.add_argument('--version', type=int, default=1,
                        help='image header version, default 1')
    parser.add_argument('--force', action='store_true',
                        help='replace an existing generated package')
    args = parser.parse_args()
    payload = args.app.read_bytes()
    if payload[:4] == b'\x7fELF':
        parser.error(f'{args.app}: an ELF is not a payload; use objcopy -O binary')
    if len(payload) > MAX_PAYLOAD and len(payload) % 0x10000 == 0:
        parser.error(f'{args.app}: {len(payload)} bytes looks like a flash dump, '
                     'not a single application payload')
    try:
        package = build_package(payload, args.version)
    except PackageError as error:
        parser.error(f'{args.app}: invalid payload (result code {error.code}): {error}')
    if args.output.exists() and not args.force:
        parser.error(f'{args.output} exists; pass --force to replace it')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)
    print(f'Packed {args.app} ({len(payload)} payload bytes) into {args.output} '
          f'({len(package)} bytes, version {args.version})')


if __name__ == '__main__':
    main()
