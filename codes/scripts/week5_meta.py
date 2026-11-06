"""Minimal Week-5 boot_meta reader for QEMU evidence (mirrors boot_meta.h)."""
import argparse
import json
from pathlib import Path
import struct
import zlib

MAGIC = 0x4D42354D
SCHEMA = 1
MARKER = 0x54494D43
PARAMS_OFFSET = 0xF0000
PAGE_SIZE = 0x1000
RECORD_SIZE = 48
STATE = {0: 'IDLE', 1: 'RECEIVING', 2: 'VERIFYING', 3: 'READY', 4: 'FAILED'}


def decode_record(raw):
    if len(raw) < RECORD_SIZE:
        return None
    fields = struct.unpack('<12I', raw[:RECORD_SIZE])
    (magic, schema, generation, active, target, state, progress, expected,
     version, error, crc, marker) = fields
    if marker != MARKER or magic != MAGIC or schema != SCHEMA:
        return None
    if zlib.crc32(raw[:40]) != crc or generation == 0:
        return None
    if active not in (1, 2) or target not in (1, 2, 0xFFFFFFFF) or target == active:
        return None
    if state == 0:
        if target != 0xFFFFFFFF or any((progress, expected, version, error)):
            return None
    elif state in (1, 2, 3):
        if target == 0xFFFFFFFF or not 192 <= expected <= 0x6FFE0:
            return None
        if progress > expected or error or (state == 3 and progress != expected):
            return None
    elif state == 4:
        if target == 0xFFFFFFFF or not error:
            return None
        if expected == 0:
            if progress:
                return None
        elif not 192 <= expected <= 0x6FFE0 or progress > expected:
            return None
    else:
        return None
    return {'generation': generation, 'active': active, 'target': target,
            'state': state, 'state_name': STATE.get(state, '?'),
            'progress': progress, 'expected': expected,
            'version': version, 'error': error}


def read_meta(flash):
    """Newest valid record, or None. Raises ValueError on ambiguity."""
    if len(flash) != 0x100000:
        raise ValueError('flash image must be 1 MiB')
    records = []
    for page in (0, 1):
        start = PARAMS_OFFSET + page * PAGE_SIZE
        record = decode_record(flash[start:start + RECORD_SIZE])
        if record:
            records.append(record)
    if not records:
        return None
    if len(records) == 1:
        return records[0]
    first, second = records
    difference = (first['generation'] - second['generation']) & 0xFFFFFFFF
    if difference == 0 or difference == 0x80000000:
        raise ValueError('ambiguous metadata records')
    return first if difference < 0x80000000 else second


def main():
    parser = argparse.ArgumentParser(description='Read the latest valid Week-5 record from a NOR snapshot')
    parser.add_argument('image', type=Path, help='saved 1 MiB NOR image')
    args = parser.parse_args()
    try:
        record = read_meta(args.image.read_bytes())
        if record is None:
            raise ValueError('no valid metadata record (blank or damaged)')
    except (OSError, ValueError) as error:
        parser.exit(2, f'{parser.prog}: {error}\n')
    print(json.dumps(record))


if __name__ == '__main__':
    main()
