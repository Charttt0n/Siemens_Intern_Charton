# Week 7 系统测试与优化实习周报

## 本周完成情况

本周在保留 Week 6 已通过本机验收的目标与源文件的前提下，补全核心模块 Unity 测试，完成 QEMU 上的正常与异常集成、升级中断与故障恢复、问题修复，以及代码体积与启动时间优化。2026 年 11 月 13 日 20:45:01–20:48:20（Asia/Shanghai）在本机从源码重新构建并执行 `make verify-week7`，`summary.json` 中 29 个验收分组全部通过。Unity 7 个套件、93 个用例全部通过，0 失败、0 忽略。同期 Week 1–6 与反馈专项回归均通过。性能实测与 GDB 观测见后文。

本轮在 QEMU MPS2-AN500 上验收，使用 QEMU GDB remote stub 和模拟 NOR 快照。实体 OpenOCD/JTAG、真实掉电、看门狗电气验证及 IEC 62443 认证尚未开展。密钥仅用于实验签名。本地验收结果待带教确认。

| 计划交付 | 本周成果 | 证据 |
|---|---|---|
| Unity 核心单元测试 | 7 个套件共 93 个用例：宿主 82 个（含 ASan/UBSan），Cortex-M7 目标机 11 个 | 本报告 A 节 |
| 正常与异常集成测试 | 已签名启动/升级、最大签名包、非法镜像拒绝、配置与出厂重置、Flash 错误保持旧槽、7 个写入中断点冷启动、看门狗复位与未确认回退 | 本报告 B 至 E 节 |
| 问题修复与调试记录 | 试运行意图先于活动槽提交、应用确认门控、出厂重置确认、输入边界、配置写失败与损坏配置处理；GDB 顺序检查走 QEMU gdbstub | 修复记录 `codes/docs/week7-fix-record.md`，GDB 核对见 E 节 |
| 体积与启动时间优化 | 正式目标 `bootloader-week7.elf` 的 text+data 由 29132 降至 27204 字节。128 KiB 镜像验证加载路径中位仿真时间由 289.0 ms 降至 165.0 ms | 本报告「体积与启动时间」 |

用例数与验收分组分别统计。七个套件计数为 Flash 1、镜像 1、boot_meta 16、YMODEM 32、update 9、core 23、Cortex-M7 11。Flash 和镜像复合测试各按 1 例统计。

## 复现与材料

在 `codes/` 下运行。

```sh
source scripts/env.sh
bash scripts/build-week7.sh
make verify-week7
make benchmark-week7
make debug-week7
```

`verify-week7` 默认在新的 `logs/week7-时间戳/build` 下从源码构建。执行后可按验收摘要、串口日志和快照核对本报告的实测输出。

交付和证据入口见 `delivery-index.md`。Week 7 演示步骤见 `codes/docs/week7-guide.md`。

<!-- pagebreak -->

## 实现思路

Week 7 新增独立主程序、控制模块和应用确认逻辑，正式目标为 `bootloader-week7.elf`。继续使用既有 UART、Flash、YMODEM、Week 5 镜像头、升级元数据和 Week 6 的 32 字节头 + payload + 272 字节签名尾（SHA-256 + RSA-2048 PKCS#1 v1.5）。Week 6 目标保留作体积与启动对照，Week 6 源文件本周未替换。CMake 为业务代码增加可选优化级别：对照与调试目标使用 `-Og`，正式目标和第三方密码库使用 `-Os`。

故障注入只链接测试 ELF，正式 ELF 的符号检查见 A 节。

修复后先持久化试运行 ARMED 意图，再切换活动槽。配置提交失败时保留原活动槽。应用只确认当前活动槽且已处于 LAUNCHED 的试运行，避免旧应用确认尚未激活的候选槽。出厂重置只接受 `factory` 后紧接的 `YES`，其他命令撤销确认；重置仍只恢复元数据与启动参数，不擦除 A/B 固件槽。参数写入改为先提交候选副本，成功后才更新运行时值；配置页损坏时进入恢复入口，禁止带着默认试运行状态启动，直到显式出厂重置。

旧版本缺陷已在 `codes/logs/week7-baseline-defects-20261113` 实际复现，摘要状态为 `REPRODUCED`（出厂重置缺少有效确认、活动槽已切到 B 但无试运行意图、冷启动缺少试运行看门狗）。旧解析函数将 `5000000000` 解析为 `705032704`，串口参数的范围检查仍会拒绝该值。本周为解析函数补充了溢出检测。

## 测试输入与实测输出

以下记录来自 2026 年 11 月 13 日的 `make verify-week7`。本节证据路径均相对于 `codes/logs/week7-20261113-204501-591995/`，场景目录内的 `uart.log` 是原始串口输出，`after.bin` 是测试后的 NOR 快照。`summary.json` 保存 29 个验收分组及断言结果。日志节选用箭头标出出现顺序。

### A 构建与单元测试

| 检查 | 输入与操作 | 预期输出 | 实测输出与证据 |
|---|---|---|---|
| 全新构建 | 从源码在新的日志目录下构建正式、调试、故障和目标机 Unity ELF | 构建命令成功并生成四类 ELF | 构建退出码 0。两份日志均有 `Built target`，见 `build.txt`、`build-target-unity.txt` |
| Unity 汇总 | 宿主以 ASan/UBSan 执行 6 个套件，QEMU 执行 `unity-week7.elf` | 所有断言成立，失败和忽略均为 0 | 宿主 82 例 + Cortex-M7 11 例，共 93 例，0 失败、0 忽略。见 `unity/summary.json`、`unity-target.txt` |
| 正式 ELF 符号 | 对 `bootloader-week7.elf` 执行 `arm-none-eabi-nm -n` | `__wrap_flash_*` 和 `fault_latched` 的命中数均为 0 | 两类符号均为 0，见 `production-symbols.txt` |

Unity 实例可对照断言源码和逐例日志。CRC32 输入 `123456789`，预期 `0xCBF43926`，`test_crc_known` 通过。十进制解析输入 `4294967295` 和 `4294967296`，预期分别接受并返回上限值、拒绝溢出，`test_decimal_bounds` 通过。这两项见 `unity/core.txt`。目标机 UART 输入 25 MHz 和 115200 baud，预期初始化返回 0、分频寄存器为 217，`unity-target.txt` 中的 `test_uart_init_divisor` 通过。

<!-- pagebreak -->

### B 已签名启动与升级

| 检查 | 输入与操作 | 预期输出 | 实测输出与证据 |
|---|---|---|---|
| 签名启动 | A 槽装入签名 v1 包，启动正式 ELF | A 槽验签成功，v1 自检并确认 | `IMAGE: SLOT A SIG PASS` → `APP: VERSION v1` → `APP: BOOT CONFIRMED`。另有 CRT、HANDOFF、SVC 成功标记。见 `signed-boot/uart.log` |
| 签名升级与确认 | A 槽运行 v1，发送 `u`，经 YMODEM 传签名 v2 包（3664 字节） | 事务提交后切到 B，v2 试运行并确认 | `UPGRADE: COMMITTED` → `TRIAL: LAUNCH` → `APP: VERSION v2` → `APP: BOOT CONFIRMED`。快照活动槽为 B。见 `upgrade-v2/` |
| 最大签名包 | 同一 v1 初始状态上传 `maximum-signed.pkg`（458752 字节 = 32 字节头 + 458448 字节 payload + 272 字节签名尾） | 完整接收并提交，B 槽运行 v2 | 上传完成，`UPGRADE: COMMITTED` → `TRIAL: LAUNCH` → `APP: VERSION v2` → `APP: BOOT CONFIRMED`。快照活动槽为 B。见 `maximum-signed-upgrade/` |

### C 非法包拒绝与无效镜像回退

前四项均从 B 槽已运行签名 v2 的快照开始。每项实测均无 `UPGRADE: COMMITTED`，快照显示活动槽为 B，B 槽内容逐字节未变。

| 检查 | 输入与操作 | 预期输出 | 实测输出与证据 |
|---|---|---|---|
| 无签名包 | 上传 `unsigned.pkg`（v3，3392 字节，无签名尾） | 拒绝候选包，继续运行 v2 | `UPGRADE: FAIL 0x00000006` → `APP: VERSION v2`。见 `unsigned/` |
| 签名错误 | 上传 `bad-signature.pkg`（v3 签名尾末字节有损坏） | 验签失败，继续运行 v2 | `UPGRADE: FAIL 0x00000006` → `APP: VERSION v2`。见 `bad-signature/` |
| Payload 损坏 | 上传 `corrupt-payload.pkg`（v3 payload 有一字节损坏） | 完整性校验失败，继续运行 v2 | `UPGRADE: FAIL 0x00000006` → `APP: VERSION v2`。见 `corrupt-payload/` |
| 签名头篡改 | 上传 `signed-header-tamper.pkg`（v3 包头版本改为 99，头 CRC 与新包头一致） | 头 CRC 正确时仍拒绝签名不匹配的包，继续运行 v2 | `UPGRADE: FAIL 0x00000006` → `APP: VERSION v2`。见 `signed-header-tamper/` |
| 活动槽损坏 | 活动 A 的 v1 镜像损坏，B 保有有效签名 v2，启动正式 ELF | 选择 B 并正常运行 v2 | `ROLLBACK: SELECT B` → `APP: VERSION v2` → `APP: BOOT CONFIRMED`。见 `corrupt-active-fallback/uart.log` |
| 两槽均无效 | A 的 v1 与 B 的 v2 镜像均损坏，发送 `boot` | 保持命令接口，无应用启动 | `ROLLBACK: NO VALID BACKUP`，无 `APP: VERSION`。见 `both-invalid/uart.log` |

<!-- pagebreak -->

### D 配置接口与存储故障

以下前四行归入同一 CLI 验收分组。Flash 操作错误用故障测试 ELF 验证。表中的故障编号对应脚本配置，串口和快照提供实际结果。

| 检查 | 输入与操作 | 预期输出 | 实测输出与证据 |
|---|---|---|---|
| 命令与溢出 | 发送 `version`，再发 `p window_ms 18446744073709555616` | 返回版本，拒绝溢出值 | `BOOT7: BL week7-1`、`CONFIG: BAD window_ms`。见 `cli/uart.log` |
| 过长行与无效确认 | 发送 `p window_ms 4000` 后加 50 个空格。再分别发送裸 `YES` 及 `factory → help → YES` | 拒绝超长输入，拒绝两次无效确认 | `CONFIG: LINE TOO LONG`，两次 `BOOT7: unknown command`，无 `FACTORY: DONE`。见 `cli/uart.log` |
| 参数持久化 | 执行 `p auto_boot 0` 和 `p window_ms 4000`，用保存的快照重启并执行 `param` | 重启后读取 4000 ms、自动启动关 | `PARAM window_ms=04000`、`PARAM auto_boot=0`。见 `cli/uart.log`、`params-restart/uart.log` |
| 出厂重置 | 在上述重启会话发送 `factory` 后紧接 `YES`，用重置后的快照再启动 | 执行重置，保留两固件槽，v1 可启动 | `FACTORY: DONE`，A/B 固件区域与重置前初始镜像一致。再次启动输出 `APP: VERSION v1`。见 `params-restart/`、`factory-boot/` |
| 配置页损坏 | 两配置页均置零，发送 `boot` | 要求重置配置，禁止启动应用 | `RECOVERY: CFG RESET REQUIRED`，无 `APP: VERSION`。见 `cfg-corrupt/uart.log` |
| B 槽擦除错误 | A 槽 v1，上传签名 v2，注入故障 10 | 升级失败，保留 A 和 v1 | `UPGRADE: FAIL 0x00000006` → `APP: VERSION v1`。快照活动槽 A 且 A 槽逐字节不变。见 `erase-error/` |
| B 槽编程错误 | A 槽 v1，上传签名 v2，注入故障 11 | 升级失败，保留 A 和 v1 | `UPGRADE: FAIL 0x00000006` → `APP: VERSION v1`。快照活动槽 A 且 A 槽逐字节不变。见 `program-error/` |
| B 槽读回错误 | A 槽 v1，上传签名 v2，注入故障 12 | 升级失败，保留 A 和 v1 | `UPGRADE: FAIL 0x00000006` → `APP: VERSION v1`。快照活动槽 A 且 A 槽逐字节不变。见 `readback-error/` |
| 试运行意图写失败 | A 槽 v1，上传签名 v2，注入故障 14 | 事务提交失败，保留 A 和 v1 | `UPGRADE: COMMIT ERROR 0x00000008` → `APP: VERSION v1`。快照活动槽 A 且 A 槽逐字节不变。见 `trial-arm-error/` |
| 参数写失败 | 注入故障 15，发送 `p window_ms 4000` 后查询 `p` | 拒绝保存，运行时保留 2500 ms | `CONFIG: SAVE FAIL` → `PARAM window_ms=02500`。见 `param-write-error/uart.log` |
| 活动槽读错误 | A 槽 v1、B 槽 v2，注入 A 槽读取故障 13 | 读取 A 失败后选择 B，运行 v2 | `ROLLBACK: SELECT B` → `APP: VERSION v2`。见 `active-read-error/uart.log` |

<!-- pagebreak -->

### E 升级中断冷启动与看门狗

七个中断场景均从 A 槽 v1 开始，用故障 ELF 上传签名 v2。命中指定注入点后保存 `<场景>/nor-at-fault.bin`，再用正式 ELF 冷启动。下表中的活动槽取自中断快照（A=1、B=2）。七份快照的 A 槽与初始镜像逐字节一致。冷启动串口日志统一在 `restart-<场景>/uart.log`。

| 检查 | 输入与操作 | 预期输出 | 实测输出与证据 |
|---|---|---|---|
| 目标槽擦除中断 | 故障 1，B 槽仅擦除前 73 字节 | 活动槽 A，冷启动 v1 | 仅前 73 字节为 `0xFF`，活动槽 1，`APP: VERSION v1`。场景 `target-erase-tear` |
| 目标槽写入中断 | 故障 2，目标数据字只写前 2 字节 | 活动槽 A，冷启动 v1 | B 槽前 66 字节与上传包一致，活动槽 1，`APP: VERSION v1`。场景 `target-write-tear` |
| 试运行记录正文中断 | 故障 3，试运行记录正文只写前 30 字节 | 活动槽 A，冷启动 v1 | 活动槽 1，`APP: VERSION v1`。场景 `trial-body-tear` |
| 试运行标记写完 | 故障 4，试运行标记完整写入后中断 | 活动槽 A，冷启动 v1 | 活动槽 1，`APP: VERSION v1`。场景 `trial-marker-complete` |
| 活动记录正文中断 | 故障 5，活动记录正文只写前 42 字节 | 活动槽 A，冷启动 v1 | 活动槽 1，`APP: VERSION v1`。场景 `activation-body-tear` |
| 活动标记写入中断 | 故障 6，活动标记只写前 2 字节 | 活动槽 A，冷启动 v1 | 活动槽 1，`APP: VERSION v1`。场景 `activation-marker-tear` |
| 活动标记写完 | 故障 7，活动标记完整写入后中断 | 活动槽 B，冷启动试运行 v2 并确认 | 活动槽 2，`TRIAL: LAUNCH` → `APP: VERSION v2` → `APP: BOOT CONFIRMED`。场景 `activation-marker-complete` |
| 看门狗复位 | 上传签名 `application-week7-hang.pkg`，应用运行但不确认，保存快照后继续运行虚拟机 | 观察到客体复位事件 | UART 有 `TRIAL: LAUNCH`、`APP: VERSION vhang`、`APP: RUNNING`，无启动确认。QMP 记录 1 条 `RESET`，`guest=true`、`reason=guest-reset`。见 `watchdog-trial/` |
| 未确认试运行回退 | 用复位前的 `trial-launched.bin` 冷启动正式 ELF | 识别未确认试运行，回到 A 的 v1 并确认 | `ROLLBACK: TRIAL UNCONFIRMED` → `ROLLBACK: SELECT A` → `APP: VERSION v1` → `APP: BOOT CONFIRMED`。见 `watchdog-snapshot-restart/uart.log` |

GDB 另以签名 v2 升级作为输入，在 `update_activate` 入口和返回点核对提交顺序。预期先保存试运行意图，再切活动槽。实测入口为 `W7_CFG_PENDING=0,1`、`W7_CFG_STAGE=0,1`、`W7_OLD_ACTIVE=1`，返回为 `W7_ACTIVATE_RESULT=0`、`W7_NEW_ACTIVE=2`，应用串口随后输出 `APP: BOOT CONFIRMED`。复核见 `codes/logs/week7-debug-20261113-final/` 下的 `gdb.txt` 和 `session/uart.log`。

<!-- pagebreak -->

### F 前期回归

各入口的预期为相关回归断言全部通过。总数和分项证据索引见 `codes/logs/week7-regression-20261113/summary.json`，同目录保留各阶段摘要。每行列出一项实测供抽查。

| 范围 | 输入与抽查场景 | 实测输出 |
|---|---|---|
| Week 1 | `make verify-week1`，在 Cortex-M7 仿真运行默认演示 ELF | QEMU 退出码 0，输出 `.data initialized`、`.bss zero`。6/6 组通过 |
| Week 2 | `make verify-week2`，在 AN500 查询 CPU 模型时钟 | Cortex-M7 的 `cpuclk` 为 25 MHz。12/12 组通过 |
| Week 3 | `make verify-week3`，对 Flash API 输入分区边界和 NOR 操作 | 边界、NOR 规则和写入预检原子性断言通过。11/11 组通过 |
| Week 4 | `make verify-week4`，将 CRC32 的 8 个固定长度输入与 zlib 对照 | 8 组值相符。27/27 组通过 |
| Week 5 | `make verify-week5`，向活动槽 A 为 v1 的系统上传 CRC 错误包 | `UPGRADE: FAIL 0x00000006`，继续运行 v1。75/75 组通过 |
| Week 6 | `make verify-week6`，向活动槽 B 为 v2 的系统上传无签名 v3 | `UPGRADE: FAIL 0x00000006`，继续运行 v2。11/11 组通过 |
| 反馈专项 | `make verify-feedback`，将 UART TX 设为满、DATA 设为 `0x55`，随后解除满状态 | 满时返回 `-1` 且 DATA 保持 `0x55`，解除后写入 `0x41`。30/30 组通过，见 `feedback-summary.json` 的 `FB-TX-02` |

## 体积与启动时间

性能证据见 `codes/logs/week7-benchmark-20261113-204522/summary.json`。测量模型为 QEMU icount shift=3（每条仿真指令 8 ns），TIMER0 按 25 MHz 计时。命令窗口固定 200 ms，只是双方相同的实验参数，产品默认窗口仍为 2500 ms。缩短命令窗口未计入优化收益。时间是仿真指标，不是真机周期或板上启动时间。

| 构建 | text | data | text+data |
|---|---|---|---|
| 基线 `bootloader-week6.elf`（业务代码 `-Og`） | 29124 | 8 | 29132 |
| 修复后调试 `bootloader-week7-debug.elf`（业务代码 `-Og`） | 29712 | 8 | 29720 |
| 正式优化 `bootloader-week7.elf`（业务代码 `-Os`） | 27196 | 8 | 27204 |

正式目标较基线减少 1928 字节（6.62%），仍低于 64 KiB 代码区上限。三个目标的 `bss_including_stack` 均为 41176 字节。对 128 KiB 已签名镜像，验证加载至交接的中位仿真时间由 289.0 ms 降至 165.0 ms（减少 124.0 ms）。演示镜像（payload 3360 字节）对应中位时间由 36.0 ms 降至 31.0 ms。每组重复 3 次，中位数见优化报告。

后续按带教反馈修订，并推进实体链路验证、Doxygen、最终演示与 Week 8 结项。
