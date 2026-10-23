"""Stage-4 image wire format. All fields are explicit little-endian uint32."""
import struct
import zlib
MAGIC = 0x4d49374d
HEADER_SIZE = 32
APP_BASE = 0x10000
APP_OFFSET = 0x10000
SLOT_SIZE = 0x70000
FLASH_SIZE = 0x100000


def package(payload, version=1):
    if not 192 <= len(payload) <= SLOT_SIZE - HEADER_SIZE:
        raise ValueError('application payload must fit slot and contain 48 vectors')
    if not 0 <= version <= 0xffffffff:
        raise ValueError('version must be uint32')
    vectors = struct.unpack_from('<48I', payload)
    if not 0x20010000 < vectors[0] <= 0x20200000 or vectors[0] % 8:
        raise ValueError('invalid application MSP')
    for i, entry in enumerate(vectors[1:], 1):
        if i in (7,8,9,10,13):
            if entry:
                raise ValueError('reserved vector must be zero')
        elif not (entry & 1) or not APP_BASE+192 <= (entry & ~1) < APP_BASE+len(payload):
            raise ValueError('application vector outside payload or not Thumb')
    header = struct.pack('<7I', MAGIC, 1, HEADER_SIZE, len(payload), APP_BASE,
                         version, zlib.crc32(payload))
    image = bytearray(b'\xff' * FLASH_SIZE)
    image[APP_OFFSET:APP_OFFSET+HEADER_SIZE+len(payload)] = header + struct.pack('<I', zlib.crc32(header)) + payload
    return bytes(image)
