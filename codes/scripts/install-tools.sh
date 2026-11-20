#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
if ! command -v brew >/dev/null; then
  echo '未找到 Homebrew。请将已有工具链加入 PATH，或参考 https://brew.sh 安装 Homebrew 后重新运行。'
  exit 1
fi
brew install qemu arm-none-eabi-gcc arm-none-eabi-gdb cmake make
# QEMU 使用自己的 GDB server；第一周不需要 OpenOCD。
