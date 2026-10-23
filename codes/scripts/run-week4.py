#!/usr/bin/env python3
"""Run the image boot demonstration; stop host emulator after the result."""
import argparse
import os
from pathlib import Path
import sys
from week3_session import Session

ROOT = Path(__file__).resolve().parents[1]
os.environ['PATH'] = '/opt/homebrew/bin:' + os.environ.get('PATH', '')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, default=ROOT/'build/week4/demo-flash.bin')
    parser.add_argument('--elf', type=Path, default=ROOT/'build/week4/bootloader-week4.elf')
    args = parser.parse_args()
    try:
        if not args.image.is_file():
            raise ValueError('image missing; run bash scripts/build-week4.sh first')
        with Session(args.elf, args.image) as vm:
            sys.stdout.write(vm.banner.decode('ascii'))
            return 0 if b'APP: RUNNING\r\n> ' in vm.banner else 1
    except (OSError, RuntimeError, ValueError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
