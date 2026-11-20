"""Week-6 signed firmware package: Week-5 image plus a 272-byte RSA trailer.

The 32-byte Week-5/Week-4 header is unchanged. A valid Week-6 file is exactly
``32 + payload + 272`` bytes. SHA-256 is computed over header||payload and
signed with RSA-2048 PKCS#1 v1.5. Test keys only; not for production.
"""
import struct
import subprocess
import zlib
from pathlib import Path
from lab_signing_key import prepare_lab_key
from week5_package import (APP_BASE, FLASH_SIZE, HEADER_SIZE, MIN_PAYLOAD,
                           Package, PackageError, SLOT_OFFSETS, SLOT_SIZE,
                           ADDRESS, FORMAT_ERROR, HEADER_CRC, LENGTH,
                           PAYLOAD_CRC, VECTOR, build_package, parse_package)

SIG_MAGIC = 0x4753364D  # "M6SG"
SIG_TYPE = 1
SIG_LENGTH = 256
TRAILER_SIZE = 16 + SIG_LENGTH
MAX_SIGNED_PAYLOAD = SLOT_SIZE - HEADER_SIZE - TRAILER_SIZE
UNSIGNED, TRAILER, TYPE, SIGNATURE = 11, 10, 12, 14


class SignedPackage(Package):
    __slots__ = ("version", "load", "crc", "payload", "signature")

    def __init__(self, version, load, crc, payload, signature):
        super().__init__(version, load, crc, payload)
        self.signature = signature


def _le32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def sign_digest_file(body, key=None):
    key = prepare_lab_key() if key is None else Path(key)
    if not key.is_file():
        raise FileNotFoundError(f'test private key missing: {key}')
    process = subprocess.run(
        ['openssl', 'dgst', '-sha256', '-sign', str(key)],
        input=body, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if process.returncode:
        raise RuntimeError(process.stderr.decode(errors='replace') or 'openssl sign failed')
    signature = process.stdout
    if len(signature) != SIG_LENGTH:
        raise RuntimeError(f'unexpected signature length {len(signature)}')
    return signature


def build_trailer(signature):
    if len(signature) != SIG_LENGTH:
        raise ValueError('signature must be 256 bytes')
    return struct.pack('<4I', SIG_MAGIC, SIG_TYPE, SIG_LENGTH, 0) + bytes(signature)


def build_signed_package(payload, version=1, key=None):
    payload = bytes(payload)
    if len(payload) > MAX_SIGNED_PAYLOAD:
        raise PackageError(LENGTH, f'payload {len(payload)} leaves no room for the signature trailer')
    body = build_package(payload, version)
    return body + build_trailer(sign_digest_file(body, key))


def parse_signed_package(data, capacity=MAX_SIGNED_PAYLOAD, require_signature=True):
    data = bytes(data)
    if len(data) < HEADER_SIZE:
        raise PackageError(LENGTH, 'shorter than the image header')
    if len(data) >= HEADER_SIZE + TRAILER_SIZE:
        trailer = data[-TRAILER_SIZE:]
        body = data[:-TRAILER_SIZE]
        if _le32(trailer, 0) == SIG_MAGIC:
            if _le32(trailer, 4) != SIG_TYPE or _le32(trailer, 8) != SIG_LENGTH or _le32(trailer, 12) != 0:
                raise PackageError(TYPE, 'unsupported or malformed signature trailer')
            parsed = parse_package(body, capacity=capacity)
            if len(body) != HEADER_SIZE + parsed.size:
                raise PackageError(LENGTH, 'signed file length does not match the header')
            return SignedPackage(parsed.version, parsed.load, parsed.crc, parsed.payload, trailer[16:])
    if require_signature:
        try:
            parse_package(data, capacity=SLOT_SIZE - HEADER_SIZE)
        except PackageError:
            raise
        raise PackageError(UNSIGNED, 'Week-5 unsigned package has no signature trailer')
    return parse_package(data, capacity=capacity)


def flash_seed(packages):
    image = bytearray(b'\xff' * FLASH_SIZE)
    for name, data in packages.items():
        if name not in SLOT_OFFSETS:
            raise ValueError(f'unknown slot {name!r}')
        if len(data) > SLOT_SIZE:
            raise ValueError(f'package for slot {name} exceeds the slot size')
        start = SLOT_OFFSETS[name]
        image[start:start + len(data)] = data
    return bytes(image)
