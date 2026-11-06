#!/usr/bin/env python3
"""Offline tests for the Week-5 package format and slot placement."""
import struct
import unittest
import zlib
from week5_package import (ADDRESS, APP_BASE, FORMAT, FORMAT_ERROR, FLASH_SIZE,
                           HEADER_CRC, HEADER_SIZE, LENGTH, MAGIC, MAX_PAYLOAD,
                           MIN_PAYLOAD, PAYLOAD_CRC, SLOT_OFFSETS, SLOT_SIZE,
                           VECTOR, PackageError, build_package, flash_seed,
                           parse_package)

RESERVED = (7, 8, 9, 10, 13)


def make_payload(size=512, tag=0, msp=0x20020000, reset=0x100C1):
    """Synthetic payload with a vector table the firmware accepts."""
    data = bytearray((index * 7 + tag) & 0xFF for index in range(size))
    if size < MIN_PAYLOAD:
        return bytes(data)
    struct.pack_into('<I', data, 0, msp)
    for index in range(1, 48):
        struct.pack_into('<I', data, 4 * index, 0 if index in RESERVED else reset)
    return bytes(data)


def reseal(payload, version=1):
    """Build header + payload without vector/length checks, for negative cases."""
    header = struct.pack('<7I', MAGIC, FORMAT, HEADER_SIZE, len(payload),
                         APP_BASE, version, zlib.crc32(payload))
    return header + struct.pack('<I', zlib.crc32(header)) + payload


def patch_payload(payload, word_index, value):
    data = bytearray(payload)
    struct.pack_into('<I', data, 4 * word_index, value)
    return reseal(bytes(data))


def patch_header(package, word_index, value, repair=True):
    data = bytearray(package)
    struct.pack_into('<I', data, 4 * word_index, value)
    if repair:
        struct.pack_into('<I', data, 28, zlib.crc32(data[:28]))
    return bytes(data)


class PackageBuildTests(unittest.TestCase):
    def test_package_is_header_plus_payload(self):
        payload = make_payload(1024, tag=1)
        package = build_package(payload, version=2)
        self.assertEqual(len(package), HEADER_SIZE + len(payload))
        self.assertNotEqual(len(package), FLASH_SIZE)
        self.assertEqual(struct.unpack_from('<7I', package), (
            MAGIC, FORMAT, HEADER_SIZE, len(payload), APP_BASE, 2,
            zlib.crc32(payload)))
        parsed = parse_package(package)
        self.assertEqual(parsed.payload, payload)
        self.assertEqual(parsed.version, 2)
        self.assertEqual(parsed.load, APP_BASE)
        self.assertEqual(parsed.crc, zlib.crc32(payload))
        self.assertEqual(parsed.size, len(payload))

    def test_payload_length_boundaries(self):
        with self.assertRaises(PackageError) as caught:
            build_package(make_payload(MIN_PAYLOAD - 1))
        self.assertEqual(caught.exception.code, LENGTH)
        # A handler needs at least two bytes after the vector table.
        for size in (MIN_PAYLOAD, MIN_PAYLOAD + 1):
            with self.subTest(size=size), self.assertRaises(PackageError) as caught:
                build_package(make_payload(size))
            self.assertEqual(caught.exception.code, VECTOR)
        for size in (MIN_PAYLOAD + 2, MIN_PAYLOAD + 3):
            package = build_package(make_payload(size))
            self.assertEqual(len(package), HEADER_SIZE + size)
        maximum = build_package(make_payload(MAX_PAYLOAD))
        self.assertEqual(len(maximum), SLOT_SIZE)
        with self.assertRaises(PackageError) as caught:
            build_package(make_payload(MAX_PAYLOAD + 1))
        self.assertEqual(caught.exception.code, LENGTH)

    def test_version_range(self):
        payload = make_payload(512)
        self.assertEqual(parse_package(build_package(payload, 0)).version, 0)
        self.assertEqual(parse_package(build_package(payload, 0xFFFFFFFF)).version,
                         0xFFFFFFFF)
        with self.assertRaises(ValueError):
            build_package(payload, 0x100000000)


class PackageParseTests(unittest.TestCase):
    def setUp(self):
        self.payload = make_payload(512, tag=3)
        self.package = build_package(self.payload, version=5)

    def assert_code(self, data, code, **kwargs):
        with self.assertRaises(PackageError) as caught:
            parse_package(data, **kwargs)
        self.assertEqual(caught.exception.code, code, str(caught.exception))

    def test_truncated_and_trailing_bytes(self):
        self.assert_code(self.package[:20], LENGTH)
        self.assert_code(self.package[:HEADER_SIZE], LENGTH)
        self.assert_code(self.package[:-1], LENGTH)
        self.assert_code(self.package + b'\x00', LENGTH)
        self.assert_code(self.package + b'\xff' * HEADER_SIZE, LENGTH)

    def test_header_field_errors(self):
        for index in (0, 1, 2):
            self.assert_code(patch_header(self.package, index, 0), FORMAT_ERROR)
        self.assert_code(patch_header(self.package, 7, 0, repair=False), HEADER_CRC)
        self.assert_code(patch_header(self.package, 4, 0x20000000), ADDRESS)
        self.assert_code(patch_header(self.package, 4, 0), ADDRESS)

    def test_declared_length_versus_file_length(self):
        self.assert_code(patch_header(self.package, 3, 513), LENGTH)
        self.assert_code(patch_header(self.package, 3, 0xFFFFFFFF), LENGTH)
        self.assert_code(patch_header(self.package, 3, MAX_PAYLOAD + 1), LENGTH)
        self.assert_code(patch_header(self.package, 3, 0), LENGTH)

    def test_capacity_parameter(self):
        larger = build_package(make_payload(4096, tag=7))
        parse_package(larger, capacity=4096)
        self.assert_code(larger, LENGTH, capacity=1024)

    def test_payload_crc_mismatch(self):
        broken = bytearray(self.package)
        broken[-1] ^= 0x01
        self.assert_code(bytes(broken), PAYLOAD_CRC)

    def test_vector_errors(self):
        payload = make_payload(512, tag=9)
        cases = {
            'reserved-vector-nonzero': (7, 1),
            'reserved-vector-debugmon': (13, 0x100C1),
            'reset-not-thumb': (1, 0x100C0),
            'reset-before-table': (1, 0x10001),
            'irq-outside-payload': (16, 0x10000 + 512),
            'msp-zero': (0, 0),
            'msp-boot-ram': (0, 0x20010000),
            'msp-unaligned': (0, 0x20020004),
            'msp-above-top': (0, 0x20200008),
        }
        for name, (index, value) in cases.items():
            with self.subTest(name):
                self.assert_code(patch_payload(payload, index, value), VECTOR)

    def test_valid_payload_with_reserved_vectors_zero(self):
        payload = bytearray(make_payload(512, tag=4))
        for index in RESERVED:
            struct.pack_into('<I', payload, 4 * index, 0)
        parsed = parse_package(reseal(bytes(payload)))
        self.assertEqual(parsed.size, 512)

    def test_handler_needs_complete_halfword_at_payload_end(self):
        size = 513
        payload = make_payload(size)
        for index in (1, 16):
            self.assert_code(patch_payload(payload, index, (APP_BASE + size - 1) | 1), VECTOR)
            valid = patch_payload(payload, index, (APP_BASE + size - 3) | 1)
            self.assertEqual(parse_package(valid).size, size)


class FlashSeedTests(unittest.TestCase):
    def test_seed_layout(self):
        first = build_package(make_payload(300, tag=1), version=1)
        second = build_package(make_payload(400, tag=2), version=2)
        seed = flash_seed({'a': first, 'b': second})
        self.assertEqual(len(seed), FLASH_SIZE)
        start_a, start_b = SLOT_OFFSETS['a'], SLOT_OFFSETS['b']
        self.assertEqual(seed[start_a:start_a + len(first)], first)
        self.assertEqual(seed[start_b:start_b + len(second)], second)
        self.assertEqual(seed[:start_a], b'\xff' * start_a)
        self.assertEqual(seed[start_a + len(first):start_b],
                         b'\xff' * (start_b - start_a - len(first)))
        self.assertEqual(seed[start_b + len(second):],
                         b'\xff' * (FLASH_SIZE - start_b - len(second)))
        for name, package_bytes in (('a', first), ('b', second)):
            start = SLOT_OFFSETS[name]
            parsed = parse_package(seed[start:start + len(package_bytes)])
            self.assertEqual(parsed.payload, package_bytes[HEADER_SIZE:])

    def test_seed_rejects_unknown_slot_and_oversize(self):
        with self.assertRaises(ValueError):
            flash_seed({'c': b'\xff' * HEADER_SIZE})
        with self.assertRaises(ValueError):
            flash_seed({'a': b'\xff' * (SLOT_SIZE + 1)})


class LayoutConstantTests(unittest.TestCase):
    def test_constants_match_firmware_layout(self):
        self.assertEqual(SLOT_OFFSETS, {'a': 0x10000, 'b': 0x80000})
        self.assertEqual((SLOT_SIZE, MAX_PAYLOAD, APP_BASE),
                         (0x70000, 0x70000 - HEADER_SIZE, 0x10000))
        self.assertEqual((HEADER_SIZE, FLASH_SIZE, MIN_PAYLOAD), (32, 0x100000, 192))


if __name__ == '__main__':
    unittest.main(verbosity=2)
