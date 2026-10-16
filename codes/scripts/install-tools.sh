#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
if ! command -v brew >/dev/null; then
  echo 'Homebrew 尚未安装。请在终端运行： /bin/bash downloads/homebrew-install.sh'
  exit 1
fi
brew install qemu arm-none-eabi-gcc arm-none-eabi-gdb cmake make
# QEMU 使用自己的 GDB server；第一周不需要 OpenOCD。
