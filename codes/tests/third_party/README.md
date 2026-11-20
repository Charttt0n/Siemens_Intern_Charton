# Third-party host YMODEM sender (test-only)

These sources are vendored unmodified for interoperability testing and the
Week-5 host upload CLI. They are **not** firmware code and are not linked
into any target; `scripts/week5_session.py` and `scripts/verify-week5.py` add
this directory to `sys.path`. No network install or serial dependency is needed
for the QEMU UNIX socket channel.

## ymodem 1.5.3

- Upstream: https://github.com/alexwoo1900/ymodem
- PyPI sdist: `ymodem-1.5.3.tar.gz`,
  sha256 `2e53628d9f538abcac93bc9ffb72919df7467b026646f755d8cf05c5d552289d`
- Licence: MIT, Copyright (c) 2018 Alex Woo; see `LICENSE.ymodem`.
- Vendor date: 2026-10-31.
- Copied unmodified: `ymodem/__init__.py`, `CRC.py`, `Platform.py`,
  `Protocol.py`, `Socket.py`, `Version.py`.
- Omitted: `ymodem/__main__.py` (it imports pyserial, which this harness does
  not use) and the bundled documentation/PDF. The protocol reference used for
  the C receiver is the YMODEM batch protocol description by Chuck Forsberg
  distributed with this package.
- Rationale: the Week-5 planning document names this implementation as the
  host-side YMODEM reference. Using it as the sender keeps receiver
  interoperability independent from the C receiver's own test code.

## ordered-set 4.1.0

- Upstream: https://github.com/rspeer/ordered-set
- PyPI sdist: `ordered-set-4.1.0.tar.gz`,
  sha256 `694a8e44c87657c59292ede72891eb91d34131f6531463aab3009191c77364a8`
- Licence: MIT, Copyright (c) 2012-2022 Elia Robyn Lake; see
  `LICENSE.ordered-set`.
- Copied unmodified: `ordered_set/__init__.py` (runtime dependency of
  `ymodem/Protocol.py`).

## Receiver provenance

The C receiver in `src/week5/ymodem.c` is an independent implementation, not a
copy of these sources. No C YMODEM receiver with a compatible licence and a
hardware-independent byte/clock interface was present in the workspace (lrzsz
is GPL and tty-oriented; MCU vendor IAP examples are HAL-specific), so the
protocol behaviour was derived from:

- Chuck Forsberg, *YMODEM Protocol reference* (batch transmission, block 0
  name/length fields, EOT/terminator handshake), as distributed with ymodem
  1.5.3 above.
- The observable behaviour of the pinned ymodem 1.5.3 sender, used only as an
  interoperability peer and covered by its MIT licence.

## Mbed TLS 3.6.7 (Week 6 firmware verify-only)

Pruned official release used by the Week-6 bootloader for SHA-256 and
RSA-2048 PKCS#1 v1.5 verification. Provenance, licence and the exact file
set are in `mbedtls/README.md`. The Week 6 and Week 7 bootloaders use this
library. See [local lab signing](mbedtls/keys/README.md) for key setup.

## Week 7 Unity

`unity/` 固定为官方 [ThrowTheSwitch Unity v2.6.1](https://github.com/ThrowTheSwitch/Unity/tree/v2.6.1)，MIT 许可证。保留原始 src/unity.c、unity.h、unity_internals.h 和 LICENSE.txt；来源及每文件 SHA256 见 `unity/SOURCE.json`。不在生产 Bootloader 中链接 Unity，宿主与 QEMU 单元测试分别构建。
