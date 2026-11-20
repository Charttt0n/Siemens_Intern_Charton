#!/usr/bin/env python3
"""Pack a raw application binary into a signed Week-6 firmware package."""
import argparse
from pathlib import Path
from week5_package import MAX_PAYLOAD, PackageError
from week6_package import build_signed_package


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', type=int, default=1)
    parser.add_argument('--key', type=Path,
                        help='PEM private key; defaults to the key for this local QEMU lab')
    parser.add_argument('--force', action='store_true')
    args = parser.parse_args()
    payload = args.app.read_bytes()
    if payload[:4] == b'\x7fELF':
        parser.error(f'{args.app}: an ELF is not a payload; use objcopy -O binary')
    if len(payload) > MAX_PAYLOAD and len(payload) % 0x10000 == 0:
        parser.error(f'{args.app}: looks like a flash dump, not a payload')
    try:
        package = build_signed_package(payload, args.version, args.key)
    except PackageError as error:
        parser.error(f'{args.app}: invalid payload (result code {error.code}): {error}')
    if args.output.exists() and not args.force:
        parser.error(f'{args.output} exists; pass --force to replace it')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)
    print(f'Packed signed {args.app} ({len(payload)} payload bytes) into '
          f'{args.output} ({len(package)} bytes, version {args.version})')


if __name__ == '__main__':
    main()
