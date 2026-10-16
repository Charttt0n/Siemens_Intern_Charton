# 第一周环境搭建与验证报告

验证日期：2026-10-12（Asia/Shanghai）

项目：`/Users/charton/projects/Siemens_Intern/codes`

结论：第一周软件安装与开发环境验证通过。本文仅覆盖环境和最小示例，不代表整个 bootloader 或第一周工业需求调研已完成。

## 主机和工具

主机：Apple Silicon（arm64），macOS 26.6.2。已存在 Xcode Command Line Tools、VS Code 和 Git。通过官方 Homebrew 安装并配置以下工具：

| 工具 | 实际版本 | 任务最低版本 | 结果 |
|---|---|---|---|
| QEMU | 11.1.2 | 7.0 | 通过 |
| GNU Arm GCC | 16.2.0 | 12.3 | 通过 |
| Arm GDB | 18.1 | 未指定 | 通过 |
| CMake | 4.4.4 | 3.20 | 通过 |
| GNU Make | 4.4.1 | 未指定 | 通过 |
| Git（原有） | 2.54.0 | 未指定 | 通过 |

Homebrew 版本：7.0.9。GNU Make 通过 `scripts/env.sh` 加入工程会话的 PATH；无需修改个人 shell 配置。

## 实际执行结果

验证命令：`python3 scripts/verify.py`，退出码 0。

1. 版本检查通过，QEMU 列出 `mps2-an500` Cortex-M7 平台。
2. CMake 使用 `arm-none-eabi-gcc`，以 `-mcpu=cortex-m7 -mthumb -mfloat-abi=soft` 编译 C99 与启动汇编，成功链接 `build/demo.elf`。
3. `arm-none-eabi-size`：text 368 bytes、data 4 bytes、bss 8 bytes，总计 380 bytes（不包含预留栈容量，也不是 ELF 文件大小）。
4. QEMU 真实运行固件并正常退出，输出：

```
PASS: Cortex-M7; .data initialized; .bss zero; bare-metal C99
```

5. GDB 在 `main` 命中断点，完成一条机器指令单步，再在 semihosting 输出前停止，读取到：

```
DEMO_RESULT=0x600d
pc = 0x40
sp = 0x203ffff0
```

## 可核查日志

- `logs/STATUS.txt`：总体验证结论。
- `logs/versions.txt`：工具实际版本。
- `logs/build.txt`：CMake、编译、链接和尺寸输出。
- `logs/qemu-run.txt`：QEMU 运行结果。
- `logs/gdb.txt`：断点、单步、寄存器与变量读取结果。
- `logs/qemu-machines.txt`：仿真平台清单。

Git 仓库已初始化为 `main` 分支，尚未创建提交或配置远程仓库。

## 使用入口与边界

日常使用见根目录 `README.md`，平台启动和内存说明见 `docs/platform.md`。

本例的输出采用 semihosting，UART 驱动和后续 bootloader 功能尚未实现。QEMU 自带 GDB server，第一周无需 OpenOCD；原任务技术栈中的 OpenOCD、串口终端、Doxygen 和 Mbed TLS 未在本阶段安装。后续按实际硬件或开发阶段引入。

## 2026年10月13日复核

上述为首次验证记录，予以保留。本次在全新构建目录重新验证，并增加启动前污染 `.data`/`.bss`、末尾哨兵、两个 C 检查变量的负向用例。共 6 项检查通过，其中 4 个固件运行场景；负向场景的固件退出码 1 为预期结果。

详细过程见 [查漏补缺记录](week1-audit.md)，本次机器可读结果见 [检查汇总](../logs/week1-review-20261013-125900/summary.json)。这些测试不代表 UART、升级回退、持久化 Flash 或签名功能已经实现。
