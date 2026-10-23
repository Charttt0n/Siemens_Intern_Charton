#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import sys
from week3_session import Session

ROOT = Path(__file__).resolve().parents[1]
os.environ['PATH'] = '/opt/homebrew/bin:' + os.environ.get('PATH', '')


def main():
    parser = argparse.ArgumentParser(description='Week 3 persistent SRAM/NOR lab')
    parser.add_argument('--image', type=Path, default=ROOT / 'build/week3-flash.bin')
    parser.add_argument('--elf', type=Path, default=ROOT / 'build/week3/bootloader-week3.elf')
    args = parser.parse_args()
    print('Commands: p ping; r read; e erase first parameter sector; w write W3OK; s stats; h help')
    print('Host: save = checkpoint; quit / EOF = checkpoint and exit. Ctrl-C = exit without saving.')
    try:
        with Session(args.elf, args.image) as vm:
            sys.stdout.write(vm.banner.decode('ascii'))
            sys.stdout.flush()
            while True:
                line = sys.stdin.readline()
                cmd = line.strip()
                if not line or cmd == 'quit':
                    vm.checkpoint()
                    print(f'Saved: {args.image}')
                    break
                if cmd == 'save':
                    vm.checkpoint()
                    print(f'Saved: {args.image}\n> ', end='', flush=True)
                elif cmd in ('p', 'r', 'e', 'w', 's', 'h'):
                    sys.stdout.write(vm.command(cmd.encode()).decode('ascii'))
                    sys.stdout.flush()
                else:
                    print('Enter p/r/e/w/s/h, save or quit.\n> ', end='', flush=True)
    except KeyboardInterrupt:
        print('\nExited without saving; last checkpoint retained.')
        return 130
    except (OSError, RuntimeError, ValueError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
