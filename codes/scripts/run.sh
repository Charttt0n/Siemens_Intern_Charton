#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
exec qemu-system-arm -M mps2-an500 -cpu cortex-m7 -nographic -monitor none -serial none -semihosting-config enable=on,target=native -kernel build/demo.elf "$@"
