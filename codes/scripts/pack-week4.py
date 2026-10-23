#!/usr/bin/env python3
import argparse
from pathlib import Path
from image_format import package


def main():
    parser = argparse.ArgumentParser(description='Create a fresh stage-4 demo Flash image')
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', type=int, default=1)
    parser.add_argument('--force', action='store_true', help='replace an existing generated image')
    args = parser.parse_args()
    image = package(args.app.read_bytes(), args.version)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('wb' if args.force else 'xb') as stream:
        stream.write(image)
    print(f'Packaged {args.app} into {args.output} ({len(image)} bytes)')


if __name__ == '__main__':
    main()
