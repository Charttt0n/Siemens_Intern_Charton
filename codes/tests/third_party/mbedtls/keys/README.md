# Week 6 and Week 7 QEMU signing keys

From `codes/`, run `make verify-week7` or `make verify-week6`. The build
uses Python 3 and OpenSSL to provision an RSA-2048 test key and generate the
matching C public-key header under the ignored `build/test-signing/` directory.
The signing scripts use the same key. If a previous lab key is already present
as `test_rsa2048_private.pem` beside this file, it is used for compatibility.

Keep the local key to reuse signed packages between builds. If the key is
regenerated, rebuild the bootloader and sign fresh packages before testing.
These keys are for local QEMU testing only.
