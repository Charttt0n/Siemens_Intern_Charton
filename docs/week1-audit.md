# 第一周任务核查与补充验证

核查日期：2026-10-13。范围依据任务书第 2 页第一周 Key Tasks 和第 3 页 Weekly Deliverables。原始环境搭建记录保留在 [验证报告](validation-report.md)，本次不覆盖或替换首次实验日志。

## 任务与交付逐项核查

| 任务书要求 | 已有证据 | 本次补充 | 结论与边界 |
|---|---|---|---|
| 安装 QEMU、GNU Arm、CMake、Make、GDB、Git | 版本日志及构建脚本 | 重新检查 6 个工具；最低版本约束通过 | 完成第一周明确列出的工具链 |
| 验证 Cortex-M7 并运行最小裸机程序 | CPUID 检查、正常输出和 GDB 日志 | 新构建目录编译，正常启动、初始化污染、两个负向场景 | 本例端到端兼容性通过 |
| 调研 S7 启动与工业要求 | S7、IEC 官方资料链接和需求表 | 核对 Secure Boot 与签名更新正文；修正装载存储器类比 | 公开功能和分层调研完成；具体 S7 内部引导实现仍需型号资料 |
| 学习向量表、内存和硬件初始化 | 启动汇编、链接脚本与 ELF 数值解读 | 实测复位 MSP/PC、复制/清零和一处末尾哨兵；补充初始化前提 | 对当前示例有实证；时钟树、外设及完整中断初始化未实现 |
| 环境与 QEMU 运行报告 | `validation-report.md` 与原始日志 | 增加本次复核入口和独立证据目录 | 完成 |
| 工业需求与启动流程技术笔记 | `week1-learning-notes.md` | 更新已执行实验、架构、来源和待办状态 | 完成当前阶段技术笔记 |
| Git 初始化与标准目录 | `main` 分支、src/cmake/scripts/docs/logs | 核查 Git 和目录，补充测试入口 | 初始化要求满足；尚无提交，作者身份未配置 |

任务书总技术栈还列出 OpenOCD、串口终端、Doxygen 和 Mbed TLS；第一周 Key Tasks 的安装清单只明确列出上表六项。本次沿用 QEMU 内置 gdbstub。未把后续工具或 UART、Flash、签名验证、应用跳转提前写成已完成，也没有把它们当作第一周最小示例必须具备的功能。

## 本次补齐的缺口

| 缺口 | 处理 | 证据 |
|---|---|---|
| `.bss` 初值可能使正常测试产生假阳性 | 复位时污染整个 `.bss`，进入 `main` 前检查全部字节 | `poisoned-startup-gdb.txt` |
| 未验证复制目的区覆盖和一处边界位置 | 同时污染 `.data`，在 `_ebss` 后放 4 字节哨兵 | 同上 |
| 只有成功路径，失败输出未实测 | 分别破坏两个 C 检查变量，要求 FAIL、结果 0xdead、退出码 1 | 两组 `corrupt-*-gdb.txt` 与 `corrupt-*-qemu.txt` |
| 可能构建新产物却运行旧 ELF | 新建独立构建目录，QEMU/GDB 显式使用同一路径 | `summary.json` 与各命令日志 |
| 资料曾只依赖 Secure Boot 摘要 | 复核官方完整页面并区分更新时验证、启动时验证与 Startup OB | 笔记第 3 节与官方链接 |
| 缺少可交付周报 | 写出实际架构、实验推理、结果边界和下一步验收条件 | `week1-report.md` 与 Word 版 |

## 本次运行结果

执行命令：`python3 scripts/verify-startup.py`。时间：2026-10-13 12:59（Asia/Shanghai）。证据目录：[week1-review-20261013-125900](../logs/week1-review-20261013-125900/summary.json)。

| 检查 ID | 检查内容 | 预期 | 实际结果 |
|---|---|---|---|
| W1-01 | 六个工具及最低版本 | 工具可用、版本达标 | PASS |
| W1-02 | 新目录编译链接与 ELF 检查 | 构建成功，使用同一产物测试 | PASS；text 368、data 4、bss 8 字节 |
| W1-03 | 正常启动 | 固件 PASS，QEMU 退出 0 | PASS |
| W1-04 | 复位契约与污染后启动 | MSP/PC/向量一致，恢复数据、清零 BSS、保留哨兵 | PASS；单步 PC 0x44 → 0x46 |
| W1-05 | main 入口破坏 initialized_data | 固件 FAIL，结果 0xdead，退出 1 | PASS，符合预期失败 |
| W1-06 | main 入口破坏 zero_data | 固件 FAIL，结果 0xdead，退出 1 | PASS，符合预期失败 |

共 6 项检查，其中 W1-03 至 W1-06 为 4 个独立固件运行场景。负向场景中的退出码 1 是通过条件；这不表示整个测试脚本失败。本次脚本最终退出 0。

关键证据：

```text
RESET_MSP=0x20400000 RESET_PC=0xc4 RESET_VECTOR=0xc5
INJECTED_DATA=cccccccc
INJECTED_BSS=a5a5a5a5a5a5a5a5
RESTORED_DATA=78563412
CLEARED_BSS=0000000000000000
UPPER_SENTINEL=0x12345678
STEP_PC=0x46
```

新 ELF：`build/week1-review-t6n_xpv9/demo.elf`。SHA-256：`7fae7eb4bd0e272693b3923f2bdb2a8c7f26b9896e0bd9f721f93bfbe6865ffe`。源码和验证脚本哈希一并记录在 `summary.json`，便于确认测试对应的版本；这些哈希不能替代 Git 历史。

## 如何重做和定位失败

```sh
cd '/Users/charton/Projects/西门子实习/codes'
source scripts/env.sh
python3 scripts/verify-startup.py
```

每次运行创建新的构建目录及 `logs/week1-review-时间戳`，不会覆盖本次报告引用的证据。`logs/week1-review-latest.txt` 指向最近一次执行目录；打开其中 `summary.json` 查看最终状态，不能仅凭目录存在判断通过。

如果失败，先看汇总中的 `error`，再查对应命令输出和 GDB 脚本。超时、工具缺失或断言失败均使脚本返回非零；不把无输出的停机当作正常运行。

## 尚需保留的限制

- 实验覆盖当前 4 字节 `.data` 与 8 字节 `.bss`；不覆盖空段、大段、不同对齐或全部链接布局。
- 哨兵仅验证 `.bss` 紧邻末尾的 4 字节没有被修改，不是完整内存越界检测。
- 两个负向场景只验证示例的错误识别和失败退出，不验证升级回退。
- 向量表尚未实现完整外部中断表，默认异常处理仍为停留循环。
- 尚无 UART、持久化 Flash、升级状态机、签名验证和独立应用跳转测试。
- 工程没有 Git 提交，且未配置 `user.name`、`user.email`。未代填作者身份或创建远程仓库；第一周初始化交付已满足，后续形成基线提交时需要真实作者配置。

## 第二周的可验收入口

第二周先整理启动和异常入口，再实现模拟 UART 驱动。验收不只看屏幕有文字：应在关闭 semihosting 的运行配置中，经 UART 输出启动阶段与结果；再证明异常停机能通过寄存器和日志定位。Flash 持久化和双镜像升级留在对应周次实现。
