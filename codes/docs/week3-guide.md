# 第三周：UART 中断与模拟 Flash 存储

周次说明：本文对应原计划Week3驱动与存储管理；Week4固件校验与应用跳转另见week4-guide.md。更新日期：2026-10-25；历史实现日期保留。独立内存分区交付说明见[week3-memory-layout.md](week3-memory-layout.md)，最新操作证据见[week3-test-report.md](week3-test-report.md)。
实现日期：2026-10-19。任务依据为 `planning/项目理解＆W1理解&八周规划_工时补充版.docx` 中的第三周：UART 收发与中断、Flash 抽象层、Bootloader／主应用／备份／参数分区，以及读写、擦除和持久化验证。

## 1. 运行

```sh
cd '/Users/charton/projects/Siemens_Intern/codes'
bash scripts/build-week3.sh
python3 scripts/run-week3.py
```

第三周构建使用 `build/week3/`，避免项目迁移前遗留在 `build/` 的 CMake 缓存。启动目标为 `bootloader-week3.elf`，不启用 semihosting；第一、二周目标保留。

启动后，每次输入一个命令再按回车：

| 命令 | 行为 |
|---|---|
| `p` | UART 往返检测，回复 `PONG` |
| `r` | 读取参数分区前 4 字节，以小端十六进制显示 |
| `e` | 擦除参数分区第一个 4 KiB 块，其他块不变 |
| `w` | 将 `W3OK` 四字节标记写到参数分区起点 |
| `s` | 显示已接收字节数、软件队列丢弃数、检测到的硬件溢出次数 |
| `h` | 显示固件命令说明 |
| `save` | 宿主机保存检查点，随后继续运行 |
| `quit` / 输入结束 EOF | 保存检查点后退出 |
| Ctrl-C | 退出，不保存本次尚未保存的修改 |

初次运行依次输入 `r`、`w`、`r`、`quit`，预期读值从 `0xffffffff` 变为 `0x4b4f3357`。重新运行后输入 `r`，仍应为 `0x4b4f3357`。输入 `e`、`quit` 后再次运行，读值恢复为 `0xffffffff`。

默认镜像为 `build/week3-flash.bin`。可通过 `--image /绝对路径/实验镜像.bin` 指定其他文件。该文件是运行数据，位于被 Git 忽略的构建目录中；重要实验数据应另行备份。已有镜像必须恰好为 1 MiB，大小不正确会被拒绝；并发使用同一镜像会被文件锁拒绝。

## 2. 代码结构

| 路径 | 责任 |
|---|---|
| `src/week3/startup.S` | 沿用第二周启动逻辑，将 IRQ 0 接到 UART0 RX 处理函数 |
| `src/week3/linker.ld` | 限制代码为 64 KiB、运行 RAM 为 3 MiB，给模拟存储留出 1 MiB |
| `src/week3/uart.c/.h` | TX 轮询、RX 中断、接收缓冲区与计数器 |
| `src/week3/flash.c/.h` | 分区接口、边界／对齐／写保护检查、NOR 读写擦除规则 |
| `src/week3/main.c` | 最小诊断命令；只操作参数区的第一个擦除块 |
| `src/week2/board.c` | 复用已有 AN500 时钟与 VTOR 初始化 |
| `scripts/week3_session.py` | QMP 控制、UART 连接、镜像加载与检查点保存 |
| `scripts/run-week3.py` | 交互式实验入口 |
| `tests/test_flash.c` | 同一组 Flash 断言分别运行于宿主机与 Cortex-M7 |
| `scripts/verify-week3.py` | 全新构建、UART 注入、镜像重启与失败场景验收 |

## 3. UART 中断

AN500 UART0 为 CMSDK APB UART，基址 `0x40004000`；RX 为外部 IRQ 0，向量表索引 16。初始化设置分频值 217、CTRL 为 `0x0b`（TX、RX、RX 中断），并打开 NVIC IRQ 0。发送保留有查询次数上限的轮询方式，未启用 TX 中断。

接收中断先清 RX 中断标志，再读取 DATA，避免读取 DATA 后立即接收新字节时误清新的中断。字节进入 128 字节环形数组，保留一个空槽区分空／满，因此实际容量为 127 字节。队列满时保留旧数据、丢弃新字节，并递增 `uart_rx_dropped`；消费端用短暂的 PRIMASK 临界区取出字节，并恢复原先的中断屏蔽状态。

主循环轮询软件队列；DATA 寄存器只由接收中断读取。当前没有低功耗等待，避免直接使用“检查队列后 WFI”产生丢失唤醒窗口。软件丢字节计数不能代替上层协议的错误恢复，后续 Ymodem 需要把溢出纳入会话失败处理。

## 4. 两套地址：运行布局与存储布局

运行布局：

| 区域 | 半开地址范围 | 用途 |
|---|---|---|
| 启动代码 | `[0x00000000, 0x00010000)` | QEMU 加载 ELF，含向量表与代码 |
| C 运行 RAM | `[0x20000000, 0x20300000)` | 静态数据与栈 |
| 保留栈 | `[0x202ff000, 0x20300000)` | 4 KiB，初始 MSP 为 `0x20300000` |
| 模拟存储 | `[0x20300000, 0x20400000)` | 1 MiB，宿主机加载镜像 |

模拟存储位于 AN500 的 SSRAM 2&3 内，完全位于第三周 ELF 的所有段之外。启动代码不会清零它；`flash_sim_bind()` 只绑定内存，不初始化或擦除。首次运行由宿主机提供全 `0xff` 镜像。

存储 API 中的偏移均相对于所选分区，不是 CPU 地址：

| 分区 | 镜像偏移范围 | CPU 映射起点 | 大小 | API 写入权限 |
|---|---|---|---|---|
| `FLASH_BOOT` | `[0x00000, 0x10000)` | `0x20300000` | 64 KiB | 只读 |
| `FLASH_APP` | `[0x10000, 0x80000)` | `0x20310000` | 448 KiB | 读／写／擦除 |
| `FLASH_BACKUP` | `[0x80000, 0xf0000)` | `0x20380000` | 448 KiB | 读／写／擦除 |
| `FLASH_PARAMS` | `[0xf0000, 0x100000)` | `0x203f0000` | 64 KiB | 读／写／擦除 |

这里的 Bootloader 分区是预留的逻辑区域，不含当前运行 ELF 的副本；当前代码从地址 0 启动。APP/BACKUP是存储槽；Week3不将槽地址作为应用执行地址或实施跳转。现有Week4运行地址约定见week4-guide.md，不计入Week3成果。

### 4.1 SIMATIC 存储职责对应依据（2026-10-23补充）

[SIMATIC 存储职责与本项目内存布局对应依据](simatic-memory-mapping.md)列出了研究样本 CPU 1511-1 PN（6ES7511-1AK02-0AB0）、官方资料、四分区及运行区对应表、容量设计理由和测试证据。对应限于装载、运行和保持性数据的职责；地址、容量、主备槽及显式检查点为本项目设计，不代表 SIMATIC 内部固件布局。严格物理布局对齐仍缺指定硬件和内部资料。

## 5. Flash 接口契约

- `flash_read`：任意字节偏移／长度；长度为 0 时允许分区末尾和空输出指针。
- `flash_program`：偏移与长度必须按 4 字节对齐，长度非零。只允许位从 1 变为 0；整个请求先校验，再修改内存。
- `flash_erase`：偏移与长度必须按 4 KiB 对齐，长度非零；擦除后为 `0xff`。
- 禁止跨分区、无效分区、空缓冲区、范围整数溢出，以及输入／输出缓冲区与模拟存储重叠。Bootloader 区拒绝编程与擦除。
- 错误码：`0` 成功；`-1` 参数；`-2` 越界；`-3` 对齐；`-4` 写保护；`-5` 需要先擦除。诊断命令将负数按 32 位十六进制打印。

目前使用 SRAM 后端，API 只支持前台单调用者，不支持中断和主循环同时写入。写保护是软件接口约束，不是 MPU 或硬件保护。拒绝非法请求时不产生部分写入，但运行中掉电／复位时的操作原子性没有保证。

## 6. 持久化的准确含义

```text
1 MiB 磁盘镜像 → QEMU loader → SRAM 中的 NOR 模型
                                    ↓ 完成 UART 命令并收到提示符
磁盘镜像 ← 临时文件 + fsync + 替换 ← QMP stop / pmemsave / cont
```

保存只允许在上一条 UART 命令已完成后进行；停止 CPU 后导出完整存储区，写入同目录临时文件，刷新内容，再替换镜像并刷新目录。写入或替换之前失败不会损坏已有检查点。已有镜像通过文件锁排除本工具的并发写入。

这验证的是**显式检查点持久化**。它不模拟真实 Flash 控制器、擦写耗时、寿命、掉电撕裂写入或自动落盘。SIGKILL 后恢复到最后一次保存是预期行为；保存之后的修改会丢失。没有做宿主机突然断电的实机实验，也没有声明抗掉电能力。Week3存储层不检测同大小镜像的内容损坏；CRC镜像校验归Week4，签名校验归后续安全阶段。

## 7. 验收与下一步

```sh
python3 scripts/verify-week3.py
python3 scripts/verify-week2.py
```

验收使用临时构建目录和独立镜像，不使用日常实验镜像。`week3-test.elf` 会擦除测试用存储并运行断言，只供验收；日常运行应使用 `bootloader-week3.elf`。证据位置见 `logs/week3-latest.txt`，结论见对应 `summary.json`，详细覆盖见 [第三周测试报告](week3-test-report.md)。

下一阶段按第四周规划实现 CRC32、镜像格式与运行地址约定、应用加载、向量表切换和应用跳转。升级协议、安全签名及恢复策略尚未实现。

## 8. 官方实现参考

查阅日期：2026-10-19。链接为上游 master，可变化；本机版本和寄存器行为以验收日志为准。

- [QEMU MPS2：AN500 内存映射和 UART IRQ 绑定](https://github.com/qemu/qemu/blob/master/hw/arm/mps2.c)
- [QEMU CMSDK APB UART：RX 标志、DATA 读取和中断语义](https://github.com/qemu/qemu/blob/master/hw/char/cmsdk-apb-uart.c)

## 一键入口与失败处理复验

在 codes/ 执行 `python3 scripts/verify-cli-week34.py`，直接测试本指南构建和运行入口。干净构建、正常运行、产物缺失、CMake/QEMU失败、启动和UART超时、进程及文件锁清理的22项记录见 [入口专项报告](cli-week34-test-report.md)。故障注入仅在临时副本进行。

## UART 波特率依据与读回

25000000 / 115200 = 217余1600，按最近整数取217；推导波特率115207.3733 baud，误差+0.0064004%。UART0的BAUDDIV地址0x40004010读回0xD9，CTRL地址0x40004008读回0x0B。完整公式、操作步骤、专项日志及仿真边界见 [UART波特率验证报告](uart-baud-validation.md)。复验：在codes/执行 `python3 scripts/verify-uart-baud.py`。字符后端收发成功不代表已测得物理串口位宽。

## PRIMASK接口契约与复验

irq_save/irq_restore位于src/week2/irq.h；每层保存原值、按后进先出恢复，所有出口必须配对。原值为1时不能无条件打开中断。两个初始值及嵌套、UART调用的22个读回断言已通过，见 [PRIMASK专项报告](irq-contract-validation.md)。复验：在codes/执行 `python3 scripts/verify-irq-contract.py`。

## TX持续阻塞与轮询上限

持续TX_FULL注入已验证：每次uart_flush最多读取状态1000000次后返回-1；putc不写DATA，write向上返回失败。Week3设置boot_status=3；Week4设置0x101，应用设置app_status=4。此预算不是准确毫秒超时。操作、预期/实际、上层取证及仿真边界见 [TX专项报告](tx-full-validation.md)；在codes/执行 `python3 scripts/verify-tx-full.py`复验。
