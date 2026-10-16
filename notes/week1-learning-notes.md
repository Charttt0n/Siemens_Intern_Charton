# 第一周学习笔记：工业 Bootloader 需求与 Cortex-M7 启动流程

整理日期：2026-10-13  
记录视角：大四实习生，从最小裸机实验梳理启动过程与工业升级需求  
项目平台：Apple Silicon macOS + QEMU `mps2-an500` + GNU Arm 工具链

## 1. 这一周需要回答什么问题

第一周的目标是建立一个能继续开发的起点：工具链能够编译程序，QEMU 能运行程序，GDB 能观察启动过程，同时说清楚后面为什么需要固件校验、升级和故障恢复。

我把学习内容归纳为三个问题：

1. 没有操作系统时，处理器怎样找到第一条指令，又怎样进入 `main()`？
2. 工业设备的 Bootloader 除了“启动成功”，还需要处理哪些失败情况？
3. 目前这个示例已经证明了什么，还有哪些能力尚未实现？

本文中的“已验证”对应本地代码或日志；“资料结论”对应官方文档；“设计设想”是本项目后续准备实现的方案。这三类内容分别记录，避免把学习示例写成已完成的工业产品。

## 2. 已完成的环境与实验

2026-10-12 的验证记录显示，QEMU 11.1.2、Arm GCC 16.2.0、Arm GDB 18.1、CMake 4.4.4、GNU Make 4.4.1 和原有 Git 2.54.0 已可使用。具体安装与验证过程见 [环境验证报告](../codes/docs/validation-report.md)。

我对各工具分工的理解如下：

| 工具 | 在当前工程中的作用 |
|---|---|
| Arm GCC / Binutils | 把 C 和汇编转换为 Cortex-M7 可执行的机器码，链接并查看 ELF |
| CMake | 根据工程配置生成构建规则 |
| Make | 执行编译、链接规则 |
| QEMU | 在 Mac 上模拟目标板，执行 ARM 裸机程序 |
| GDB | 连接 QEMU，设置断点、单步并读取寄存器和变量 |
| Git | 保存源代码、文档和后续修改历史 |

Mac 自身是 arm64，但这并不表示本机编译器可以直接生成 Cortex-M7 裸机固件。目标指令集、ABI、链接布局和运行环境都需要交叉工具链明确指定。

实际验证输出为：

```text
PASS: Cortex-M7; .data initialized; .bss zero; bare-metal C99
```

GDB 日志记录了 `main` 断点、一次 `stepi` 和 `semihost` 断点；随后读到 `demo_result = 0x600d`。这表明当前示例已跑通“编译 → 链接 → 仿真运行 → 调试观察”。原始证据见 [QEMU 日志](../codes/logs/qemu-run.txt)、[GDB 日志](../codes/logs/gdb.txt) 和 [总体验证状态](../codes/logs/STATUS.txt)。

### 2.1 从正常输出进一步检查初始化

复核时，我发现原测试存在一个盲点：模拟 RAM 可能默认就是零，单看 `.bss == 0`，还不能判断清零代码是否真正起作用。因此，2026-10-13 补做了启动前内存污染实验，并在全新构建目录编译，确保 QEMU 和 GDB 使用同一份新 ELF。

具体做法是在复位入口暂停，将 `.data` 的 4 字节运行区写成 `0xCC`，将 `.bss` 的 8 字节全部写成 `0xA5`，并在 `_ebss` 后紧邻的 4 字节写入哨兵 `0x12345678`。随后运行到 `main` 的第一条指令，在任何 C 赋值发生前检查内存。

结果是 `.data` 恢复为 `0x12345678`，整个 `.bss` 清零，末尾哨兵没有改变。这说明当前布局下的复制和清零确实执行了。哨兵只覆盖一个边界观察位置，不能由此推导任意长度、任意布局都没有越界。

### 2.2 检查程序能否识别错误

我另外启动两个独立 QEMU 实例，分别在进入 `main` 时把 `initialized_data`、`zero_data` 改成 `0xDEADBEEF`。两次都观察到 `demo_result = 0xdead`、固件输出 FAIL、QEMU 退出码为 1。由于这是主动注入的错误，测试应判为通过。

这两个负向用例验证的是当前 C 示例的异常识别和失败退出路径，尚未涉及 Bootloader 回退或恢复。全部结果见 [本次检查汇总](../codes/logs/week1-review-20261013-125900/summary.json)；运行方法和限制见 [第一周查漏补缺记录](../codes/docs/week1-audit.md)。

## 3. SIMATIC S7：先区分底层启动和 PLC 程序启动

### 3.1 从公开资料中能确认的内容

本次以 S7-1500 的公开资料为主要参考。S7 是一个产品家族，型号、硬件代次和固件版本不同，能力也可能不同。

| 资料结论 | 对本项目的启发 |
|---|---|
| S7-1500 将装载存储器、工作存储器和保持性存储器区分开；装载存储器在 SIMATIC Memory Card 上，工作存储器用于运行所需的代码和数据，保持性存储器保存有限的掉电保持数据 | 区分 PLC 程序的非易失保存、运行时存储和保持数据；不能据此推导 CPU 固件或 Bootloader 的物理分区 |
| S7-1500 启动 OB 由操作系统在 STOP 向 RUN 转换时调用，完成后进入循环程序 | PLC 的用户程序启动发生在运行时系统之上，不能直接等同于 MCU 的复位入口 |
| S7-1500/ET 200MP 手册将 Secure Boot 的适用范围限定到特定订货号系列：`6ES751x-xxx03-0AB0`、`6ES751x-xxx10-0AB0` 起的相关 CPU，在启动前检查待执行固件的真实性和完整性 | 应把固件可信性检查纳入启动设计，但不能据此断言所有 S7 CPU 都采用同一种实现 |

对应来源：[Siemens CPU 存储区说明][s7-memory]、[Startup OBs][s7-startup]、[Secure Boot for CPUs][s7-secure]。2026-10-13 已复核 Secure Boot 官方页面正文，具体型号选定后仍需核对设备手册。

我还把三个概念按检查时机分开：签名固件更新在完整下载之后、安装之前检查更新文件；Secure Boot 在启动前检查待执行固件；Startup OB 由 PLC 操作系统在 STOP 转 RUN 时调用。因此，接收更新时校验成功，不能代替下一次启动时的镜像检查。这里描述的是文档所列产品功能，尚未还原其内部实现。[签名固件更新][s7-update]、[Secure Boot][s7-secure]、[Startup OBs][s7-startup]

### 3.2 我目前建立的分层认识

下面是帮助理解的层次划分，**不是已经还原出的 Siemens 内部启动链**：

```text
处理器复位和底层引导
        ↓
设备固件 / 运行时系统可用
        ↓
PLC 用户程序的启动阶段
        ↓
循环控制程序
```

本项目主要练习第一层及向应用程序交接的过程。OB100 等启动组织块属于 PLC 编程层；它们不能替代 Cortex-M 的向量表或 `Reset_Handler`。

目前没有足够资料确认目标 S7 的内部 Boot ROM、一级/二级引导划分、具体 Flash 地址、密钥存放方式或 A/B 切换算法，所以笔记中不为这些细节编造实现。后续设计的 Bootloader、主镜像、备用镜像和元数据区域是本项目方案，不能标成“S7 原厂内存布局”。

同样，QEMU `mps2-an500` 模拟的是 Arm MPS2 平台的 Cortex-M7 配置；本项目不要求安装 TIA Portal，也不代表正在仿真一台真实 S7 PLC。[QEMU 平台说明][qemu-board]

## 4. 工业 Bootloader 的需求怎么落到代码和测试上

我目前的理解是：正常启动只覆盖最顺利的一条路径。升级中断、镜像损坏和程序无法运行时，系统是否仍然可恢复，才是后续设计中需要重点验证的部分。

下表是根据项目任务整理的**候选工程需求**，不是 IEC 条文清单，也不是已经实现的功能。

| 需求 | 准备如何设计 | 后续如何验收 | 当前状态 |
|---|---|---|---|
| 镜像边界有效 | 检查长度、装载范围、入口及向量表，防止越界读写或跳转 | 输入超长、截断、非法地址镜像，确认拒绝启动 | 待实现 |
| 完整性与真实性 | 先做 CRC32 检错；后续使用受信任公钥验证签名和受保护的镜像元数据 | 修改镜像内容、使用错误签名、伪造版本信息，确认拒绝 | 待实现 |
| 升级中断可恢复 | 在候选区写新镜像，保留已确认镜像；完整验证后再提交切换状态 | 在接收、写入、验证、提交各阶段模拟复位 | 待实现 |
| 启动失败可回退 | 候选镜像试运行，由应用确认成功；设置有限重试次数 | 使用能通过签名但启动后卡死的应用，确认回退 | 待实现 |
| 状态可持久保存 | 为升级元数据设计序号、校验和冗余记录，考虑写入中断 | 破坏一份元数据或制造半写入记录，确认可判定有效状态 | 待设计 |
| 失败可诊断 | 给出版本、启动原因、失败阶段和错误码 | 故障发生后可从日志定位原因 | 仅有最小示例输出 |
| 恢复过程受控 | 没有可用镜像时进入恢复模式；升级命令同样做边界和权限约束 | 两个镜像均损坏时仍能进入受控恢复入口 | 待实现 |

“升级失败不变砖”在这里应理解为：在约定故障模型内，仍有可信引导或恢复途径。它不是对任意硬件损坏、密钥丢失或存储器永久失效的绝对保证。

### 4.1 CRC、哈希和签名分别解决什么

CRC32 适合检测传输或存储中的意外错误，不能证明发送者可信。SHA-256 可以计算内容摘要，但如果攻击者能同时替换镜像和普通存储的摘要，仅比较摘要仍不足以认证镜像。数字签名需要结合可信公钥和正确的验证流程，才能检查镜像是否来自授权签名方。

任务书计划采用 SHA-256 和 RSA-2048，这是本项目后续实现方案。上述 Siemens Secure Boot 页面没有指定 RSA-2048，不能把任务书中的算法选择写成原厂实现。正式实现还要确定签名编码、被签名内容范围、密钥保护和更新策略；不能只写“用了 RSA，所以升级安全”。

### 4.2 为什么两个分区还不够

双镜像只是恢复方案的基础。必须进一步回答：哪一份已验证、哪一份已确认运行成功、切换标志写到一半怎么办、下一次启动依据什么选镜像。

我准备采用以下状态思路进行后续讨论：

```text
已确认镜像可用
    → 将更新写入候选区
    → 验证候选镜像
    → 记录待试运行状态
    → 试运行候选镜像
    → 应用确认成功后提交

验证失败：保留已确认镜像
试运行失败：达到重试上限后回退
无可用镜像：进入恢复模式
```

这是设计草案，尚未实现。还需要区分“回到上一份已确认镜像”和“允许安装任意旧版本”：前者用于恢复，后者可能重新引入已修复漏洞，必须制定版本策略。

### 4.3 高可用、功能安全与网络安全

对本项目来说，高可用关注减少故障后的停机和恢复成本；网络安全关注未经授权的修改或访问；功能安全还涉及故障时设备是否进入规定的安全状态。三者有关联，但不能互相替代。

例如，如果应用启动异常，不能仅为了恢复运行就反复复位并使输出状态不可预测。真实工业设备的输出安全状态，需要由系统需求明确。本项目当前没有真实执行器，暂时只能验证启动和恢复的软件逻辑。

## 5. 对 IEC 62443 的第一周认识

IEC 62443 是工业自动化和控制系统网络安全标准系列。根据 IEC 的公开介绍，4-1 侧重产品安全开发过程，4-2 侧重组件技术安全要求，3-3 侧重系统安全要求及安全等级。[IEC 标准系列概览][iec-overview]

Siemens 的纵深防御思路同时覆盖工厂安全、网络安全和系统完整性。因此，固件校验只是其中一个环节。[Siemens 工业网络安全][siemens-security]

作为实习生，我现阶段可以先形成这些可检查的工作习惯：

- 给需求编号，说明触发条件、预期行为和测试证据。
- 记录哪些输入不可信，例如串口接收到的镜像长度、版本和命令。
- 将密钥、更新包、源代码和测试日志的职责分开。
- 除正常路径外，保存镜像损坏、升级中断、启动失败的测试结果。
- 记录组件版本以及已知限制，后续保留问题修复和依赖更新记录。

目前仅阅读了公开说明，尚未完成标准原文逐条分析、威胁建模、安全等级确定或认证评估。后续汇报应使用“参考 IEC 62443 的安全开发思路”，而不是“已经符合 IEC 62443”。

## 6. 从复位到 main：结合当前代码理解

### 6.1 向量表前两项为什么特别重要

Cortex-M7 复位启动会取得初始主栈指针 MSP 和复位入口；入口值的最低位用于 Thumb 状态，必须正确设置。向量表随后还包含异常和中断入口。[Arm Cortex-M7 用户指南][arm-guide]

本工程 [startup.S](../codes/src/startup.S) 的开头是：

```asm
vectors:
.word _stack_top
.word Reset_Handler
```

2026-10-13 对已构建的 `demo.elf` 做只读检查，得到：

```text
.isr_vector 起始字节：00 00 40 20  c5 00 00 00
_stack_top     = 0x20400000
Reset_Handler  = 0x000000c4
```

ELF 是 little-endian，所以第一组四字节表示 `0x20400000`，第二组表示 `0x000000c5`。复位处理函数的指令地址为 `0xc4`，表中记录的入口带有 Thumb 位，因而为 `0xc5`。这不是入口地址相差一个字节的错误。

本工程初始栈顶是 RAM 区域的末端边界，栈向较低地址增长。GDB 中观察到的 `sp = 0x203ffff0` 则是执行到 C 函数内部后的栈指针，不能与刚复位时的初始值混为一谈。

### 6.2 CPU 不会自动做好所有 C 初始化

当前 `Reset_Handler` 完成三件事：从 `_sidata` 复制初值到 `_sdata.._edata`，将 `_sbss.._ebss` 清零，然后 `bl main`。

| 对象 | 当前工程中的含义 | 只读符号检查结果 |
|---|---|---|
| `_sidata` | `.data` 初值的装载地址 | `0x00000170` |
| `_sdata` / `_edata` | `.data` 运行地址范围，左闭右开 | `0x20000000..0x20000004` |
| `_sbss` / `_ebss` | `.bss` 清零范围，左闭右开 | `0x20000004..0x2000000c` |
| `initialized_data` | 有初值的全局变量 | 初值 `0x12345678` |
| `zero_data` | 应为零的全局变量 | 在 `main` 中检查 |

因此，`main()` 不是这个程序的第一条执行指令。启动代码先准备好它依赖的基本运行条件。Arm 的 Cortex-M7 启动参考代码也展示了向量表和运行环境初始化的职责划分；该历史参考用于理解结构，不作为本工程新增依赖。[Arm CMSIS 启动示例][cmsis-startup]

### 6.3 链接脚本把哪些东西放在了哪里

[linker.ld](../codes/src/linker.ld) 为本例指定如下布局；物理区域对应关系见 [平台说明](../codes/docs/platform.md) 和 [QEMU 板级实现][qemu-source]。

| 区域 | 地址范围 | 本例用途 |
|---|---|---|
| ZBT SSRAM1 | `0x00000000–0x003fffff` | 向量表、指令、常量及 `.data` 初值 |
| ZBT SSRAM2/3 | `0x20000000–0x203fffff` | 可写数据、零初始化数据及栈 |

其中，`.data > RAM AT> CODE` 表示运行时在 RAM 区，初始内容放在 CODE 区；`KEEP(*(.isr_vector))` 防止链接垃圾回收丢弃向量表。`ENTRY(Reset_Handler)` 指定 ELF 入口信息，但不能替代处理器复位所需的向量表内容。

`CODE` 是工程给区域起的名字，该区域在此 QEMU 板型中实际是 SRAM。链接脚本中的 `rx` 也不等于已经配置了硬件写保护。以后做 Flash 驱动时，擦除粒度、写入限制、掉电保存和故障注入都需要单独建模。

### 6.4 当前示例的硬件初始化边界

当前代码没有配置真实芯片的时钟树、缓存、MPU、外设引脚或 UART。它依赖 QEMU 板型提供的启动环境，采用软浮点编译，并通过 CPUID 的 PartNo 字段确认 Cortex-M7 模型。

向量表当前只保留了 16 个系统向量表项，后面 14 项统一指向默认处理函数。这是最小示例的简化，包括保留槽位也被如此处理；尚未覆盖该板型的完整外部中断表。第二周及外设开发阶段，应按平台要求补齐有效处理函数并正确处理保留项。

`Default_Handler` 目前只会循环停留，便于定位异常，不属于已经设计好的工业故障恢复机制。

### 6.5 我对当前架构和后续模块的划分

当前实现中，构建工具在宿主机工作，启动逻辑在模拟目标机执行；GDB 是旁路观察工具，不是启动流程中的一环。

```text
宿主机：CMake → Make → Arm GCC / 链接器 → demo.elf
                                             ↓ 加载
目标机：QEMU mps2-an500 → 复位向量 → Reset_Handler → main
                                      ↓                ↓
                               复制 .data / 清 .bss   semihosting 输出
调试器：GDB ───────────── 经 gdbstub 暂停和观察目标机
```

正式 Bootloader 的模块拆分是后续设计草案：板级启动负责建立可运行环境；存储抽象负责读写与错误返回；镜像验证负责边界、完整性及签名；启动决策负责选择镜像和恢复状态；传输模块只负责收取候选数据；应用交接集中处理入口、栈和中断状态。

我希望先把这些职责分开，因为接收完文件不代表镜像可信，镜像可信也不代表应用能正常启动。测试可以分别制造传输截断、验证失败和应用卡死，判断出错的是哪一层，而不是把所有判断塞进一个 `main`。

### 6.6 硬件初始化需要建立哪些前提

我把硬件初始化理解为逐步满足执行条件：栈和存储器可用，C 全局数据已准备好，再配置需要的时钟与外设；开放中断前确认向量表和处理函数正确；使用 FPU、缓存或 MPU 前确认其配置和启动状态。

本例只实测了栈、入口和 C 数据初始化。QEMU 平台可供程序运行，不代表已经验证了某款 MCU 的 PLL、Flash 等待周期或引脚复用。第二周应先查当前板型提供哪些寄存器，再决定哪些初始化能在模拟器中验证，哪些需要真实硬件条件。

## 7. 输出和调试过程中的几个易混点

**看到文字输出，不代表 UART 已经工作。** 当前 `semihost()` 使用 `bkpt 0xab` 请求宿主环境服务。第二周需要实现真实的模拟 UART 寄存器收发，并用独立测试验证。

**QEMU 调试不需要先启动 OpenOCD。** 当前 GDB 直接连接 QEMU 自带的 gdbstub；OpenOCD 主要在后续选用相应真实硬件调试链路时再考虑。[QEMU GDB 文档][qemu-gdb]

**Reset_Handler 能到 main，不代表应用跳转已经完成。** 现在只有一个最小镜像。以后 Bootloader 跳到独立应用，还需要检查应用向量表、栈指针和入口，处理向量表基址、中断、SysTick 及遗留外设状态，并约定应用接管条件。这部分属于后续设计。

**只杀掉 QEMU 再重启，不一定构成有效断电测试。** 如果模拟存储内容完全丢失，就无法证明写了一半的升级状态能在下次启动被正确识别。故障测试需要保留模拟非易失存储的中间状态。

## 8. 可以复用的观察步骤

在项目目录下查看当前构建产物：

```sh
source scripts/env.sh
arm-none-eabi-objdump -s -j .isr_vector build/demo.elf
arm-none-eabi-nm -n build/demo.elf
```

本次只读检查的原始输出已保存到 [ELF 检查记录](../codes/logs/week1-elf-inspection.txt)。上述具体地址对应当前构建，修改代码后可能改变，应重新查看。

需要自行重做启动观察时，在第一个终端运行：

```sh
bash scripts/run.sh -S -gdb tcp:127.0.0.1:3333
```

第二个终端进入同一目录：

```sh
source scripts/env.sh
arm-none-eabi-gdb build/demo.elf
```

在 GDB 中输入：

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

这里列出的是交互复习步骤。本次 2026-10-13 的自动复核已实际检查复位寄存器、向量表、`main` 断点和单步，并增加初始化污染及失败路径测试；原始 GDB 命令和输出保存在独立日志目录。

```sh
python3 scripts/verify-startup.py
```

脚本每次创建全新构建目录和带时间戳的证据目录，记录源码与 ELF 的 SHA-256。通过 `logs/week1-review-latest.txt` 可以定位最近一次执行；它只是执行位置索引，是否通过应查看该目录的 `summary.json`。

## 9. 第一周交付与第二周准备

| 第一周交付项 | 当前记录 |
|---|---|
| 开发环境与 QEMU 示例验证 | 已完成，保留版本、编译、运行和 GDB 日志 |
| 标准工程目录和 Git 初始化 | 已完成；尚无提交，Git 作者姓名与邮箱未配置，不能宣称已形成版本历史 |
| 工业 Bootloader 需求与启动流程笔记 | 已补充来源正文核对、内存污染和负向用例；详见查漏记录与周报 |
| S7 内部启动实现 | 仅建立公开资料支持的分层认识；具体产品细节仍需型号与资料确认 |

第二周准备按以下顺序推进：先独立解释并整理启动汇编和链接脚本，再完善异常入口与运行环境初始化，最后开发 UART 输出并替换仅依靠 semihosting 的演示。时钟初始化要以选定模拟平台实际提供的硬件模型为准，不能直接照搬某款 STM32 的寄存器配置。

进入后续设计前，我需要向导师确认：是否指定某个 S7 CPU 作为参考；仿真平台是否保持 `mps2-an500`；Flash 持久化采用什么方案；“升级失败可恢复”的故障范围是什么；安全目标是功能演示还是需要进一步做标准条款映射。

目前可以在周报中准确表述为：

> 第一周已完成开发环境搭建及 Cortex-M7 最小裸机示例验证，结合向量表、链接布局和调试日志整理了复位到 C 入口的过程。通过 Siemens 公开资料，区分了固件安全启动与 PLC 用户程序启动，并形成了镜像校验、升级恢复和状态管理的初步需求。正式 Bootloader、UART 驱动和安全升级功能仍按后续周计划实现。

## 10. 资料记录

任务范围依据用户提供的《Industrial Embedded Systems Bootloader Development》第一周计划。以下均为官方资料，检索日期为 2026-10-13；标准部分使用公开概览，不替代标准全文。网页可能更新，产品能力应以具体型号和固件手册为准。

- [Siemens：S7-1500 CPU memory areas][s7-memory]，STEP 7 V20，2024-11。
- [Siemens：Startup OBs (S7-1500)][s7-startup]，STEP 7 V20，2024-11。
- [Siemens：Secure Boot for CPUs][s7-secure]，S7-1500/ET 200MP Manual Collection V20，2025-06；本次已核对正文。
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
