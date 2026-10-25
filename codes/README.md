# Cortex-M7 工业 Bootloader 学习工程

目标：在 Apple Silicon macOS 上完成 GNU Arm GCC + CMake/Make → ELF → QEMU Cortex-M7 → GDB 的验证闭环。

## Week 1–4 分组交付（2026-10-22）

现有 QEMU 实现按规划书 Week 1–2、Week 3–4 分组交付。目录和脚本中的 week1 至 week4 是计划阶段编号。2026-10-22复验分别通过 6、12、11、23 项检查。2026-10-23的[全部计划核查](../planning/week1-4-plan-audit.md)确认功能已有证据，Week1指定手册学习及S7/工业要求研究仍有资料缺口；完整计划尚未全部闭环。

```sh
bash scripts/build-week4.sh
python3 scripts/run-week4.py
python3 scripts/verify-week4.py
```

完整演示已覆盖镜像头与 CRC32 校验、应用加载、再次校验、处理器状态交接、应用 C 初始化及 SVC 异常验证。CRC 不提供签名认证，存储仍使用显式宿主检查点。

- [Week3 驱动与存储周报](../reports/week3/week3-report.md)
- [Week4 固件校验与应用跳转周报](../reports/week4/week4-report.md)
- [Week3 Word提交版](../reports/week3/Week3实习周报.docx)
- [Week4 Word提交版](../reports/week4/Week4实习周报.docx)
- [分周交付总索引](../planning/week3-week4-delivery-index.md)
- [前四阶段交付说明与复现步骤](docs/week2-delivery.md)
- [交付说明 Word 版](docs/前四阶段交付说明.docx)
- [第四阶段技术指南](docs/week4-guide.md)
- [第四阶段测试报告](docs/week4-test-report.md)
- [实际周次与计划阶段进度](../planning/stage-progress.md)

## 计划第三阶段 UART 中断与存储管理（2026-10-19）

已新增独立第三周目标：UART 接收中断与环形缓冲区、1 MiB 模拟 NOR Flash 分区、读写擦除接口，以及宿主机检查点保存与重启恢复。第三周自动验收 11 项通过；第二周回归见最新测试报告。

```sh
bash scripts/build-week3.sh
python3 scripts/run-week3.py
# 自动验收，使用独立测试镜像：
python3 scripts/verify-week3.py
```

- [第三周架构、分区与操作指南](docs/week3-guide.md)
- [SIMATIC 存储职责与本项目内存布局对应依据](docs/simatic-memory-mapping.md)
- [第三周测试报告与覆盖边界](docs/week3-test-report.md)

持久化依赖显式保存检查点，尚不具备真实 Flash 掉电恢复能力。CRC 与应用跳转现已由第四阶段实现；升级协议仍待后续阶段推进。

## 第二周：最小 Bootloader（2026-10-14）

已新增汇编启动、完整 AN500 向量表、板级初始化与 CMSDK UART0 驱动，关闭 semihosting 仍能输出启动和 C 环境自检日志。最终 12 项检查通过，包括本机时钟核对、全新构建、第一周回归以及 9 个第二周固件运行场景。

```sh
bash scripts/build-week2.sh
bash scripts/run-week2.sh
# 看到启动日志后按 Ctrl-C 结束；自动验收使用：
python3 scripts/verify-week2.py
```

- [启动架构、操作指南与学习笔记](docs/week2-guide.md)
- [逐步测试报告与覆盖边界](docs/week2-test-report.md)
- [Week3 驱动与存储周报](../reports/week3/week3-report.md)
- [Week4 固件校验与应用跳转周报](../reports/week4/week4-report.md)
- [最终验收摘要](logs/week2-20261014-003543/summary.json)

时钟按 QEMU 固定 25 MHz 模型适配，代码区仍为模拟 SRAM。本阶段不包括真实 PLL 调频、Flash 擦写或应用跳转。第一周工具链说明及原示例入口保留如下。

## 第一周环境状态

2026-10-12：工具安装、Cortex-M7 交叉编译、QEMU 运行、GDB 断点与单步均已通过。详见 [验证报告](docs/validation-report.md)。

## 第一周学习笔记

[工业 Bootloader 需求与 Cortex-M7 启动流程](../notes/week1-learning-notes.md)：以大四实习生视角整理 S7 公开资料、工业需求、启动代码与调试证据，包含尚未实现的内容和第二周计划（2026-10-13）。

## 第一周复核与周报

2026-10-13 已完成逐项核查和增强验证：6 项检查通过，其中 4 个独立固件运行场景，包括启动前内存污染和两个预期失败用例。

- [查漏补缺及测试证据](docs/week1-audit.md)
- [Week 1–2 周报](../reports/week1-2/week1-2-report.md)
- [Week 1–2 周报 Word 版](../reports/week1-2/Week1-2实习周报.docx)

重新验证：`python3 scripts/verify-startup.py`。每次新建构建与证据目录，最新位置见 `logs/week1-review-latest.txt`，通过与否见对应 `summary.json`。

Git 已初始化为 `main`，并于 2026-10-13 配置本仓库作者身份 Charton。第一周源码、笔记、周报及验证记录随首次基线提交归档。周报中“尚无提交”的描述保留为复核时的状态，后续完成情况见 [Git 配置补记](docs/git-baseline.md)。

## 安装与验证

Homebrew 官方安装程序需要用户在 macOS 系统窗口中输入管理员密码。安装后：

```sh
cd '/Users/charton/projects/Siemens_Intern/codes'
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

## 第一周示例边界

当前平台为 QEMU `mps2-an500`（Arm MPS2+ FPGA 平台的 Cortex-M7 模型），不是 SIMATIC S7 的硬件仿真。第一周 `demo.elf` 使用 semihosting 输出以验证工具链；第二周 `bootloader.elf` 已实现独立 UART 输出，两个目标的运行入口分开。Semihosting 仅用于本地可信测试固件。

QEMU 自带 GDB server，直接连接 `arm-none-eabi-gdb`；本周无需 OpenOCD、PuTTY 或 Minicom。Doxygen、Mbed TLS 等在后续对应阶段引入。模拟平台的 SRAM 不能直接当作真实可擦写且持久化的 MCU Flash，后续 Flash/断电恢复需单独设计仿真存储层。

官方参考：
- https://www.qemu.org/docs/master/system/arm/mps2.html
- https://docs.brew.sh/Installation
- https://formulae.brew.sh/formula/arm-none-eabi-gcc
- https://formulae.brew.sh/formula/arm-none-eabi-gdb

## 2026-10-24 老师反馈修订

在codes执行`make verify-all`重新验收；逐阶段使用`make verify-week1`至`make verify-week4`，反馈补测使用`make verify-feedback`。用例明细见docs/week1-test-report.md至week4-test-report.md及feedback-test-report.md，技术图示与格式见docs/teacher-feedback-technical.md。交付清单使用schemas/delivery-manifest.schema.json；生成及核验命令见技术说明。
