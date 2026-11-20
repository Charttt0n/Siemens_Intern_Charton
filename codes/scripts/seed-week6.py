#!/usr/bin/env python3
"""Create a QEMU NOR seed from signed Week-6 packages. Lab setup only."""
import argparse
from pathlib import Path
from week6_package import flash_seed, parse_signed_package


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--slot-a', type=Path, required=True)
    parser.add_argument('--slot-b', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--force', action='store_true')
    parser.add_argument('--allow-unsigned', action='store_true',
                        help='accept a Week-5 unsigned image for negative tests')
    args = parser.parse_args()
    try:
        packages = {}
        for slot, path in (('a', args.slot_a), ('b', args.slot_b)):
            if path is None:
                continue
            if path.resolve() == args.output.resolve():
                parser.error('output must differ from package inputs')
            data = path.read_bytes()
            if not args.allow_unsigned:
                parse_signed_package(data)
            packages[slot] = data
        if args.output.exists() and not args.force:
            parser.error('output exists; pass --force for explicit replacement')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(flash_seed(packages))
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f'Created NOR seed: {args.output.resolve()}')


if __name__ == '__main__':
    main()
