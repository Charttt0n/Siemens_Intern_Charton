# 第一周：Cortex-M7 开发环境

目标：在 Apple Silicon macOS 上完成 GNU Arm GCC + CMake/Make → ELF → QEMU Cortex-M7 → GDB 的验证闭环。

## 当前状态

2026-10-12：工具安装、Cortex-M7 交叉编译、QEMU 运行、GDB 断点与单步均已通过。详见 [验证报告](docs/validation-report.md)。

## 第一周学习笔记

[工业 Bootloader 需求与 Cortex-M7 启动流程](docs/week1-learning-notes.md)：以大四实习生视角整理 S7 公开资料、工业需求、启动代码与调试证据，包含尚未实现的内容和第二周计划（2026-10-13）。

## 第一周复核与周报

2026-10-13 已完成逐项核查和增强验证：6 项检查通过，其中 4 个独立固件运行场景，包括启动前内存污染和两个预期失败用例。

- [查漏补缺及测试证据](docs/week1-audit.md)
- [第一周周报](docs/week1-report.md)
- [第一周周报 Word 版](docs/第一周实习周报.docx)

重新验证：`python3 scripts/verify-startup.py`。每次新建构建与证据目录，最新位置见 `logs/week1-review-latest.txt`，通过与否见对应 `summary.json`。

Git 已初始化为 `main`，并于 2026-10-13 配置本仓库作者身份 Charton。第一周源码、笔记、周报及验证记录随首次基线提交归档。周报中“尚无提交”的描述保留为复核时的状态，后续完成情况见 [Git 配置补记](docs/git-baseline.md)。

## 安装与验证

Homebrew 官方安装程序需要用户在 macOS 系统窗口中输入管理员密码。安装后：

```sh
cd '/Users/charton/Projects/西门子实习/codes'
bash scripts/install-tools.sh
source scripts/env.sh
python3 scripts/verify.py
```

预期固件输出（实际是否通过以 `logs/STATUS.txt` 为准）：

```
PASS: Cortex-M7; .data initialized; .bss zero; bare-metal C99
```

脚本检查 QEMU >= 7.0、Arm GCC >= 12.3、CMake >= 3.20，并记录 Make、Git、GDB 版本。验证包括真实交叉编译、Cortex-M7 CPUID 检查、.data/.bss 初始化检查，以及 GDB 断点、单步与变量读取。超时或失败将返回非零退出码。

## 日常使用

```sh
source scripts/env.sh
bash scripts/build.sh
bash scripts/run.sh
```

VS Code 可直接打开本目录，通过“终端 → 运行任务”构建、运行或验证；不依赖额外插件。

手动调试，在第一个终端运行：

```sh
bash scripts/run.sh -S -gdb tcp:127.0.0.1:3333
```

在另一个终端进入本目录并运行：

```sh
source scripts/env.sh
arm-none-eabi-gdb build/demo.elf
```

然后输入：

```gdb
target remote 127.0.0.1:3333
break main
continue
stepi
info registers
print initialized_data
```

## 目录

- `src/`：最小启动汇编、链接脚本、C99 验证程序。
- `cmake/`：Arm 裸机交叉编译配置。
- `scripts/`：安装、环境加载、构建、运行和验证脚本。
- `logs/`：实际工具版本、编译、QEMU 和 GDB 日志。
- `docs/`：环境说明。
- `downloads/`：官方安装文件，不纳入 Git。

## 本周边界

当前平台为 QEMU `mps2-an500`（Arm MPS2+ FPGA 平台的 Cortex-M7 模型），不是 SIMATIC S7 的硬件仿真。本例使用 semihosting 输出以验证工具链，不代表第二周 UART 驱动已完成。Semihosting 仅用于本地可信测试固件。

QEMU 自带 GDB server，直接连接 `arm-none-eabi-gdb`；本周无需 OpenOCD、PuTTY 或 Minicom。Doxygen、Mbed TLS 等在后续对应阶段引入。模拟平台的 SRAM 不能直接当作真实可擦写且持久化的 MCU Flash，后续 Flash/断电恢复需单独设计仿真存储层。

官方参考：
- https://www.qemu.org/docs/master/system/arm/mps2.html
- https://docs.brew.sh/Installation
- https://formulae.brew.sh/formula/arm-none-eabi-gcc
- https://formulae.brew.sh/formula/arm-none-eabi-gdb
