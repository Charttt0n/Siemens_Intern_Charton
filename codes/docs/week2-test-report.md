# 第二周启动与 UART 验收报告

执行日期：2026-10-14（Asia/Shanghai）。本报告记录实际执行结果，测试操作仅修改 QEMU 中的模拟内存或寄存器。

## 1. 结论及要求对应

第二周最终验收 **12 项检查全部 PASS**：包括模型时钟检查、全新构建检查、第一周回归，以及 9 个第二周固件运行场景。不是 12 个相互独立的固件功能。三次重复启动分别计为三项。

| 任务书要求 | 实现与证据 | 状态 |
|---|---|---|
| 汇编启动、向量表、栈和 C 入口 | 48 项向量表、MSP/PC/Thumb 位断言、main 断点 | 通过 |
| `.data` 复制与 `.bss` 清零 | 20 字节 data、36 字节 BSS 整段污染及恢复检查 | 通过；代码区是 SSRAM 的平台适配 |
| Cortex-M7 系统时钟 | 本机 qtree 的 CPU/UART 25 MHz 检查、板级频率接口 | 完成固定时钟模型适配；未实现真实 MCU 调频 |
| 基础 UART 与主机日志 | 无 semihosting 的 UART0 日志，寄存器读回 | 通过 |
| 启动与正常 C 环境 | 正常启动、自检、三个错误路径及 HardFault 场景 | 通过本报告所列用例 |
| 源码、启动日志、逐步报告与架构文档 | `src/week2/`、验证脚本、日志及本组文档 | 已提供 |

因此 Milestone 1 的 QEMU 最小启动目标已达到，但这一结论不包括真实 Flash、真实时钟切换、应用加载或工业认证。

## 2. 如何重现

```sh
cd '/Users/charton/projects/Siemens_Intern/codes'
python3 scripts/verify-week2.py
```

脚本依次记录工具版本、查询模型时钟、在独立目录构建两个目标、检查 ELF，再运行第一周回归和第二周场景。最终源文件 SHA-256 和 ELF SHA-256 均写入摘要。

本次最终证据：[summary.json](../logs/week2-20261014-003543/summary.json)。完整目录为 `logs/week2-20261014-003543/`，执行时间为 00:35:43 至 00:35:48（+08:00）。先前 `week2-20261014-003439` 为补充参数与时钟检查之前的首轮结果，不作为最终覆盖项清单。

工具：QEMU 11.1.2、GNU Arm GCC 16.2.0、GDB 18.1、CMake 4.4.4、GNU Make 4.4.1。以日志中的完整版本输出为准。

被测 ELF：`build/week2-8rmz65ry/bootloader.elf`。

SHA-256：`efeb1a4e9dd8055904d548a69e321f672dad385374065f3dab9fd066f04b9a94`。

构建目录不纳入 Git。重新构建的调试信息路径可能改变 ELF 哈希；比较时应结合源文件哈希和工具版本，不能要求不同构建目录的 ELF 必然逐字节相同。

## 3. 用例、步骤与实际结果

| 编号 | 场景与步骤 | 断言/实际观察 | 结果 |
|---|---|---|---|
| 1 | 启动暂停的 QEMU，执行 `info qtree` | CPU 时钟输入为 25 MHz，5 个 UART 的 pclk 为 25000000 | PASS |
| 2 | 全新 CMake 构建，查看段、符号和反汇编 | 第二周目标在 `-Wall -Wextra -Werror` 下通过；无 BKPT 指令 | PASS |
| 3 | 运行独立第一周 demo 目标 | 原 C 环境成功标记出现、QEMU 正常退出 | PASS |
| 4–6 | 三次启动独立第二周 QEMU 实例 | 每次 UART 都精确输出 144 字节，包含 CRLF | PASS × 3 |
| 7 | 复位处检查向量；data 填 CC，BSS 填 A5；末尾写哨兵；运行至 main 和 boot_idle | 20 字节 data 恢复；36 字节 BSS 清零；哨兵仍为 0x12345678；VTOR=0，CTRL=1，BAUDDIV=217，无 TX overrun | PASS |
| 8 | 在 main 入口通过 GDB 调用驱动，尝试无效配置、空指针和禁用 TX，再继续正常启动 | 无效调用返回 -1，非法初始化不修改已有寄存器；随后仍得到正常启动日志 | PASS |
| 9 | main 入口把 `runtime_data[4]` 改成 0xdeadbeef | boot_status=2；UART 输出 FAIL，无 PASS；共 134 字节 | PASS（预期失败被检测） |
| 10 | main 入口把 `runtime_bss[6]` 改成 0xdeadbeef | boot_status=2；UART 输出 FAIL，无 PASS；共 134 字节 | PASS（预期失败被检测） |
| 11 | 首次 uart_write 之前清除 UART CTRL | boot_status=3；不写出任何 UART 字节，不输出成功标记 | PASS（预期失败被检测） |
| 12 | 把模拟内存中 main 第一条指令改成 UDF #0 | 进入 fault_idle；exception_number=3，xPSR 异常号=3，即 HardFault | PASS |

第 8 项实际拒绝的参数为 `(clock, baud)`：`(0,115200)`、`(25000000,0)`、`(25000000,2000000)`、`(25000000,1)`。另验证有效 115200 配置得到分频值 217，以及 `uart_write(NULL)` 和 TX 未使能时的字符发送返回错误。

首轮测试后新增了模型时钟和参数边界两项，并修正调试记录中 PC 与向量入口值的区分，再执行了上述最终套件。没有把未执行的计划项标成 PASS。

## 4. 判定规则与证据组织

第二周固件会留在等待循环，不主动退出 QEMU。脚本在规定时间内收到预期串口字节后结束模拟器；涉及状态的场景还必须通过 GDB 断言。没有日志、日志不匹配、GDB 断言失败、异常退出或超时均判 FAIL。QEMU 被宿主结束后的退出码仅留档，不作为成功依据。

负向场景使用不同的预期输出；UART 禁用和异常场景必须先到达对应断点并验证失败状态，才接受零字节输出。这样“根本没运行”不能被误判成预期失败。

每个运行场景分别保存 `*-command.txt`、`*-uart.log` 和 `*-qemu.log`；调试场景另存 `.gdb` 脚本及 `*-gdb.txt`。串口输出与模拟器诊断分开；除宿主主动终止消息外，其他 QEMU 诊断均使测试失败。

关键原始记录：

- [模型时钟](../logs/week2-20261014-003543/model-clock.txt)
- [启动污染与寄存器检查](../logs/week2-20261014-003543/poisoned-startup-and-registers-gdb.txt)
- [参数边界](../logs/week2-20261014-003543/uart-parameter-boundaries-gdb.txt)
- [正常 UART 字节](../logs/week2-20261014-003543/uart-repeat-1-uart.log)
- [HardFault 注入](../logs/week2-20261014-003543/undefined-instruction-gdb.txt)

## 5. 大小与覆盖边界

实际段统计：text 968 字节（包含 192 字节向量表）、data 20 字节、`.bss` 36 字节、保留栈 4096 字节。GNU size 汇总显示 bss=4132，是 `.bss` 与 NOLOAD `.stack` 的合计，不能写成业务全局变量用了 4132 字节。

本次没有验证物理 UART 波形、波特率容差、串口后端持续阻塞、所有异常类型、启动途中异常、堆/栈溢出、任意段长度或空段。未实现 RX、中断 UART、Flash、应用跳转、签名、回滚、看门狗。测试覆盖当前最小启动目标，不能外推为工业可靠性或安全合规证明。
