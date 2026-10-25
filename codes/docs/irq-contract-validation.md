# PRIMASK 接口与恢复测试报告

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
