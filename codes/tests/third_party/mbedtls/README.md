# Mbed TLS 3.6.7 (pruned, firmware verify-only)

Vendored for Week-6 SHA-256 + RSA-2048 signature verification.

- Upstream: https://github.com/Mbed-TLS/mbedtls
- Official release: `mbedtls-3.6.7.tar.bz2`
  https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
- SHA256: `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`
- Licence: Apache-2.0 OR GPL-2.0-or-later; see `LICENSE`.
- Vendor date: 2026-11-10.
- Mbed TLS 3.6 is an LTS branch (fixes at least until March 2027).

Only the translation units required for SHA-256 and RSA PKCS#1 v1.5 verify
are copied, plus the corresponding public and private headers. TLS, X.509,
PSA crypto, entropy and key generation are not compiled into the bootloader.
The firmware uses `codes/src/week6/week6_mbedtls_config.h` via
`MBEDTLS_CONFIG_FILE`. Board UART, Flash and watchdog drivers stay outside
this tree.

Do not replace these sources with an unverified snapshot. If the library is
updated, record a new official tarball checksum here.
