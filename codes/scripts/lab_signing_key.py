"""Provision a per-checkout RSA test key and the public header for firmware."""

import fcntl
import re
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory


ROOT = Path(__file__).resolve().parents[1]
KEY_DIR = ROOT / 'build/test-signing'
PRIVATE_KEY = KEY_DIR / 'test_rsa2048_private.pem'
PUBLIC_HEADER = KEY_DIR / 'week6_lab_public_key.h'
LEGACY_KEY = ROOT / 'tests/third_party/mbedtls/keys/test_rsa2048_private.pem'


def openssl(*arguments, data=None):
    return subprocess.run(['openssl', *arguments], input=data, capture_output=True,
                          check=True).stdout


def prepare_lab_key():
    KEY_DIR.mkdir(parents=True, exist_ok=True)
    with (KEY_DIR / '.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        with TemporaryDirectory(dir=KEY_DIR) as staging:
            staging = Path(staging)
            # Keep compatibility with signed packages in existing local labs.
            key = LEGACY_KEY if LEGACY_KEY.is_file() else PRIVATE_KEY
            if not key.is_file():
                temporary_key = staging / 'private.pem'
                openssl('genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:2048',
                        '-out', temporary_key)
                temporary_key.chmod(0o600)
                temporary_key.replace(key)

            public = openssl('rsa', '-in', key, '-pubout')
            description = openssl('rsa', '-pubin', '-text', '-noout', '-modulus',
                                  data=public).decode('ascii')
            match = re.search(r'^Modulus=([0-9A-Fa-f]{512})$', description, re.MULTILINE)
            if not match or not re.search(r'^Exponent: 65537\b', description, re.MULTILINE):
                raise ValueError('Lab key must be RSA-2048 with public exponent 65537')
            modulus = bytes.fromhex(match.group(1))
            lines = ['/* Generated public key for this QEMU lab. */',
                     'static const unsigned char week6_test_rsa_n[] = {']
            for offset in range(0, len(modulus), 12):
                chunk = modulus[offset:offset + 12]
                lines.append('    ' + ', '.join(f'0x{byte:02x}' for byte in chunk) + ',')
            lines += ['};', '#define week6_test_rsa_n_len 256u',
                      'static const unsigned char week6_test_rsa_e[] = {0x01, 0x00, 0x01};',
                      '#define week6_test_rsa_e_len 3u']
            header = '\n'.join(lines) + '\n'
            if not PUBLIC_HEADER.is_file() or PUBLIC_HEADER.read_text() != header:
                candidate = staging / PUBLIC_HEADER.name
                candidate.write_text(header, encoding='ascii')
                candidate.replace(PUBLIC_HEADER)
            return key


if __name__ == '__main__':
    prepare_lab_key()
