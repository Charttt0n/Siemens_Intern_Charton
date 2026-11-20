#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
build="${1:-build/week7}"
cmake -S . -B "$build" -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build "$build" --target bootloader-week7.elf bootloader-week7-debug.elf \
    bootloader-week7-fault.elf bootloader-week6.elf \
    application-week7-v1.elf application-week7-v2.elf \
    application-week7-v3.elf application-week7-hang.elf --parallel
for ver in v1 v2 v3 hang; do
    arm-none-eabi-objcopy -O binary "$build/application-week7-${ver}.elf" "$build/application-week7-${ver}.bin"
    case "$ver" in v1) version=1;; v2) version=2;; v3) version=3;; hang) version=9;; esac
    python3 scripts/pack-week6.py --app "$build/application-week7-${ver}.bin" --version "$version" \
        --output "$build/application-week7-${ver}.pkg" --force
done
arm-none-eabi-size "$build"/bootloader-week7.elf "$build"/bootloader-week6.elf
