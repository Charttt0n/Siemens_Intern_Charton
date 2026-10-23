# 第一周学习笔记 工业 Bootloader 与 Cortex-M7 启动流程

初次整理：2026 年 10 月 13 日  
更新日期：2026 年 10 月 16 日  
开发环境：MacBook + QEMU mps2-an500 + GNU Arm 工具链

## 1. 阶段学习目标

第一周主要完成开发环境搭建和最小裸机程序验证，并结合代码梳理处理器从复位进入 main 的过程，为后续固件升级、镜像校验和故障恢复建立可运行、可调试的工程基础。

本阶段重点学习复位入口与执行顺序、全局数据初始化、启动结果验证，以及工业设备升级失败后的恢复需求。

本笔记以第一周 Demo 和测试记录为基础，补充 UART 与 Git 的后续进展，并区分实验结果、资料分析和待实现方案。

## 2. 开发工具及职责

| 工具 | 主要作用 |
|---|---|
| VS Code | 编辑代码、查看工程，也可以作为构建和调试的操作入口 |
| Arm GCC / Binutils | 编译 C 和汇编，链接生成 ELF，查看符号和反汇编 |
| CMake 和 Make | CMake 生成构建规则，Make 按规则执行编译和链接 |
| QEMU | 在 Mac 上模拟开发板，运行编译好的 ARM 固件 |
| GDB | 连接模拟器，设置断点、单步执行，查看寄存器和内存 |
| Git 和 GitHub | Git 保存修改历史，GitHub 保存远程仓库，便于同步和查看 |

当前项目通过终端运行 QEMU，以无图形窗口的方式模拟目标板，并在终端显示串口日志。VS Code 提供代码编辑入口，QEMU 负责固件仿真执行。

Mac 与 Cortex-M7 的目标指令、运行环境和内存布局不同，因此本项目使用交叉编译工具链生成目标固件。

10 月 12 日的验证记录中，工具版本为 QEMU 11.1.2、Arm GCC 16.2.0、GDB 18.1、CMake 4.4.4、GNU Make 4.4.1 和 Git 2.54.0。详细过程见 [环境验证报告](../codes/docs/validation-report.md)。

## 3. 复位至 C 入口的启动流程

### 3.1 向量表与初始栈指针

在 QEMU mps2-an500 平台上，第一周 [startup.S](../codes/src/startup.S) 的向量表前两项如下：

```asm
vectors:
.word _stack_top
.word Reset_Handler
```

第一项为初始主栈指针 MSP，第二项为复位入口，其余表项用于异常和中断入口。[Arm Cortex-M7 用户指南][arm-guide]

10 月 13 日第一周 ELF 的检查结果如下：

```text
向量表前 8 字节：00 00 40 20  c5 00 00 00
_stack_top    = 0x20400000
Reset_Handler = 0x000000c4
```

ELF 采用小端存储：前四字节表示 0x20400000，后四字节表示 0x000000c5。复位函数的指令地址为 0xc4，向量项的最低位表示 Thumb 状态。

初始栈顶位于 RAM 末端边界，栈向低地址增长。函数执行过程中 SP 会变化，因此调试记录中的 0x203ffff0 与初始 MSP 对应不同执行时刻。具体地址仅适用于该次构建，代码修改后应重新检查。

### 3.2 全局数据初始化

以下两个全局变量分别用于观察 .data 与 .bss 的初始化结果：

```c
volatile uint32_t initialized_data = 0x12345678;
volatile uint32_t zero_data;
```

initialized_data 具有指定初值，位于 .data；zero_data 需要零初始化，位于 .bss。链接脚本定义段位置，启动汇编完成初值复制和清零后调用 main。

| 符号 | 第一周示例中的含义 | 当时检查结果 |
|---|---|---|
| _sidata | .data 初值的装载地址 | 0x00000170 |
| _sdata / _edata | .data 在 RAM 中的起止边界 | 0x20000000 至 0x20000004，不含结束地址 |
| _sbss / _ebss | .bss 的清零范围 | 0x20000004 至 0x2000000c，不含结束地址 |

.data 复制循环的源码节选如下：

```asm
  ldr r0, =_sidata
  ldr r1, =_sdata
  ldr r2, =_edata
1:
  cmp r1, r2
  bcs 2f
  ldr r3, [r0], #4
  str r3, [r1], #4
  b 1b
```

r0 指向初值装载位置，r1 指向 RAM 写入位置，r2 表示结束边界。循环每次复制 4 字节并递增读写地址，随后清零 .bss 并执行 bl main。当前链接脚本按 4 字节对齐段边界，与按字复制方式相匹配。

main 之前执行的启动代码负责建立基本 C 运行条件。相关职责可结合 [Arm CMSIS 启动示例][cmsis-startup] 对照分析。

### 3.3 链接布局与存储区域

第一周 [linker.ld](../codes/src/linker.ld) 的主要布局如下：

| 区域 | 地址范围 | 用途 |
|---|---|---|
| ZBT SSRAM1 | 0x00000000 至 0x003fffff | 向量表、代码、常量和 .data 初值 |
| ZBT SSRAM2/3 | 0x20000000 至 0x203fffff | 可写数据、.bss 和栈 |

.data > RAM AT> CODE 表示变量运行时在 RAM 中，初始内容放在 CODE 区。KEEP(*(.isr_vector)) 用于保留向量表，避免链接垃圾回收将其移除。ENTRY(Reset_Handler) 指定 ELF 入口信息，复位时所需的向量表仍然要正确填写。

CODE 是工程定义的区域名称，在当前模拟板上对应 SRAM，尚不具备 Flash 擦写约束和掉电保存行为。链接脚本中的 rx 属性也不等同于硬件写保护，后续存储层需要单独建模和验证。[平台说明](../codes/docs/platform.md)、[QEMU 板级源码][qemu-source]

## 4. 启动验证方法与结果

### 4.1 正常启动与调试结果

第一周 Demo 的正常输出如下：

```text
PASS: Cortex-M7; .data initialized; .bss zero; bare-metal C99
```

GDB 记录包含 main 断点、stepi 单步及 demo_result = 0x600d，支持当时编译、链接、仿真运行和调试流程已完成的结论。[QEMU 日志](../codes/logs/qemu-run.txt)、[GDB 日志](../codes/logs/gdb.txt)

### 4.2 初始化污染验证

仅观察 .bss 为零不足以确认清零循环正确执行，因为模拟 RAM 初值可能已为零。10 月 13 日的补充实验通过预先写入错误值，检查启动代码能否恢复预期状态。

测试停在 Reset_Handler，将 .data 的 4 字节填充为 0xCC，将 .bss 的 8 字节填充为 0xA5，并在 .bss 末尾后设置 4 字节哨兵。随后运行至 main 的第一条指令，在 C 程序后续赋值前检查内存。

| 检查内容 | 实际结果 |
|---|---|
| .data 初值恢复 | 恢复为 0x12345678 |
| .bss 清零 | 8 字节全部为零 |
| 末尾哨兵 | 仍为 0x12345678 |
| GDB 单步 | PC 从 0x44 前进到 0x46 |

测试将汇编循环与内存变化对应起来，验证了当前布局下的初始化行为。哨兵仅覆盖一个末尾观察位置，其他段长度和边界情况仍需补充测试。

### 4.3 负向用例与判定规则

两个负向用例在 main 入口分别将 initialized_data 或 zero_data 修改为 0xDEADBEEF，预期触发自检失败。实际结果均为输出 FAIL、结果标记 0xdead、退出码 1，符合错误检测的预期。

第一周增强验证共 6 项检查通过，包括工具检查、全新构建和 4 个固件运行场景。这两个错误用例验证的是自检和失败退出，升级回滚需要后续单独实现。[检查摘要](../codes/logs/week1-review-20261013-125900/summary.json)、[增强验证报告](../codes/docs/week1-audit.md)

## 5. S7 启动资料与项目需求

本阶段主要参考 S7-1500 公开资料，重点区分 PLC 用户程序启动与 MCU 复位过程的层次和职责。

| 概念 | 从资料中整理的作用 |
|---|---|
| 签名固件更新 | 完整下载后、安装前检查更新文件 |
| Secure Boot | 启动前检查待执行固件的真实性和完整性 |
| Startup OB | PLC 操作系统在 STOP 转 RUN 时调用，处理用户程序的启动逻辑 |

Startup OB 工作在 PLC 运行时系统之上，不能直接等同于 Cortex-M 的 Reset_Handler。[签名更新][s7-update]、[Secure Boot][s7-secure]、[Startup OBs][s7-startup]

S7 的装载存储器、工作存储器和保持性存储器体现了非易失保存、运行时存储和掉电保持数据的不同职责。PLC 程序存储区说明不足以确定 CPU 固件或 Bootloader 的实际分区。[CPU 存储区说明][s7-memory]

已有资料记录中，Secure Boot 的支持范围与具体 CPU 订货号有关。需由带教确认参考型号后查阅对应手册。目前尚未确认其内部 Boot ROM、密钥存放和镜像切换实现。

本项目使用 Arm MPS2 模拟板，后续镜像分区和升级状态属于项目设计方案。S7 公开资料用于分析工业需求。[QEMU 平台说明][qemu-board]

## 6. 固件升级与恢复设计

后续设计重点包括损坏镜像拒绝、写入中断后的状态恢复，以及通过校验但运行失败的候选镜像回退。

当前设计草案如下，相关功能尚未全部实现：

| 需求 | 初步方案 | 后续测试 |
|---|---|---|
| 镜像边界有效 | 检查长度、装载地址、向量表和入口 | 截断、超长、非法地址 |
| 完整性与真实性 | CRC 检错，后续结合可信公钥验证签名和元数据 | 修改内容、错误签名、伪造版本 |
| 升级中断可恢复 | 保留已确认镜像，更新先写候选区 | 在接收、写入、验证、提交时模拟复位 |
| 启动失败可回退 | 候选镜像先试运行，由应用确认成功 | 校验通过但启动后卡死 |
| 状态可持久保存 | 设计带序号、校验和冗余的状态记录 | 半写入记录、损坏一份记录 |
| 故障可定位 | 区分失败阶段并记录状态 | 结合 UART 日志和 GDB 判断原因 |
| 无可用镜像时恢复 | 保留受控的恢复入口 | 两份镜像都损坏 |

### 6.1 CRC 哈希和签名的区别

CRC32 主要用于检查意外传输或存储错误。SHA-256 用来计算内容摘要，但如果镜像和普通存储的摘要一起被替换，仅比较摘要仍然不能证明来源可信。签名验证还需要可信公钥和正确的验证过程。

任务书中的 SHA-256 和 RSA-2048 属于后续方案。实现前需明确签名格式、签名覆盖范围、密钥保护和版本策略，算法调用本身不足以建立完整的安全升级流程。这些算法选择也不能直接写成 S7 的内部实现。

### 6.2 双镜像与升级状态管理

初步流程如下，具体状态转换仍需进一步讨论：

```text
保留已确认镜像 → 将更新写入候选区 → 验证候选镜像
             → 记录待试运行状态 → 试运行 → 应用确认后提交

验证失败：保留已确认镜像
试运行失败：达到重试上限后回退
无可用镜像：进入恢复模式
```

关键设计问题包括候选镜像的有效性确认时机，以及切换记录不完整时的处理方式。回退至已确认镜像用于恢复；是否允许安装任意旧版本，需要另行确定版本安全策略。

有效的断电测试需要保留模拟非易失存储的中间状态。若结束 QEMU 后存储内容完全丢失，重启结果无法验证部分写入状态的恢复。因此应先明确持久化方案，再实施故障注入。

### 6.3 工业可靠性与安全要求

减少停机、抵御未经授权的修改，以及故障后的安全状态分别对应可用性、网络安全和功能安全要求。真实设备反复重启可能影响输出，其故障行为需由系统需求规定。当前未连接真实执行器，验证范围主要为软件启动和恢复流程。

IEC 62443 的资料学习目前限于公开概览：4-1 侧重安全开发过程，4-2 侧重组件技术要求，3-3 侧重系统安全要求。Siemens 的纵深防御也包括工厂、网络和系统完整性多个层面。[IEC 概览][iec-overview]、[Siemens 工业网络安全][siemens-security]

当前可落实的工作包括建立需求、实现和测试的对应关系，检查串口输入长度与命令边界，保存正常和异常结果，并记录版本及已知问题。标准全文分析、威胁建模和认证评估尚未展开，后续范围需与带教确认。

## 7. 当前工程架构

```text
构建：CMake → Make → Arm GCC / 链接器 → ELF
第一周：复位向量 → Reset_Handler → 数据初始化 → main → semihosting
第二周：启动代码 → main → board / UART → 自检和串口日志
异常：异常入口 → exception_handler → fault_idle
调试：GDB 经 QEMU gdbstub 观察寄存器和内存
```

第一周向量表只有 16 个系统表项，后 14 项统一指向默认处理函数，包括保留槽位。这是最小示例的简化。第二周已新增 48 项向量表，并区分异常入口和保留项；当前异常处理主要记录异常号并停留，恢复功能尚未实现。

后续按板级初始化、存储、镜像验证、传输、启动选择和应用交接划分模块，以区分接收、验证及应用启动阶段的故障，并限制 main 的职责范围。

时钟配置应依据模拟板实际提供的模型，不能直接套用其他 MCU 的 PLL 和引脚配置。第一周重点是栈、入口和 C 数据；第二周使用模型固定的 25 MHz 时钟，尚未实现真实 MCU 的时钟切换、缓存和 MPU 配置。

## 8. 截至更新日期的进展

### 8.1 UART 输出已完成阶段验证

第一周 semihost 使用 bkpt 0xab 请求宿主机服务。该机制用于最小 Demo 输出，不能作为 UART 驱动有效性的依据。

第二周已经新增 UART0 轮询发送，关闭 semihosting 后仍能输出：

```text
BOOT: week2 mps2-an500 Cortex-M7
CLOCK: 25000000 Hz (QEMU fixed)
UART0: 115200 requested, divisor=217, 8N1
PASS: C runtime; UART boot ready
```

10 月 14 日最终记录中，12 项检查通过，包含模型时钟、构建、第一周回归及 9 个第二周运行场景；三次正常启动分别计数。已检查初始化污染、参数边界、变量破坏、UART 禁用和 HardFault 注入。[第二周测试报告](../codes/docs/week2-test-report.md)

当前 UART 实现 TX 轮询发送，RX 和中断尚未实现。轮询上限按循环次数定义，实际时间超时与持续阻塞仍需验证。独立应用跳转尚未实现，后续需处理应用入口、MSP、向量表及遗留中断状态。

### 8.2 Git 已提交并同步到 GitHub

目前已有第一周基线 8bb81c4、第二周代码 8cbafe1 和目录整理 9f8969f。工程已推送到 [GitHub 仓库](https://github.com/Charttt0n/Siemens_Intern_Charton)，main 关联 origin/main。原笔记中“尚无提交”的记录是第一周早期状态，现已更新。

后续代码在 codes 中修改，周报在 reports 中维护，学习记录保存在 notes，规划保存在 planning。archive 保存历史副本。Git 仓库位于项目根目录，统一管理这些正式材料。

## 9. 实验复现与后续安排

下面的命令从项目的 codes 目录执行，用于检查已有第一周构建产物：

```sh
cd /Users/charton/projects/Siemens_Intern/codes
source scripts/env.sh
arm-none-eabi-objdump -s -j .isr_vector build/demo.elf
arm-none-eabi-nm -n build/demo.elf
```

ELF 检查记录见 [日志](../codes/logs/week1-elf-inspection.txt)。具体地址应以对应构建产物为准。

手动调试第一周 Demo 时，第一个终端运行：

```sh
bash scripts/run.sh -S -gdb tcp:127.0.0.1:3333
```

第二个终端进入同一个 codes 目录，再运行：

```sh
source scripts/env.sh
arm-none-eabi-gdb build/demo.elf
```

在 GDB 中检查向量表和寄存器，并设置 main 断点：

```gdb
target remote 127.0.0.1:3333
x/2wx 0x0
info registers pc sp
break main
continue
print/x initialized_data
print zero_data
stepi
```

自动验证和第二周启动入口如下：

```sh
python3 scripts/verify-startup.py
python3 scripts/verify-week2.py
bash scripts/run-week2.sh
```

QEMU 模拟调试使用自带的 gdbstub，当前不需要先启动 OpenOCD。[QEMU GDB 文档][qemu-gdb] 第二周程序输出后会进入等待循环，日志输出完成后保持等待属于预期行为，可按 Ctrl+C 结束运行。

验证脚本会新建构建和日志目录。latest 文件用于定位最近一次运行目录，验证结果以对应 summary.json 为准；源码和 ELF 哈希用于对应测试版本。

下一阶段继续分析启动汇编、链接脚本和 UART 驱动，并结合调试复现关键执行过程；随后增加接收、中断和缓冲区测试。Flash 仿真方案、参考 S7 型号、升级恢复故障范围和安全标准条款映射要求，需与带教确认。

## 10. 参考资料

任务范围依据项目任务书《Industrial Embedded Systems Bootloader Development》第一周计划。以下均为官方资料，检索日期为 2026-10-13；标准部分使用公开概览，不替代标准全文。网页可能更新，产品能力应以具体型号和固件手册为准。

- [Siemens：S7-1500 CPU memory areas][s7-memory]，STEP 7 V20，2024-11。
- [Siemens：Startup OBs (S7-1500)][s7-startup]，STEP 7 V20，2024-11。
- [Siemens：Secure Boot for CPUs][s7-secure]，S7-1500/ET 200MP Manual Collection V20，2025-06；10 月 13 日记录中已核对正文。
- [Siemens：Signed firmware update for CPUs][s7-update]，S7-1500/ET 200MP Manual Collection V20。
- [Siemens：Industrial Cybersecurity][siemens-security]。
- [IEC：62443 系列概览][iec-overview]。
- [Arm：Cortex-M7 Devices Generic User Guide，DUI 0646B][arm-guide]。
- [Arm：CMSIS_4 Cortex-M7 GCC 启动参考][cmsis-startup]，历史仓库，已归档。
- [QEMU：MPS2 平台][qemu-board]、[板级源码][qemu-source]、[GDB 调试][qemu-gdb]。

[s7-memory]: https://docs.tia.siemens.cloud/r/en-us/v20/functional-description-of-s7-1500-cpus-s7-1500/memory-areas-s7-1500/cpu-memory-areas-s7-1500
[s7-startup]: https://docs.tia.siemens.cloud/r/en-us/v20/functional-description-of-s7-1500-cpus-s7-1500/organization-blocks-s7-1500/description-of-all-types-of-organization-blocks-s7-1500/startup-obs-s7-1500
[s7-secure]: https://docs.tia.siemens.cloud/r/simatic_s7_1500_et_200mp_manual_collection_enus_20/basic-information/automation-system/industrial-cybersecurity/secure-operation-of-cpus/secure-boot-for-cpus
[siemens-security]: https://www.siemens.com/en-us/company/digital-transformation/cybersecurity/industrial-cybersecurity/
[iec-overview]: https://syc-se.iec.ch/deliveries/cybersecurity-guidelines/security-standards-and-best-practices/iec-62443/
[arm-guide]: https://documentation-service.arm.com/static/61efd6602dd99944d051417b
[cmsis-startup]: https://github.com/ARM-software/CMSIS_4/blob/master/Device/ARM/ARMCM7/Source/GCC/startup_ARMCM7.c
[qemu-board]: https://www.qemu.org/docs/master/system/arm/mps2.html
[qemu-source]: https://github.com/qemu/qemu/blob/master/hw/arm/mps2.c
[qemu-gdb]: https://www.qemu.org/docs/master/system/gdb.html

[s7-update]: https://docs.tia.siemens.cloud/r/simatic_s7_1500_et_200mp_manual_collection_enus_20/basic-information/automation-system/industrial-cybersecurity/secure-operation-of-cpus/signed-firmware-update-for-cpus
