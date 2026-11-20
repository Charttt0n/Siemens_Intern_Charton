#!/usr/bin/env python3
"""Upload a signed Week-6 package over YMODEM or inspect a saved NOR seed."""
import argparse
from pathlib import Path
import sys
import shutil

sys.path.insert(0, str(Path(__file__).resolve().parent))
from week3_session import FLASH_SIZE
from week6_session import Session


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True, help='NOR seed or working image')
    parser.add_argument('--elf', type=Path, default=Path('build/week6/bootloader-week6.elf'))
    parser.add_argument('--package', type=Path)
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--boot-only', action='store_true')
    args = parser.parse_args()
    work = args.image
    if not work.exists():
        parser.error(f'{work} does not exist')
    with Session(args.elf, work, evidence=args.evidence) as session:
        session.read_until(b'BOOT6: command window\r\n', 8)
        if args.package and not args.boot_only:
            sent, text = session.upload(args.package, command=b'u')
            print(text.decode(errors='replace'))
            if not sent:
                raise SystemExit('YMODEM sender reported failure')
        else:
            text = session.boot(timeout=30)
            print(text.decode(errors='replace'))
        session.fault_snapshot(work) if False else None
        # Persist the running NOR back into the working image when evidence is requested.
        if args.evidence:
            args.evidence.mkdir(parents=True, exist_ok=True)
            session.fault_snapshot(args.evidence / 'nor.bin')
            (args.evidence / 'uart.bin').write_bytes(bytes(session.transcript))


if __name__ == '__main__':
    main()
