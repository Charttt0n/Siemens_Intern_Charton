#!/usr/bin/env python3
"""Create a QEMU NOR seed from validated single-firmware packages.

Only for initial lab setup. Use send-week5.py for actual upgrades; seeding a
flash file is not evidence of a UART upgrade. Metadata starts erased and is
initialized to active A by the bootloader on first boot.
"""
import argparse
from pathlib import Path
from week5_package import flash_seed, parse_package


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--slot-a', type=Path, required=True)
    parser.add_argument('--slot-b', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--force', action='store_true')
    args = parser.parse_args()
    try:
        inputs = {'a': args.slot_a, 'b': args.slot_b}
        packages = {}
        for slot, path in inputs.items():
            if path is None:
                continue
            if path.resolve() == args.output.resolve():
                parser.error('output must differ from package inputs')
            packages[slot] = path.read_bytes()
            parse_package(packages[slot])
        if args.output.exists() and not args.force:
            parser.error('output exists; pass --force for explicit replacement')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(flash_seed(packages))
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f'Created NOR seed: {args.output.resolve()} (active A after initialization)')


if __name__ == '__main__':
    main()
