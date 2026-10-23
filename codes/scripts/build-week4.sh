#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
cmake -S . -B build/week4 -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build/week4 --target bootloader-week4.elf application.elf --parallel
arm-none-eabi-objcopy -O binary build/week4/application.elf build/week4/application.bin
python3 scripts/pack-week4.py --app build/week4/application.bin --output build/week4/demo-flash.bin --force
arm-none-eabi-size build/week4/bootloader-week4.elf build/week4/application.elf
