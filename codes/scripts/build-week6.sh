#!/bin/bash
# Week-6 entry point: signed bootloader and v1/v2/v3/hang applications.
# Week-1..5 CMake targets remain available in the same project.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
cmake -S . -B build/week6 -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build/week6 --target bootloader-week6.elf \
    application-week6-v1.elf application-week6-v2.elf \
    application-week6-v3.elf application-week6-hang.elf --parallel
for ver in v1 v2 v3 hang; do
    arm-none-eabi-objcopy -O binary build/week6/application-week6-${ver}.elf \
        build/week6/application-week6-${ver}.bin
done
python3 scripts/pack-week6.py --app build/week6/application-week6-v1.bin --version 1 \
    --output build/week6/application-week6-v1.pkg --force
python3 scripts/pack-week6.py --app build/week6/application-week6-v2.bin --version 2 \
    --output build/week6/application-week6-v2.pkg --force
python3 scripts/pack-week6.py --app build/week6/application-week6-v3.bin --version 3 \
    --output build/week6/application-week6-v3.pkg --force
python3 scripts/pack-week6.py --app build/week6/application-week6-hang.bin --version 9 \
    --output build/week6/application-week6-hang.pkg --force
arm-none-eabi-size build/week6/bootloader-week6.elf \
    build/week6/application-week6-v1.elf build/week6/application-week6-v2.elf \
    build/week6/application-week6-v3.elf build/week6/application-week6-hang.elf
