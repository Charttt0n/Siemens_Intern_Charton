#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
# Ctrl-C stops this interactive demo; no semihosting is enabled.
exec qemu-system-arm -M mps2-an500 -cpu cortex-m7 -display none \
  -monitor none -serial stdio -kernel build/bootloader.elf "$@"
