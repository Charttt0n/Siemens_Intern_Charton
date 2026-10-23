#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
cmake -S . -B build/week3 -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build/week3 --target bootloader-week3.elf --parallel
arm-none-eabi-size build/week3/bootloader-week3.elf
