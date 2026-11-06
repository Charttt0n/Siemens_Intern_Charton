"""Week-5 single-firmware upgrade package.

Wire format: the existing 32-byte Week-4 image header, immediately followed by
the raw application payload. A package is exactly ``32 + len(payload)`` bytes.

It is *not* a flash dump: the 1 MiB NOR model, its 0xFF padding and the
bootloader region are host-side lab artifacts, and an ELF file is never a valid
payload. Validation here mirrors ``image_validate()`` in ``src/week4/image.c``
so the host and the firmware reject the same byte streams with the same codes.
"""
import struct
import zlib
from image_format import APP_BASE, APP_OFFSET, FLASH_SIZE, HEADER_SIZE, MAGIC, SLOT_SIZE

FORMAT = 1
MIN_PAYLOAD = 192  # 48 AN500 vector entries
MAX_PAYLOAD = SLOT_SIZE - HEADER_SIZE
SLOT_OFFSETS = {"a": APP_OFFSET, "b": 0x80000}

# Same numeric values as image_result in src/week4/image.h.
FORMAT_ERROR, HEADER_CRC, LENGTH, ADDRESS, PAYLOAD_CRC, VECTOR = 2, 3, 4, 5, 6, 7


class PackageError(ValueError):
    """Rejected package or payload; ``code`` matches the firmware result code."""

    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


class Package:
    """Parsed package header plus payload."""

    __slots__ = ("version", "load", "crc", "payload")

    def __init__(self, version, load, crc, payload):
        self.version = version
        self.load = load
        self.crc = crc
        self.payload = payload

    @property
    def size(self):
        return len(self.payload)

    def __repr__(self):
        return (f"Package(version={self.version}, size={self.size}, "
                f"load=0x{self.load:08x}, crc=0x{self.crc:08x})")


def _le32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def _vectors_ok(payload):
    """Mirror of vectors_ok() in src/week4/image.c."""
    size = len(payload)
    if (msp := _le32(payload, 0)) <= 0x20010000 or msp > 0x20200000 or msp % 8:
        return False
    for index in range(1, 48):
        entry = _le32(payload, 4 * index)
        if index in (7, 8, 9, 10, 13):
            if entry:
                return False
        elif not (entry & 1) or not APP_BASE + MIN_PAYLOAD <= (entry & ~1) <= APP_BASE + size - 2:
            return False
    return True


def build_header(payload, version=1):
    """Return the 32-byte header for ``payload``; raise PackageError if invalid."""
    if not 0 <= version <= 0xFFFFFFFF:
        raise ValueError("version must be a uint32")
    size = len(payload)
    if not MIN_PAYLOAD <= size <= MAX_PAYLOAD:
        raise PackageError(
            LENGTH, f"payload length {size} outside {MIN_PAYLOAD}..{MAX_PAYLOAD}")
    if not _vectors_ok(payload):
        raise PackageError(VECTOR, "payload vector table is invalid")
    header = struct.pack("<7I", MAGIC, FORMAT, HEADER_SIZE, size, APP_BASE,
                         version, zlib.crc32(payload))
    return header + struct.pack("<I", zlib.crc32(header))


def build_package(payload, version=1):
    """Return ``header + payload``; the result is exactly 32 + len(payload)."""
    payload = bytes(payload)
    return build_header(payload, version) + payload


def parse_package(data, capacity=MAX_PAYLOAD):
    """Validate and split a package. ``capacity`` is the receiving slot limit."""
    data = bytes(data)
    if len(data) < HEADER_SIZE:
        raise PackageError(LENGTH, f"{len(data)} bytes is shorter than the header")
    header, payload = data[:HEADER_SIZE], data[HEADER_SIZE:]
    if (_le32(header, 0) != MAGIC or _le32(header, 4) != FORMAT
            or _le32(header, 8) != HEADER_SIZE):
        raise PackageError(FORMAT_ERROR, "magic, format or header size mismatch")
    if zlib.crc32(header[:28]) != _le32(header, 28):
        raise PackageError(HEADER_CRC, "header CRC32 mismatch")
    size = _le32(header, 12)
    if not MIN_PAYLOAD <= size <= MAX_PAYLOAD:
        raise PackageError(LENGTH, f"declared payload length {size} outside slot")
    if size > capacity:
        raise PackageError(LENGTH, f"declared payload length {size} exceeds {capacity}")
    if size != len(payload):
        raise PackageError(
            LENGTH, f"file is 32 + {len(payload)} bytes but the header declares 32 + {size}")
    load = _le32(header, 16)
    if load != APP_BASE:
        raise PackageError(ADDRESS, f"load address 0x{load:08x} is not 0x{APP_BASE:08x}")
    crc = _le32(header, 24)
    if zlib.crc32(payload) != crc:
        raise PackageError(PAYLOAD_CRC, "payload CRC32 mismatch")
    if not _vectors_ok(payload):
        raise PackageError(VECTOR, "payload vector table is invalid")
    return Package(_le32(header, 20), load, crc, payload)


def flash_seed(packages):
    """Return a 1 MiB seed image with ``{slot: package}`` written in place.

    Byte streams are copied verbatim so tests can seed deliberately corrupt
    slots. This creates QEMU lab input only; it is never an upgrade package.
    """
    image = bytearray(b"\xff" * FLASH_SIZE)
    for name, data in packages.items():
        if name not in SLOT_OFFSETS:
            raise ValueError(f"unknown slot {name!r}")
        if len(data) > SLOT_SIZE:
            raise ValueError(f"package for slot {name} exceeds the slot size")
        start = SLOT_OFFSETS[name]
        image[start:start + len(data)] = data
    return bytes(image)
