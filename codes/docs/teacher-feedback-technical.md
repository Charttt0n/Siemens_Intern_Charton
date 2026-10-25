# 老师反馈技术补充

2026-10-25 更新。范围为规划书 Week 1–4；反馈口述中的周次与源码归属分别记录，不搬动已有模块以匹配口述编号。

## UART 波特率计算与寄存器验证

本项补齐Week 3–4 UART0的计算依据、取整过程、寄存器读回及仿真边界。专项验证对两个阶段的生产固件分别干净构建并运行，两项均通过。Week4使用全FF镜像使Bootloader停在拒绝分支，读回的是Bootloader初始化后的寄存器，不是应用重新初始化后的状态。

### 公式与取整

本工程外设为CMSDK APB UART。设UART输入时钟为 fPCLK、目标波特率为 Btarget、整数分频为 D，则 Dideal = fPCLK / Btarget；分频后的理论波特率 Bactual = fPCLK / D。此BAUDDIV直接记录时钟分频值，无需在分母额外乘16。依据见Arm CMSDK手册4.3节、表4-7及4-10页的分频示例。[Arm CMSDK技术手册 DDI0479D](https://documentation-service.arm.com/static/5e8f224c7100066a414f7810)

输入 fPCLK = 25000000 Hz，Btarget = 115200 baud：

```text
Dideal = 25000000 / 115200 = 217.0138888889
整数商 q = 217
余数 r = 25000000 - 217 * 115200 = 1600
四舍五入阈值 = ceil(115200 / 2) = 57600
1600 < 57600，因此不进位，D = 217 = 0x000000D9
```

实际代码在 src/week3/uart.c 的 uart_init 中先做整除，再判断 r >= baud - baud/2 才进位；采用商和余数避免 clock_hz + baud/2 的加法溢出。两个输入须非零，取整后D须落在[16, 0xFFFFF]；不合格时返回-1，在修改寄存器之前退出。Week4复用同一驱动。此处记录源码规则，本次专项只新增默认配置的读回验证，不声称重跑了所有非法参数测试。

### 实际波特率及量化误差

```text
Bactual = 25000000 / 217 = 115207.3732718894 baud
差值 = Bactual - Btarget = +7.3732718894 baud
相对误差 = (Bactual - Btarget) / Btarget
         = 0.00006400409626
百分比误差 = 相对误差 * 100% = +0.006400409626%
ppm误差 = 相对误差 * 1000000 = +64.00409626 ppm
理论位宽 = D / fPCLK = 8.68 us
8N1连续发送一帧为10位，理论帧时间 = 86.8 us（不含帧间空闲）
```

正号表示分频结果略快于目标。误差只含整数分频量化误差，未计入晶振偏差、PLL/时钟树误差、抖动和对端时钟偏差，不能据此直接判断真机链路的总体容差。

### 寄存器读回与证据

复验命令：在 codes/ 执行 `python3 scripts/verify-uart-baud.py`。脚本在临时目录构建两个Bootloader，用QMP的info qtree定位MMIO基址0x40004000对应的UART0，读取其pclk-frq，随后经GDB读回32位寄存器并断言。证据根目录 BAUDLOG = `logs/uart-baud-20261024-235338`（相对 codes/）。该目录也保存构建输出、QEMU版本、GDB命令文件和ELF SHA256；本轮QEMU为11.1.2。

| 固定编号 | 操作与具体输入 | 预期 | 实际与判定 |
|---|---|---|---|
| BAUD-W3 | 干净构建Week3；默认空白存储；启动至UART提示符；读qtree和寄存器 | UART0 pclk-frq=25000000；BAUDDIV=217；CTRL=0x0B | 25000000；0x000000D9 (217)；0x0000000B；PASS |
| BAUD-W4 | 干净构建Week4；全FF镜像；停在REJECT后读qtree和寄存器 | 同上，读取Bootloader状态 | 25000000；0x000000D9 (217)；0x0000000B；PASS |

BAUD-W3证据：BAUDLOG/week3-qtree.txt、BAUDLOG/week3-registers.gdb、BAUDLOG/week3-registers.txt、BAUDLOG/week3/uart.log。BAUD-W4对应week4同名文件；汇总为BAUDLOG/summary.json。两个registers.txt均包含UART_BAUD_READBACK_PASS。

UART0基址为0x40004000；BAUDDIV偏移0x10，读回地址为0x40004010。CTRL偏移0x08，读回地址0x40004008；0x0B表示bit0 TX使能、bit1 RX使能、bit3 RX中断使能，bit6高速测试模式未开启。GDB可用 `x/wx 0x40004010` 和 `x/wx 0x40004008` 人工复核。

### 仿真验证边界

本轮证明模型UART0的时钟属性、固件写入的分频和使能寄存器符合设计，且启动字符可通过模型后端接收。QEMU 11.1.2的cmsdk-apb-uart.c通过整数除法将pclk_frq/bauddiv传给字符后端的串口参数接口，数值为115207；它与上述精确分频推导值的表示精度不同。[QEMU 11.1.2 UART模型源码](https://github.com/qemu/qemu/blob/v11.1.2/hw/char/cmsdk-apb-uart.c)

本工程使用Unix socket字符后端，接收的是字节流，没有测量物理TX/RX波形。因此终端字符正确、PONG应答成功、宿主日志时间间隔都不能作为115200物理线速、8.68 us位宽、对端容差或实物时钟准确性的实测证据。真机验收仍需确认实际UART输入时钟，并用示波器或逻辑分析仪测量重复0x55等数据的TX位宽，按 Bmeasured = 1/Tbit 计算实测波特率，再与目标及对端容差比较。此项尚未开展。

## PRIMASK 保存与恢复接口契约

接口位于 src/week2/irq.h，由 src/week3/uart.c 的初始化和队列消费复用。此公共基础接口已在前轮整理到前一阶段，本次仅补注释和可追溯测试，不移动文件，不改变驱动执行逻辑。第二阶段轮询UART没有接收队列临界区，不为形式统一增加调用。

### 调用约束与效果

`uint32_t irq_save(void)`：读取进入时PRIMASK，返回0或1，随后通过CPSID i设置PRIMASK=1。调用返回后才进入受保护区；读取和屏蔽是两条指令，不应把读取瞬间视为已经屏蔽。

`void irq_restore(uint32_t saved)`：通过MSR PRIMASK写回对应irq_save返回的原值。参数必须是这一层保存的值；本接口不做参数校验。正常、错误和提前返回路径都须配对恢复。嵌套必须后进先出，内层恢复内层值、外层恢复外层值；不得用固定0替代保存值，否则调用者原本屏蔽中断时会被错误解除屏蔽。

适用范围为单核Cortex-M特权执行环境。PRIMASK=1阻止可配置优先级异常激活，不屏蔽NMI和HardFault；恢复0仅解除PRIMASK这一层屏蔽，不保证NVIC、BASEPRI及FAULTMASK等其他条件允许中断立即执行。接口不改这些寄存器，也不清除pending状态。语义依据：[Arm CMSIS Core寄存器说明](https://arm-software.github.io/CMSIS_6/main/Core/group__Core__Register__gr.html)。

内联汇编的memory clobber约束编译器对内存访问的重排，不等同于DMB或DSB硬件屏障。该接口不提供多核互斥，不替代Bootloader跳转时的完整处理器状态交接；设备操作需要的硬件屏障由调用处按需求安排。临界区应保持短小，当前UART消费端只访问队列和索引。

```c
uint32_t outer = irq_save();
/* 外层临界区 */
uint32_t inner = irq_save();
/* 内层临界区 */
irq_restore(inner); /* 仍为1 */
irq_restore(outer); /* 回到调用者的0或1 */
```

### 专项验证方法

在 codes/ 执行 `python3 scripts/verify-irq-contract.py`。脚本临时交叉编译实际src/week3/uart.c与tests/test_irq.c，在QEMU AN500 Cortex-M7中执行MRS、CPSID、MSR指令。初始状态分别用独立CPSIE i及CPSID i建立，不用被测irq_restore设置前置条件。每组11个断言，每个断言打印实际值、预期值和OK/FAIL；两组全部通过，QEMU退出0。

证据根目录 IRQLOG = `logs/irq-contract-20261024-235737`（相对codes/）：build.txt记录编译命令及退出码，qemu.txt记录完整逐项结果和退出码，summary.json记录两组判定与ELF SHA256，qemu-version.txt记录版本。测试用semihosting输出结果并退出，仅用于测试固件，不引入生产固件。

| 固定编号 | 输入与操作步骤 | 预期结果 | 实际结果与证据 |
|---|---|---|---|
| IRQ-INIT-0 | CPSIE i建立0；外层save；内层save；内层restore；外层restore；实际uart_init；空队列uart_getc | 外层保存0；内层保存1；内层恢复1；外层恢复0；UART调用后保持0 | 11个断言全部OK；PASS；IRQLOG/qemu.txt中GROUP initial=0 |
| IRQ-INIT-1 | CPSID i建立1；依次执行相同操作 | 外层保存1；内层保存1；两层恢复都为1；UART调用后保持1 | 11个断言全部OK；PASS；IRQLOG/qemu.txt中GROUP initial=1 |

下表每一行对应日志中的同名断言。两列使用“预期/实际”格式，所有行均为PASS；联合固定编号由组编号与断言名构成，例如IRQ-INIT-0/OUTER_RESTORE。

| 断言名 | 检查点 | 初值0 预期/实际 | 初值1 预期/实际 |
|---|---|---|---|
| ENTRY | 独立设置后读PRIMASK | 0/0 | 1/1 |
| SAVE_RETURN | 外层irq_save返回值 | 0/0 | 1/1 |
| SAVE_MASK | 外层save后读PRIMASK | 1/1 | 1/1 |
| NEST_RETURN | 内层irq_save返回值 | 1/1 | 1/1 |
| NEST_MASK | 内层save后读PRIMASK | 1/1 | 1/1 |
| INNER_RESTORE | 恢复内层后读PRIMASK | 1/1 | 1/1 |
| OUTER_RESTORE | 恢复外层后读PRIMASK | 0/0 | 1/1 |
| UART_INIT_RETURN | uart_init(25000000,115200)返回值 | 0/0 | 0/0 |
| UART_INIT_MASK | 初始化调用后读PRIMASK | 0/0 | 1/1 |
| UART_GETC_RETURN | 空接收队列uart_getc返回值 | 0/0 | 0/0 |
| UART_GETC_MASK | 队列消费调用后读PRIMASK | 0/0 | 1/1 |

本轮验证范围为默认配置、空队列路径、两层嵌套和特权模式下的掩码读回。不将其扩展为非特权调用有效性、NMI/HardFault注入、所有异常出口或真机中断延迟的测量结论。既有FB-IRQ-02仍保留历史证据；当前tests/test_irq.c仍输出两条PASS汇总，兼容旧补测脚本的检查条件。

## TX 持续阻塞的有限轮询与错误传播

2026-10-25补测直接覆盖持续TX_FULL，7组专项检查全部通过：两版驱动各10个断言、3个生产目标隔离检查，以及Week3输出、Week4输出、Week4跳转前flush和Week4应用输出四条错误路径。每组都有固定编号、输入、预期和实际结果及日志。本轮重点为错误传播及有限退出，不把宿主看门狗终止当作驱动超时成功。

### 注入方式与复验

在 codes/ 执行 `python3 scripts/verify-tx-full.py`。测试在临时工程副本中建立独立测试目标，启用UART_TX_FAULT_TEST，使用tests/uart_tx_test.h及tx_fault_state.c提供的易失状态、DATA替身和状态读取计数器。src/week2/uart.c与src/week3/uart.c仍执行原有uart_flush、uart_putc和uart_write函数；没有另写一套轮询算法。注入使每次TX状态读取都返回1，持续到函数自行返回。

两版驱动均交叉编译到Cortex-M7，在QEMU上执行。第三阶段Bootloader及第四阶段Bootloader复用week3驱动，第四阶段应用使用week2驱动。完整上层测试复用实际main和错误处理代码，通过GDB在idle函数处读取状态；初始测试DATA为0x55，禁用生产UART输出对DATA替身的物理发送。不能仅以UART没有文本作为成功证据，须同时满足返回值、读取次数和状态断言。

正常构建不定义UART_TX_FAULT_TEST，直接访问原MMIO寄存器。正常bootloader-week3.elf、bootloader-week4.elf、application.elf均重新构建，并通过符号表确认不含uart_tx_test_*。本次生产路径只增加编译期选择，不改变轮询上限和错误语义。

证据根目录 TXLOG = `logs/tx-full-20261025-015904`（相对codes/）。summary.json汇总7组结果及ELF SHA256；每个.txt日志保存实际命令、完整输出及退出码；各错误路径同时保存.gdb脚本和-uart.log。

### 驱动返回值与计数

两组均先初始化UART至TX使能，设置TX_FULL=1、读取计数=0、DATA=0x55；依次调用flush、putc('A')、write("ABC")，随后令TX_FULL=0，重新发送"A"。下表每项分别在TX-WEEK2-DRIVER和TX-WEEK3-DRIVER下执行，联合编号为组编号/断言名，20个断言全部OK。证据分别为TXLOG/week2-driver.txt、TXLOG/week3-driver.txt。

| 断言名 | 具体输入或操作 | 预期结果 | 两版实际结果 |
|---|---|---|---|
| INIT | uart_init(25000000,115200) | 返回0 | 0；OK |
| FULL_FLUSH_RETURN | 持续TX_FULL=1；uart_flush() | -1 | -1；OK |
| FULL_FLUSH_READS | 上一步返回后读计数 | 1000000 | 1000000；OK |
| FULL_PUTC_RETURN | uart_putc('A') | -1 | -1；OK |
| FULL_PUTC_TOTAL_READS | flush和putc累计状态读取 | 2000000 | 2000000；OK |
| FULL_WRITE_RETURN | uart_write("ABC") | 首字符发送失败即返回-1 | -1；OK |
| FULL_WRITE_TOTAL_READS | 三次调用后累计状态读取 | 3000000，未继续发送B/C | 3000000；OK |
| FULL_DATA_UNCHANGED | 读取测试DATA | 仍为0x55，无失败写入 | 0x55；OK |
| RECOVER_WRITE_RETURN | TX_FULL=0，计数清零；write("A") | 0 | 0；OK |
| RECOVER_DATA_READS | 恢复后读DATA和计数 | DATA=0x41，读取1次 | 0x41、1；OK |

两组QEMU测试均由固件断言完成后主动退出0，包含TX_FULL_CONTRACT_PASS；脚本10秒外层限制仅防止测试挂死，本轮未触发。

### 上层失败状态与成功标记

| 固定编号 | 操作步骤与具体输入 | 预期结果 | 实际结果 |
|---|---|---|---|
| TX-PRODUCTION | 重新构建3个正常目标；nm检查符号 | 无注入变量 | 三个ELF均不含uart_tx_test_*；PASS |
| TX-W3-PRINT | 持续FULL=1、DATA=0x55；运行实际Week3 main；断在fault_idle | print收到write失败；boot_status=3；有限退出 | boot_status=0x3；读取1000000次；DATA=0x55；PASS |
| TX-W4-PRINT | 有效镜像，持续FULL=1；运行实际Week4 main；断在boot_idle | 初次输出失败；boot_status=0x101；不跳转应用 | 0x101；读取1000000次；DATA=0x55；PASS |
| TX-W4-FLUSH | 有效镜像；FULL=0完成校验和输出；在main的最终flush前改为FULL=1 | 最终flush失败，boot_status=0x101；停在boot_idle | 0x101；该次flush新增读取1000000次；PASS |
| TX-W4-APP | 正常Bootloader加载有效的注入应用；应用FULL=1；断在app_idle | 交接正常；应用print失败；app_status=4；不进入运行成功状态 | handoff_ok=1；app_status=4；读取1000000次；DATA=0x55；PASS |

TX-PRODUCTION证据为TXLOG/bootloader-week3-symbols.txt、bootloader-week4-symbols.txt、application-symbols.txt。其他4组各对应TXLOG/编号.txt、编号.gdb、编号-uart.log。例如TX-W4-FLUSH对应TXLOG/TX-W4-FLUSH.txt。

四条错误路径GDB断言均退出0，均没有PONG或APP: RUNNING。Week4应用用例允许Bootloader先输出自身的CRC/LOAD成功信息；最终是否运行成功须同时检查app_status和APP: RUNNING，不能把之前的中间成功行当作应用成功。GDB取证后由测试脚本终止并回收QEMU，不能将这个宿主清理退出码当作固件成功退出。

### 轮询预算的边界

一百万次限制是每次uart_flush最多读取状态一百万次的预算，不是100万毫秒，也不是固定的毫秒超时。执行时长受CPU时钟、编译优化、MMIO访问开销、中断抢占及QEMU调度影响。测试计数器本身也增加开销，因此不把本轮宿主耗时换算为真机超时。

uart_putc先flush，失败时不写DATA；uart_write对非空字符串逐字符调用putc，遇到首个失败立即返回-1。本次持续阻塞在首字符即发生；空字符串没有待发送字节，不要求因FULL而失败。发送多字节的总预算与字符数、阻塞位置有关，调用者另行调用flush也有独立预算。若产品要求准确毫秒超时，应使用明确时基和截止时间，并对抢占、时钟变化和计数回绕另行设计验证。

故障注入替代的是UART状态和DATA观测点，验证了Cortex-M7上驱动控制流及实际调用者的错误状态。它没有制造QEMU字符后端的物理持续拥塞，也未验证真机串口线上的阻塞时长。UART失效时不依赖再发送一条错误文本，使用GDB读取boot_status/app_status作为证据。

### 正常路径回归

未启用注入的第三阶段11项、第四阶段27项回归均通过。新证据为 `logs/week3-20261025-015915-135199` 和 `logs/week4-20261025-015917-068252`。既有逐项报告保留原始证据路径；这两份目录是本次新增的生产分支回归证据，不替换先前日志。

## UART 命令完成到 QMP cont

![UART检查点时序](images/uart-checkpoint-sequence.png)

固件处理命令，输出完整响应及提示符“> ”。宿主response收到提示符才将quiescent置真；checkpoint要求该状态，然后发送stop并等待成功响应，发送pmemsave导出0x20300000起1MiB。宿主检查长度，写同目录临时文件、fsync文件、os.replace原子替换、fsync目录。finally发送cont。保存中断或replace失败时会尝试恢复QEMU；replace前失败保持旧镜像。若目录fsync在替换后失败，则新文件可能已经可见，不承诺回退旧文件；若QMP断开则cont可能失败，向调用者报告错误。

本实现只有一个同步命令发送者；提示符证明本轮命令处理完成，不等同于任意并发设备均静止。W3-06/07/08/09覆盖成功、replace失败恢复及重启，qmp.json同时保留请求和响应。

## 向量合法范围与边界覆盖

本工程固定AN500、48项向量和192字节向量表。范围规则来自src/week4/image.c的vectors_ok，image_validate与boot_verify_loaded共同调用；宿主打包器scripts/image_format.py实施相同的输入规则。下表是本工程镜像契约，不将其扩展为所有Cortex-M芯片的统一地址规则。

| 检查对象 | 精确规则 | 边界含义 | 验证与证据 |
|---|---|---|---|
| 向量表位置 | payload复制到0x00010000；向量表[0x10000,0x100C0)，共48项 | load必须为固定地址；表本身按256字节对齐 | W4-BUILD、W4-HANDOFF；链接与GDB证据 |
| 初始MSP vector[0] | 0x20010000 < MSP <= 0x20200000；MSP模8=0 | 下界不接受；上界接受；保留Bootloader RAM | VEC-001..015；MSP-AT-BASE |
| Reset vector[1] | entry位0必须1；addr=entry按位与0xFFFFFFFE | Thumb位与地址分别检查，不按4字节对齐处理 | Reset索引7个边界；RESET-BELOW |
| 所有已实现异常及IRQ入口 | 0x100C0 <= addr < 0x10000+payload_size，且entry位0=1 | 代码下界接受；上界不接受；零入口拒绝 | 42个必填索引各7项；VEC-MATRIX |
| 核心保留项 | 索引7、8、9、10、13必须为0 | 每一项单独验证0可接受、非0拒绝 | 5项各2个断言 |
| 外部IRQ | 索引16..47对应IRQ0..31，每项必须为非零合法Thumb入口 | 未使用IRQ也要配置合法默认处理函数 | 全部32项都测，不仅测IRQ0 |
| 复制后的复核 | 先CRC，再用同一规则检查MSP及全部入口 | 先修正测试CRC才能直接触达向量拒绝分支 | LOAD-VECTOR-MSP、LOAD-VECTOR-IRQ31 |

新证据VLOG = `logs/vector-bounds-20261025-021521`（相对codes/）。`python3 scripts/verify-vector-bounds.py`在干净构建中执行321个宿主断言（ASan/UBSan），再做4个CPU镜像拒绝、2个复制后向量复核及2个状态路径验证。完整321项的输入、预期、实际、文件行号见codes/docs/vector-boundary-cases.json；原始输出为VLOG/matrix.txt。

| 覆盖类别 | 输入及预期 | 本轮结果 |
|---|---|---|
| MSP上下界 | 0、base-8、base、base+8、top-8、top、top+8、0xFFFFFFF8 | 只有base+8、top-8、top接受；8项PASS |
| MSP对齐 | base+8再分别加1..7 | 7种余数全部返回7；PASS |
| 每个必填入口 | 最低合法Thumb地址、低于代码区、最高内部地址、等于末端、超过末端、清Thumb位、零 | 42×7=294项PASS |
| 每个保留项 | 零与0x100C1 | 5×2=10项PASS |
| 表长边界 | payload=191、192 | 191返回长度错误4；192无代码可容纳合法入口，返回向量错误7；2项PASS |
| CPU拒绝 | MSP恰为RAM下界、保留项13非零、IRQ31恰到末端、Reset低于代码区 | 各输出REJECT: 0x00000007；4项PASS |
| 加载后复核 | 运行区MSP改为下界、IRQ31入口改零；更新boot_image.crc以越过CRC检查 | 两项均在boot_verify_loaded返回7；PASS |

对于基准972字节payload，代码起始地址范围为[0x100C0,0x103CC)；最低合法entry为0x100C1，最高合法entry为0x103CB，entry=0x103CD解码为末端，拒绝。接受边界值的宿主用例只验证地址判定，没有把修改成任意内部地址的镜像启动执行。当前规则只检查入口起始地址、Thumb位及范围，不证明该地址上的完整指令或函数语义有效；实际应用运行证据仍来自未修改的合法应用。最低可执行镜像还须包含代码，不能把192字节长度字段检查通过解释为整个镜像通过。

<!-- landscape -->

## 完整状态转移图

![完整启动状态及失败去向](images/boot-state-complete.png)

<!-- portrait -->

图中所有Bootloader失败最终都进入boot_idle循环，应用自己的成功或失败最终进入app_idle。当前没有备份槽切换、重试下载或自动恢复分支。完整可编辑图源为codes/docs/images/boot-state-complete.mmd，渲染脚本为codes/scripts/render-boot-state.py。图中“通过”表示继续下一步，不表示最终应用运行成功。

boot_status不是进度枚举：它在BSS初始化时为0，成功交接前也会被写为0；中间校验阶段没有逐状态写入。判断应用成功须结合APP: RUNNING、app_status=1及交接/CRT/SVC检查。应用出错时boot_status可能仍为0，应读取app_status和app_exception。

## REJECT 输出与状态字典

src/week4/main.c的reject(result)先把result写入boot_status，再依次输出前缀、十六进制码、CRLF和提示符，最后进入boot_idle。精确格式如下，其中h为小写十六进制数字；引号表示字符串边界，不是发送字节：

```text
"REJECT: 0xhhhhhhhh\r\n> "
```

前缀中的空格、0x、恰好8位十六进制和末尾提示符后的空格都是输出的一部分。`\r\n`表示CR/LF两个字节，不是发送反斜线字符；提示符之后没有额外换行。唯一字段是无符号32位错误码，来源是image_result，不含地址、时间、序号，也不是宿主进程退出码。

| boot_status值 | 名称与触发条件 | REJECT行为 | 最终停留及证据 |
|---|---|---|---|
| 0 | BSS初值，或完成最终flush后准备交接 | 不输出REJECT；单值0不证明应用成功 | 初期位置不定；交接后应用app_idle；W4-HANDOFF |
| 1 | IMAGE_IO：设备未绑定、读取失败等 | REJECT: 0x00000001 | boot_idle；VLOG/STATUS-IO.txt及-uart.log |
| 2 | IMAGE_FORMAT_ERROR：magic、format、header_size不符 | REJECT: 0x00000002 | boot_idle；IMG-02..05 |
| 3 | IMAGE_HEADER_CRC：前28字节CRC不匹配 | REJECT: 0x00000003 | boot_idle；IMG-06 |
| 4 | IMAGE_LENGTH：长度小于表、超过槽/执行区，或load/verify重复边界无效 | REJECT: 0x00000004 | boot_idle；IMG-07/08；load参数分支按源码映射 |
| 5 | IMAGE_ADDRESS：load不等于0x10000 | REJECT: 0x00000005 | boot_idle；IMG-09 |
| 6 | IMAGE_PAYLOAD_CRC：存储payload CRC不匹配 | REJECT: 0x00000006 | boot_idle；IMG-10 |
| 7 | IMAGE_VECTOR：存储或运行区向量非法 | REJECT: 0x00000007 | boot_idle；向量矩阵及LOAD-VECTOR两项 |
| 8 | IMAGE_COPY_CRC：运行区CRC与已校验info.crc不符 | REJECT: 0x00000008 | boot_idle；W4-COPY |
| 0x100 | CPU异常：记录exception_number=IPSR，再写状态 | 异常路径不调用reject，无REJECT保证 | boot_idle；VLOG/STATUS-HARDFAULT.txt读回exception_number=3 |
| 0x101 | UART初始化、write或最终flush失败 | UART不可用，不保证错误文本或完整REJECT | boot_idle；TX-W4-PRINT、TX-W4-FLUSH |

VLOG = `logs/vector-bounds-20261025-021521`（相对codes/）；IMG编号对应第四阶段输入表的宿主和UART日志，TX编号对应TX专项报告。状态1由GDB在image_validate入口将已绑定设备的memory指针置零触发实际flash_read失败；0x100由无效取指触发真实HardFault验证。它们分别是数据读取错误注入和CPU异常测试，不是篡改boot_status伪造结果。各load参数失败的所有子条件尚未逐一做GDB注入，应以表中具体验证范围为准。

应用使用独立状态：app_status=0为初值，1为CRT/交接/SVC通过后的运行状态，2为CRT、交接或SVC检查失败，3为应用异常（记录app_exception），4为UART失败。最终均在app_idle循环。app_status=4已由TX-W4-APP验证；不能将应用状态值套用为Bootloader的image_result。

格式错误示例（只展示拒绝行及提示符）：

```text
"REJECT: 0x00000002\r\n> "
```

复制损坏示例的完整UART为：

```text
BOOT: stage4 image validation\r\n
IMAGE: CRC32 PASS\r\n
"REJECT: 0x00000008\r\n> "
```

UART在reject输出途中失败时，可能留下不完整前缀，boot_status最终被print改写为0x101；这时以GDB状态取证，不要求再靠同一UART输出错误行。IRQ唤醒后idle函数仍循环，不自动恢复启动流程。

## 独立 manifest schema 与示例

交付manifest是包根目录的delivery-manifest.json，用于记录列出的交付文件路径、字节数和SHA256，帮助老师检查复制完整性及复核材料。它不进入Bootloader解析流程；固件仍使用32字节M7IM二进制头。manifest不是签名或老师批准证明，approval_status固定为pending-teacher-review。

独立schema路径为codes/schemas/delivery-manifest.schema.json，采用JSON Schema Draft 2020-12；实现为codes/scripts/delivery_manifest.py。校验器实现本schema使用的type、required、additionalProperties、const、enum、pattern、minItems、minimum及date-time检查，不作为任意schema的通用执行器。

| 字段 | 约束 | 本轮检验 |
|---|---|---|
| 根对象 | 6字段全部必填；不允许额外字段 | 各字段缺失及UNKNOWN均拒绝 |
| schema_version | 常量整数1，布尔true不作1接受 | VERSION、BOOLEAN-VERSION |
| plan_group | week1-2、week3、week4；week3-4仅兼容历史合并包 | GROUP拒绝week5 |
| generated_at | ISO日期时间，必须+08:00；秒必填，可有小数；日期必须真实有效 | DATE、DATE-TEXT、ZONE |
| image_format | 常量M7IM-v1 | FORMAT |
| approval_status | 常量pending-teacher-review | APPROVAL拒绝approved |
| files | 非空数组；每项只允许path、size、sha256，均必填 | FILES-TYPE、EMPTY、FILE-MISSING等 |
| files[].path | 相对POSIX路径；无绝对路径、盘符、反斜线、控制字符、.或..分段、双斜线；规范化后不变 | TRAVERSAL、ABSOLUTE、DOT等 |
| files[].size | 非负整数，拒绝布尔和小数 | SIZE、BOOL-SIZE、FLOAT-SIZE |
| files[].sha256 | 64个小写十六进制字符 | HASH、HASH-UPPER |

规范化路径、重复路径拒绝、真实文件存在性、长度/哈希、符号链接越界属于校验器的附加语义检查。基本字段形状由schema约束；schema本身不读取文件。生成器扫描目录中的文件，排除delivery-manifest.json自身以避免自引用；校验器核对清单列出的文件，不声称对未列出的额外文件做完整性认证。

有效示例完整JSON如下。配套README.txt字节存放在codes/schemas/examples/，其长度和SHA256与清单一致。

```json
{
  "schema_version": 1,
  "plan_group": "week3-4",
  "generated_at": "2026-10-25T02:16:04.132229+08:00",
  "files": [
    {
      "path": "README.txt",
      "size": 25,
      "sha256": "798fea15a540e232f4ae06c1888d0467da504195abb547083ebfec97ceb955fb"
    }
  ],
  "image_format": "M7IM-v1",
  "approval_status": "pending-teacher-review"
}
```

无效示例：缺少必填files字段，应抛出ValueError，不能作为空清单接受。

```json
{
  "schema_version": 1,
  "plan_group": "week3-4",
  "generated_at": "2026-10-25T02:16:04.132229+08:00",
  "image_format": "M7IM-v1",
  "approval_status": "pending-teacher-review"
}
```

完整的另外两份无效JSON为schemas/examples/invalid-traversal.json和invalid-hash.json，分别把path设为../escape和sha256设为xyz。有效与三个无效示例均在本轮脚本中生成并验证；校验成功0退出，字段/文件失败抛异常，命令非零退出。

复验：在codes/执行`python3 scripts/verify-manifest.py`。本轮41项全部通过，证据MLOG = `logs/manifest-20261025-021604`（相对codes/），每项完整输入JSON和预期/实际异常见summary.json及对应JSON文件，包括有效实例、6个必填字段、各标量/数组类型、日期、大小、哈希、路径、重复、缺失文件与符号链接越界。

生成正式交付清单：`python3 scripts/delivery_manifest.py 包目录 --group week4`；再次核验：`python3 scripts/delivery_manifest.py 包目录`。打包后还校验ZIP完整性。清单能检测非故意复制错误，但SHA256清单可被一同修改，不具备签名认证能力。

## 修改记录与版本追踪

逐项记录位于planning/teacher-feedback-2026-10-24.md：每条均列反馈编号、修改位置、验证用例、证据路径、修订版本和老师确认状态。版本清单位于planning/feedback-version.json，记录Git基线、待提交状态及本轮源文件内容指纹。老师确认日期和结论由实际反馈填写，当前统一待确认，不用测试通过替代老师批准。


2026-10-25归档补充：Week3与Week4周报现分别位于reports/week3与reports/week4。交付清单plan_group新增week3、week4，保留week3-4兼容历史快照；此枚举扩展不改变原有校验约束。原跨周测试证据及实例保留执行时版本。
