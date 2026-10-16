# 第一周学习笔记 工业 Bootloader 与 Cortex-M7 启动流程

初次整理：2026 年 10 月 13 日  
更新日期：2026 年 10 月 16 日  
开发环境：MacBook + QEMU mps2-an500 + GNU Arm 工具链

## 1. 这一阶段主要学什么

第一周主要是把开发环境搭起来，跑通一个最小裸机程序，再结合代码理解处理器怎样从复位进入 main。后面的固件升级、镜像校验和失败恢复，都需要先有一个能运行、能调试的基础工程。

我现在主要围绕几个问题学习：程序还没进入 main 时执行了什么？全局变量的初值是谁准备的？程序输出 PASS 后，还需要做哪些检查？工业设备升级失败时，怎样保留恢复的机会？

这份笔记以第一周的 Demo 和测试记录为基础，后面补充了 UART 和 Git 的进展。已经跑过的实验、查资料得到的认识，以及后续准备做的功能，会分别说明。

## 2. 先理清几个工具的分工

| 工具 | 我的理解 |
|---|---|
| VS Code | 编辑代码、查看工程，也可以作为构建和调试的操作入口 |
| Arm GCC / Binutils | 编译 C 和汇编，链接生成 ELF，查看符号和反汇编 |
| CMake 和 Make | CMake 生成构建规则，Make 按规则执行编译和链接 |
| QEMU | 在 Mac 上模拟开发板，运行编译好的 ARM 固件 |
| GDB | 连接模拟器，设置断点、单步执行，查看寄存器和内存 |
| Git 和 GitHub | Git 保存修改历史，GitHub 保存远程仓库，便于同步和查看 |

QEMU 本身是一个程序。当前项目通过终端启动它，串口日志也显示在终端里，所以不会出现单独的开发板图形界面。VS Code 负责编辑，真正执行固件的是 QEMU。

Mac 的处理器虽然也是 Arm 架构，但 Cortex-M7 的指令、运行环境和内存布局与 Mac 不同，仍然需要交叉编译工具链。

10 月 12 日的验证记录中，工具版本为 QEMU 11.1.2、Arm GCC 16.2.0、GDB 18.1、CMake 4.4.4、GNU Make 4.4.1 和 Git 2.54.0。详细过程见 [环境验证报告](../codes/docs/validation-report.md)。

## 3. 从复位到 main 的过程

### 3.1 向量表先告诉处理器栈和入口在哪里

本项目使用 QEMU 的 mps2-an500 平台，模拟 Cortex-M7。第一周 [startup.S](../codes/src/startup.S) 中的向量表从下面两项开始：

```asm
vectors:
.word _stack_top
.word Reset_Handler
```

第一项是初始主栈指针 MSP，第二项是复位入口。后面的表项保存异常和中断入口。[Arm Cortex-M7 用户指南][arm-guide]

10 月 13 日对第一周 ELF 的检查结果是：

```text
向量表前 8 字节：00 00 40 20  c5 00 00 00
_stack_top    = 0x20400000
Reset_Handler = 0x000000c4
```

这里采用小端存储，因此前四个字节表示 0x20400000，后四个字节表示 0x000000c5。复位函数的指令地址是 0xc4，向量项多出的最低位用于表示 Thumb 状态。

栈顶位于 RAM 的末端边界，使用栈时向低地址增长。函数执行后 SP 会变化，所以调试中看到的 0x203ffff0 与初始 MSP 不同是正常的。具体地址对应这次构建，修改代码后需要重新查看。

### 3.2 进入 C 程序之前还要准备全局数据

我对启动代码的理解，可以先从两个全局变量看起：

```c
volatile uint32_t initialized_data = 0x12345678;
volatile uint32_t zero_data;
```

initialized_data 有指定初值，放在 .data 中；zero_data 需要零初始化，放在 .bss 中。链接脚本安排这些内容的位置，启动汇编负责复制初值和清零，完成后才调用 main。

| 符号 | 第一周示例中的含义 | 当时检查结果 |
|---|---|---|
| _sidata | .data 初值的装载地址 | 0x00000170 |
| _sdata / _edata | .data 在 RAM 中的起止边界 | 0x20000000 至 0x20000004，不含结束地址 |
| _sbss / _ebss | .bss 的清零范围 | 0x20000004 至 0x2000000c，不含结束地址 |

下面是 .data 复制循环的源码节选：

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

r0 指向初值，r1 指向写入位置，r2 是结束边界。每次复制 4 字节，并移动读写位置。后面的 .bss 循环把对应范围写成零，再执行 bl main。当前链接脚本把段边界按 4 字节对齐，与这种复制方式对应。

这也说明 main 不是裸机程序最先执行的代码。进入 main 之前，启动代码已经准备了一部分 C 运行条件。可以结合 [Arm CMSIS 启动示例][cmsis-startup] 对照理解。

### 3.3 链接脚本决定内容放在哪里

第一周 [linker.ld](../codes/src/linker.ld) 的主要布局如下：

| 区域 | 地址范围 | 用途 |
|---|---|---|
| ZBT SSRAM1 | 0x00000000 至 0x003fffff | 向量表、代码、常量和 .data 初值 |
| ZBT SSRAM2/3 | 0x20000000 至 0x203fffff | 可写数据、.bss 和栈 |

.data > RAM AT> CODE 表示变量运行时在 RAM 中，初始内容放在 CODE 区。KEEP(*(.isr_vector)) 用来保留向量表，避免它被链接垃圾回收移除。ENTRY(Reset_Handler) 指定 ELF 入口信息，复位时所需的向量表仍然要正确填写。

这里容易混淆的是 CODE 这个名字：它在当前模拟板上对应 SRAM，并不是已经实现了擦写和掉电保存的 Flash。链接脚本里的 rx 属性也不能代替硬件写保护。后续做存储层时，需要另外考虑这些行为。[平台说明](../codes/docs/platform.md)、[QEMU 板级源码][qemu-source]

## 4. 用实验检查启动代码

### 4.1 正常运行能看到什么

第一周 Demo 的输出是：

```text
PASS: Cortex-M7; .data initialized; .bss zero; bare-metal C99
```

GDB 记录中有 main 断点、stepi 单步和 demo_result = 0x600d。这些结果说明当时已经跑通编译、链接、模拟运行和调试。[QEMU 日志](../codes/logs/qemu-run.txt)、[GDB 日志](../codes/logs/gdb.txt)

### 4.2 为什么还要先把内存写坏

只看到 .bss 为零，还不能确定清零循环执行正确，因为模拟器提供的内存初值可能恰好就是零。10 月 13 日的补充实验因此先设置错误初值，再观察启动代码能否恢复。

测试停在 Reset_Handler，将 .data 的 4 字节填成 0xCC，将 .bss 的 8 字节填成 0xA5，并在 .bss 末尾后放一个 4 字节哨兵。然后运行到 main 的第一条指令，在 C 程序后续赋值前检查内存。

| 检查内容 | 实际结果 |
|---|---|
| .data 初值恢复 | 恢复为 0x12345678 |
| .bss 清零 | 8 字节全部为零 |
| 末尾哨兵 | 仍为 0x12345678 |
| GDB 单步 | PC 从 0x44 前进到 0x46 |

这种测试让我更容易把汇编循环和实际内存变化联系起来。哨兵只观察一个末尾位置，还不能说明所有段长度和边界情况都没有问题。

### 4.3 错误用例为什么也算通过

另外两个实验在 main 入口分别把 initialized_data 或 zero_data 改成 0xDEADBEEF。预期就是让自检失败，因此固件输出 FAIL、结果为 0xdead、退出码为 1，才说明程序正确识别了错误。

第一周增强验证共 6 项检查通过，包括工具检查、全新构建和 4 个固件运行场景。这两个错误用例验证的是自检和失败退出，升级回滚需要后续单独实现。[检查摘要](../codes/logs/week1-review-20261013-125900/summary.json)、[增强验证报告](../codes/docs/week1-audit.md)

## 5. S7 资料和这个项目有什么关系

目前主要参考 S7-1500 的公开资料。我觉得先分清不同层次的“启动”比较重要，否则容易把 PLC 用户程序启动和 MCU 复位过程混在一起。

| 概念 | 从资料中整理的作用 |
|---|---|
| 签名固件更新 | 完整下载后、安装前检查更新文件 |
| Secure Boot | 启动前检查待执行固件的真实性和完整性 |
| Startup OB | PLC 操作系统在 STOP 转 RUN 时调用，处理用户程序的启动逻辑 |

Startup OB 工作在 PLC 运行时系统之上，不能直接当成 Cortex-M 的 Reset_Handler。[签名更新][s7-update]、[Secure Boot][s7-secure]、[Startup OBs][s7-startup]

S7 的装载存储器、工作存储器和保持性存储器，也让我注意到“文件保存在哪里”和“程序运行时使用哪里”需要分开考虑。不过 PLC 程序存储区的介绍，不能直接推出 CPU 固件或 Bootloader 的分区。[CPU 存储区说明][s7-memory]

已有资料记录中，Secure Boot 的支持范围与具体 CPU 订货号有关。因此还需要带教确认参考型号，再查对应手册。现阶段尚不清楚它内部的 Boot ROM、密钥存放和镜像切换实现。

本项目使用的是 Arm MPS2 模拟板；后续设计的镜像区和升级状态，是自己的练习方案。S7 资料主要帮助理解工业需求。[QEMU 平台说明][qemu-board]

## 6. 后面的升级功能准备怎样考虑

正常启动只是第一步。后续要解决的问题是：镜像损坏时能否拒绝启动，写到一半断电后怎样处理，新版本能通过校验但运行失败时又怎么办。

下面是当前的设计草案，还没有全部实现：

| 需求 | 准备采用的做法 | 后续测试 |
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

任务书中的 SHA-256 和 RSA-2048 属于后续方案。真正实现时还要确定签名格式、签名覆盖的数据、密钥保护和版本策略，不能只把算法调用加进去就认为升级已经安全。这些算法选择也不能直接写成 S7 的内部实现。

### 6.2 双镜像还需要状态管理

我目前倾向于按下面的流程讨论：

```text
保留已确认镜像 → 将更新写入候选区 → 验证候选镜像
             → 记录待试运行状态 → 试运行 → 应用确认后提交

验证失败：保留已确认镜像
试运行失败：达到重试上限后回退
无可用镜像：进入恢复模式
```

这里最需要想清楚的是，什么时候把新镜像标为可用，以及切换记录写到一半该怎么办。回退到上一份已确认镜像用于恢复，是否允许安装任意旧版本则涉及安全策略，需要另外确定。

断电测试也不能简单理解成关闭 QEMU 再打开。如果模拟存储内容完全丢失，下次启动就看不到写了一半的状态。需要先设计能保存中间状态的模拟非易失存储，再做故障注入。

### 6.3 工业场景还要考虑什么

减少停机、避免未经授权的修改，以及故障后让设备处于安全状态，是不同的要求。真实设备启动失败时，反复重启可能影响输出，具体安全行为需要系统需求来规定。当前没有接真实执行器，先关注软件启动和恢复流程。

IEC 62443 的学习目前只到公开概览：4-1 侧重安全开发过程，4-2 侧重组件技术要求，3-3 侧重系统安全要求。Siemens 的纵深防御也包括工厂、网络和系统完整性多个层面。[IEC 概览][iec-overview]、[Siemens 工业网络安全][siemens-security]

对现在的工作，我可以先把需求、代码和测试对应起来，注意串口输入的长度和命令检查，保存正常与异常结果，记录版本和已知问题。标准全文、威胁建模和认证还没有展开，后续需要带教明确范围。

## 7. 当前代码怎样分层

```text
构建：CMake → Make → Arm GCC / 链接器 → ELF
第一周：复位向量 → Reset_Handler → 数据初始化 → main → semihosting
第二周：启动代码 → main → board / UART → 自检和串口日志
异常：异常入口 → exception_handler → fault_idle
调试：GDB 经 QEMU gdbstub 观察寄存器和内存
```

第一周向量表只有 16 个系统表项，后 14 项统一指向默认处理函数，包括保留槽位。这是最小示例的简化。第二周已新增 48 项向量表，并区分异常入口和保留项；当前异常处理主要记录异常号并停留，恢复功能还没有实现。

后续准备继续分开板级初始化、存储、镜像验证、传输、启动选择和应用交接。这样出现问题时，可以先判断是接收失败、验证失败还是应用本身启动失败，main 也不至于承担所有细节。

时钟配置需要按模拟板实际提供的模型来做，不能直接照搬某款 STM32 的 PLL 和引脚配置。第一周重点是栈、入口和 C 数据；第二周使用模型固定的 25 MHz 时钟，尚未实现真实 MCU 的时钟切换、缓存和 MPU 配置。

## 8. 截至更新日期的进展

### 8.1 UART 输出已完成阶段验证

第一周 semihost 使用 bkpt 0xab 请求宿主机服务。它能帮助跑通 Demo，但不能据此判断 UART 驱动已经工作。

第二周已经新增 UART0 轮询发送，关闭 semihosting 后仍能输出：

```text
BOOT: week2 mps2-an500 Cortex-M7
CLOCK: 25000000 Hz (QEMU fixed)
UART0: 115200 requested, divisor=217, 8N1
PASS: C runtime; UART boot ready
```

10 月 14 日最终记录中，12 项检查通过，包含模型时钟、构建、第一周回归及 9 个第二周运行场景；三次正常启动分别计数。已检查初始化污染、参数边界、变量破坏、UART 禁用和 HardFault 注入。[第二周测试报告](../codes/docs/week2-test-report.md)

当前 UART 只做 TX 轮询发送，RX 和中断还没实现。轮询上限是循环次数，还需要继续考虑实际超时和持续阻塞。程序到 main 也不代表已经实现跳转到独立应用，应用入口、MSP、向量表和遗留中断状态仍需后续处理。

### 8.2 Git 已提交并同步到 GitHub

目前已有第一周基线 8bb81c4、第二周代码 8cbafe1 和目录整理 9f8969f。工程已推送到 [GitHub 仓库](https://github.com/Charttt0n/Siemens_Intern_Charton)，main 关联 origin/main。原笔记中“尚无提交”的记录是第一周早期状态，现已更新。

后续代码在 codes 中修改，周报在 reports 中维护，学习记录放在 notes，规划放在 planning。archive 保存历史副本。Git 仓库位于项目根目录，统一管理这些正式材料。

## 9. 复习和重做实验的入口

下面的命令从项目的 codes 目录执行，用来查看已有的第一周构建产物：

```sh
cd /Users/charton/projects/Siemens_Intern/codes
source scripts/env.sh
arm-none-eabi-objdump -s -j .isr_vector build/demo.elf
arm-none-eabi-nm -n build/demo.elf
```

ELF 检查记录见 [日志](../codes/logs/week1-elf-inspection.txt)。地址以本次构建为准，不能一直套用笔记中的数值。

手动调试第一周 Demo 时，第一个终端运行：

```sh
bash scripts/run.sh -S -gdb tcp:127.0.0.1:3333
```

第二个终端进入同一个 codes 目录，再运行：

```sh
source scripts/env.sh
arm-none-eabi-gdb build/demo.elf
```

GDB 中可以先检查向量表和寄存器，再断在 main：

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

QEMU 模拟调试使用自带的 gdbstub，当前不需要先启动 OpenOCD。[QEMU GDB 文档][qemu-gdb] 第二周程序输出后会进入等待循环，终端不再打印是正常现象，按 Ctrl+C 可以结束运行。

验证脚本会新建构建和日志目录。latest 文件只是最近一次运行位置，是否通过还要查看对应 summary.json；源码和 ELF 哈希用于对应测试版本。

接下来需要熟悉启动汇编、链接脚本和 UART 驱动，能够结合调试演示解释它们；再增加接收、中断和缓冲区测试。Flash 仿真方案、参考的 S7 型号、升级恢复的故障范围，以及是否需要做安全标准条款映射，准备与带教确认。

## 10. 参考资料

任务范围依据用户提供的《Industrial Embedded Systems Bootloader Development》第一周计划。以下均为官方资料，检索日期为 2026-10-13；标准部分使用公开概览，不替代标准全文。网页可能更新，产品能力应以具体型号和固件手册为准。

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
