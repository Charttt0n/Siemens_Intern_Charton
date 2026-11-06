#!/bin/bash
# Week-5 entry point: slot-aware bootloader, applications v1/v2/v3, packages.
# The Week-1..4 targets remain unchanged in the same CMake project.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
cmake -S . -B build/week5 -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build/week5 --target bootloader-week5.elf bootloader-week5-upgrade.elf application-v1.elf application-v2.elf application-v3.elf --parallel
arm-none-eabi-objcopy -O binary build/week5/application-v1.elf build/week5/application-v1.bin
arm-none-eabi-objcopy -O binary build/week5/application-v2.elf build/week5/application-v2.bin
python3 scripts/pack-week5.py --app build/week5/application-v1.bin --version 1 \
    --output build/week5/application-v1.pkg --force
python3 scripts/pack-week5.py --app build/week5/application-v2.bin --version 2 \
    --output build/week5/application-v2.pkg --force
arm-none-eabi-objcopy -O binary build/week5/application-v3.elf build/week5/application-v3.bin
python3 scripts/pack-week5.py --app build/week5/application-v3.bin --version 3 \
    --output build/week5/application-v3.pkg --force
arm-none-eabi-size build/week5/bootloader-week5.elf \
    build/week5/bootloader-week5-upgrade.elf \
    build/week5/application-v1.elf build/week5/application-v2.elf build/week5/application-v3.elf
