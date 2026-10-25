# TX 持续阻塞与错误传播测试报告

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
