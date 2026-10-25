# Week3 存储操作测试记录

更新日期：2026-10-25　测试平台：QEMU MPS2 AN500 Cortex M7

## 一 验收结论与证据版本

本报告对应原八周规划第3周的存储操作测试交付物。本次从正式codes工程执行verify-week3.py，干净构建后完成11组阶段检查，全部PASS。其中存储相关覆盖同一Flash断言集的宿主/CPU执行、分区与读写擦除边界、显式检查点、保存失败恢复、强制终止恢复及错误镜像长度；UART中断/命令检查作为相关集成证据保留，不将11组统称为11种独立存储场景。

最新证据W3LOG = logs/week3-20261025-192322-302943（相对codes/）。10月24日及本日早期记录保留为历史；当前交付结论以本次summary.json为准。生产ELF SHA256：a8b558aab31f286f01b3e643c236ce6568064ec0c10ce3763192bc580fc24477。

原计划的4小时是预计投入，并非实际工时记录。测试通过支持当前SRAM NOR模型与显式保存语义，不证明真实片内Flash或自动掉电安全；老师确认仍待填写。

## 二 方法 前置条件与判定

宿主路径编译真实flash.c和test_flash.c，开启ASan/UBSan；同一flash_selftest断言集随后在Cortex-M7上运行。断言为失败立即返回源码行号：宿主退出0且打印PASS、CPU输出SELFTEST PASS，说明执行到的全部断言成立。原始日志没有逐条打印每次比较的返回值，本文按源码条件展开预期及通过结论，不虚构逐条采样值。

固定数据D = F0 55 AA 00 11 22 33 44；全1输入为8个FF；混合输入M = 00 00 00 00 00 00 00 FF。初始化1 MiB存储为FF。可写分区P依次取APP、BACKUP、PARAMS，容量L分别为0x70000、0x70000、0x10000。GDB、UART与QMP记录共同核对目标状态、实际字节及持久化结果。

验证路径：工具与干净构建 → 指定被测ELF → 实际输入/故障注入 → 输出与状态断言 → 原始日志 → summary.json。预期拒绝算通过；意外退出、断言失败或非预期超时算失败。

## 三 存储API输入与结果明细

以下ST编号为本次文档的断言归纳编号，对应W3-02/W3-03中的同一断言集，不代表新增测试次数。各行“通过”依据native-flash.txt退出0及irq/uart.log中的SELFTEST PASS；完整表达式和源码行号见week3-storage-validation.json。

| 编号 | 方式与实际输入 | 预期结果 | 实际与证据 |
|---|---|---|---|
| ST-01 | bind(NULL,memory,1MiB)；bind(dev,NULL,1MiB)；size=1MiB-1；未绑定read | FLASH_ARGUMENT=-1 | 对应CHECK成立；宿主/CPU通过；native-flash.txt、irq/uart.log |
| ST-02 | 有效bind；顺序遍历四区offset/size并累加 | 返回0；连续覆盖0x100000；均4KiB对齐 | 对应CHECK成立；同上 |
| ST-03 | partition_get(-1/COUNT)；无效分区read；NULL设备read；非零read输出NULL | get返回NULL；read返回-1 | 对应CHECK成立；同上 |
| ST-04 | APP偏移0x70000、输出NULL、长度0 | 合法空读，返回0 | 对应CHECK成立；同上 |
| ST-05 | APP偏移0x70000读1字节；读offset=UINT32_MAX | 越界，FLASH_RANGE=-2 | 对应CHECK成立；同上 |
| ST-06 | APP偏移4写UINT32_MAX长度；offset=UINT32_MAX擦4KiB | 范围错误-2，不因加法溢出放行 | 对应CHECK成立；同上 |
| ST-07 | 编程offset=1,size=4；offset=0,size=3 | FLASH_ALIGNMENT=-3 | 对应CHECK成立；同上 |
| ST-08 | 编程长度0；输入NULL且长度4 | FLASH_ARGUMENT=-1 | 对应CHECK成立；同上 |
| ST-09 | 擦除offset=1,size=4096；offset=0,size=4；size=0 | 前两项-3；长度0返回-1 | 对应CHECK成立；同上 |
| ST-10 | BOOT编程D的前4字节；BOOT擦4096字节 | FLASH_PROTECTED=-4 | 对应CHECK成立；同上 |
| ST-11 | read输出指向后端；program输入指向后端 | FLASH_ARGUMENT=-1，拒绝别名绕过 | 对应CHECK成立；同上 |
| ST-12 | 各P从L-4写8字节；从L-4096擦8192字节 | 两者-2；不得跨分区 | 对应CHECK成立；同上 |
| ST-13 | 各P偏移0写D；重复写D；随后读8字节 | 写/重复写/读均0；8字节逐项等于D | 对应CHECK成立；同上 |
| ST-14 | 已写D位置改写8个FF | FLASH_NEEDS_ERASE=-5 | 对应CHECK成立；同上 |
| ST-15 | 已写D位置编程M，末字节违反1→0规则 | 返回-5；再读8字节仍为D，前面合法字节也未部分修改 | 对应CHECK成立；同上 |
| ST-16 | 各P最后4字节写D的前4字节，擦P首4096字节 | 写/擦均0；首块逐字节FF，末端写入仍保留 | 对应CHECK成立；同上 |
| ST-17 | 擦各P最后4096字节；最终扫描整个1MiB | 返回0；整块逐字节FF，无分区外残留修改 | 对应CHECK成立；同上 |

输入均按上述顺序在独立测试存储内执行，不能任意打乱前置状态。当前断言未对所有API参数的笛卡尔组合或真实硬件故障穷举；软件参数预校验的“不部分修改”不等同于运行中断电原子性。

## 四 QEMU持久化与操作记录

命令为单字节；主机交互界面按回车提交。下列\r\n表示CR/LF字节，末尾提示符含空格。

| 编号 | 实际操作与输入 | 预期输出或状态 | 实际结果与路径 |
|---|---|---|---|
| STORE-01 / W3-06 | 新Session；r,w,r；checkpoint；逐字节比对1MiB | PARAM 0xffffffff；OK；PARAM 0x4b4f3357；文件仅[0xF0000,0xF0004)=W3OK，其余FF | UART逐字节匹配，文件全量匹配；write/uart.log、summary.json |
| STORE-02 / W3-06 | 第一Session占用同一镜像时尝试第二Session | 拒绝第二写者，异常含already in use | 异常断言通过；summary.json；检查位置verify-week3.py |
| STORE-03 / W3-07 | 保存W3OK后e；令os.replace抛OSError，再checkpoint；随后p | 保留旧镜像；错误向上返回；finally尝试cont；PONG | 全量旧镜像相同，PONG字节匹配；write/qmp.json、write/uart.log、summary.json |
| STORE-04 / W3-08 | 上一步擦除未保存；SIGKILL并回收；重新加载旧镜像后r | 恢复上次保存的W3OK，而非未保存擦除 | PARAM 0x4b4f3357\r\n> ；restart/uart.log |
| STORE-05 / W3-09 | e后显式checkpoint；新QEMU进程r；读整个文件 | PARAM 0xffffffff；文件1MiB全FF | UART与整镜像断言通过；erased-restart/uart.log、summary.json |
| STORE-06 / W3-10 | 3字节文件bad作为镜像启动 | ValueError含exactly；原bad三字节不修改 | 异常和文件不变断言通过；summary.json；检查位置verify-week3.py |

完整命令往返样例：r→PARAM 0xffffffff\r\n> ；w→OK\r\n> ；r→PARAM 0x4b4f3357\r\n> 。QMP请求和响应保存在qmp.json，可对应stop、pmemsave及cont顺序。文件互斥、旧文件比较和异常类型由脚本断言检查，不宣称UART本身输出这些宿主断言。

## 五 完整阶段用例表

保留W3-01至W3-11编号，执行表与结果表通过编号连接。W3LOG统一指向本次最新证据。存储API详表、宿主保存流程表及本节存在覆盖重叠，不能相加为独立用例总数。

## 执行表

日志根目录：`W3LOG = logs/week3-20261025-192322-302943`（相对 codes/）。表内路径由该根目录和文件名组成。

| 固定编号 | 测试方式 | 操作步骤 | 具体输入 |
|---|---|---|---|
| W3-01 | 新构建 | 1. 在新目录配置CMake；2. 构建bootloader-week3.elf及week3-test.elf；3. 执行size和objdump；4. 检查退出码及BKPT。 | 仅构建stage3与test目标 |
| W3-02 | 宿主Flash API | 1. cc编译flash.c和test_flash.c并启用ASan/UBSan；2. 运行native测试；3. 运行完整CHECK断言集并检查进程退出码。 | test_flash.c；初始1MiB全FF；ASan/UBSan |
| W3-03 | CPU Flash API | 1. 启动week3-test.elf；2. 固件执行同一flash_selftest；3. 收集串口至提示符；4. 断言SELFTEST PASS。 | 同一test_flash.c在Cortex-M7运行 |
| W3-04 | RX中断溢出 | 1. GDB检查UART/NVIC/向量配置；2. 设置test_pause_rx=1；3. 串口发送200字节；4. GDB读取计数及队列逐字节比较；5. 清队列并恢复消费。 | 暂停消费，串口注入bytes(range(200)) |
| W3-05 | 环形队列与命令 | 1. 连续发送300次p并逐次等待提示符；2. 发送?；3. 发送s；4. 检查PONG、ERROR及统计值。 | 300次p，随后?及s |
| W3-06 | 检查点与互斥 | 1. 发送r/w/r；2. 执行checkpoint；3. 构造1MiB预期镜像并逐字节比较；4. 同镜像再开Session，验证互斥拒绝。 | r,w,r；save；另一Session打开同镜像 |
| W3-07 | 保存失败恢复 | 1. 保存已有W3OK镜像；2. 发送e；3. 用mock令os.replace抛OSError；4. 调checkpoint并捕获异常；5. 比较旧文件；6. 发送p验证CPU已恢复。 | erase后os.replace抛OSError |
| W3-08 | 未保存丢失 | 1. 已保存W3OK后擦除但不保存；2. SIGKILL并等待QEMU退出；3. 新Session加载同镜像；4. 发送r并检查应答。 | 保存W3OK后erase不保存并SIGKILL；重开QEMU |
| W3-09 | 保存擦除 | 1. 发送e；2. 显式checkpoint；3. 关闭并新建Session；4. 发送r；5. 检查文件全1MiB为FF。 | erase、save、重启 |
| W3-10 | 错误镜像尺寸 | 1. 写入3字节bad文件；2. 使用该文件启动Session；3. 捕获ValueError；4. 重新读取文件确认未改动。 | 文件内容bad，仅3字节 |
| W3-11 | 模型诊断 | 1. 遍历各场景qemu.log；2. 排除宿主主动终止消息；3. 断言其余非空行列表为空。 | 检查全部qemu.log |

## 结果与证据表

| 固定编号 | 预期输出 | 实际输出 | 判定 | 日志路径（根目录见上） |
|---|---|---|---|---|
| W3-01 | 退出0且无BKPT | 构建及反汇编断言通过 | PASS | W3LOG/build.txt；W3LOG/summary.json |
| W3-02 | 参数/边界/NOR规则/原子性断言通过 | 退出0，PASS: flash API, partition boundaries, NOR rules, validation atomicity | PASS | W3LOG/native-flash.txt；W3LOG/summary.json |
| W3-03 | SELFTEST PASS | SELFTEST PASS | PASS | W3LOG/irq/uart.log；W3LOG/summary.json |
| W3-04 | 收到200；保留前127；丢73；无硬件overrun | count=200/drop=73/overrun=0；队列逐字节通过 | PASS | W3LOG/irq-overflow.txt；W3LOG/summary.json |
| W3-05 | 每次PONG；未知命令ERROR；drop=0x49 | 300次应答通过；ERROR command；DROP 0x00000049 | PASS | W3LOG/irq/uart.log；W3LOG/summary.json |
| W3-06 | PARAM从ffffffff到4b4f3357；仅0xf0000四字节W3OK；拒绝第二写者 | 完整1MiB逐字节匹配；锁异常符合 | PASS | W3LOG/write/uart.log；W3LOG/summary.json |
| W3-07 | 旧镜像不变；finally执行cont；后续p能应答 | 旧镜像相同；PONG；QMP请求可追溯 | PASS | W3LOG/write/qmp.json；W3LOG/summary.json |
| W3-08 | 恢复最近检查点W3OK | PARAM 0x4b4f3357 | PASS | W3LOG/restart/uart.log；W3LOG/summary.json |
| W3-09 | 整镜像FF；PARAM ffffffff | 1MiB逐字节全FF；PARAM 0xffffffff | PASS | W3LOG/erased-restart/uart.log；W3LOG/summary.json |
| W3-10 | 拒绝且原文件不变 | ValueError exactly；原字节bad | PASS | W3LOG/summary.json；W3LOG/summary.json |
| W3-11 | 除主动终止消息外无guest error | 无其他诊断 | PASS | W3LOG/summary.json；W3LOG/summary.json |

## 六 版本 复现与交付要求核对

在正式工程codes目录执行python3 scripts/verify-week3.py，脚本使用独立临时构建和测试镜像，不修改日常实验镜像。包内亦提供相同阶段脚本。工具版本见W3LOG下qemu-system-arm-version.txt、arm-none-eabi-gcc-version.txt、arm-none-eabi-gdb-version.txt、cmake-version.txt和cc-version.txt。

交付核对：测试方式、编号、步骤、实际输入、预期、实际和证据路径均已列明；源码与原始日志随包提供。当前材料满足既定仿真方案的可追溯测试记录要求；真实片内Flash、自动掉电保持、SIMATIC物理布局对齐仍需额外实现或老师认可范围，不能由本报告的PASS替代。

源文件摘要、被测ELF摘要、CHECK源码位置和范围限制见codes/docs/week3-storage-validation.json。历史证据不删除，但不再作为当前两份交付物的唯一依据。
