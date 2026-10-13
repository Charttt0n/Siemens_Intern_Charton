#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
cmake -S . -B build -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build --target bootloader.elf --parallel
arm-none-eabi-size build/bootloader.elf
