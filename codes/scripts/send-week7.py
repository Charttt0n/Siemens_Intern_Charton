#!/usr/bin/env python3
"""Boot or upload a signed image to Week 7 in QEMU. The input NOR is read-only;
use --evidence/nor.bin as the next seed to retain the explicit snapshot."""
import argparse
from pathlib import Path
from week7_session import Session

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image',type=Path,required=True)
    p.add_argument('--elf',type=Path,default=Path('build/week7/bootloader-week7.elf'))
    p.add_argument('--package',type=Path)
    p.add_argument('--evidence',type=Path,required=True)
    a=p.parse_args()
    if not a.image.is_file():p.error('NOR seed does not exist')
    if a.package and not a.package.is_file():p.error('package does not exist')
    with Session(a.elf,a.image,a.evidence) as vm:
        # __enter__ has already consumed the command-window marker.
        if a.package:
            sent,text=vm.upload(a.package)
            print(text.decode(errors='replace'))
            if not sent or b'UPGRADE: COMMITTED' not in text:
                raise SystemExit('upgrade did not commit; inspect UART evidence')
        else:
            text=vm.boot(timeout=30);print(text.decode(errors='replace'))
        if b'APP: BOOT CONFIRMED' not in vm.transcript:
            raise SystemExit('application did not confirm boot')
        vm.fault_snapshot(a.evidence/'nor.bin')
    return 0
if __name__=='__main__':raise SystemExit(main())
