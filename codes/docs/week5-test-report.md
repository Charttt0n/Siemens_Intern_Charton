# Week 5 固件升级测试报告

2026 年 11 月 6 日复验了 Week 5 单文件 UART 升级、逻辑 A/B 槽和参数事务。测试覆盖原规划第 5 页的接收、写入、校验、切换和新固件启动，以及失败与受控中断后的旧槽保护。

## 结果与环境

verify-week5.py 的 75 个顶层检查分组全部通过。其中包括 9 项独立发送端互操作、24 条启动记录、12 条负向记录和 9 例中断快照。这些明细已计入顶层分组。前四周主回归、反馈回归及共享向量边界专项同步通过。11 月 1 日的五周核查报告保留当时的结果。

平台为 QEMU MPS2-AN500 Cortex-M7，C99/Thumb，软浮点；运行时不依赖 semihosting 输出升级日志。测试重新在临时目录构建正式、READY 边界和故障注入目标，宿主 C 测试使用 ASan/UBSan。第三方发送端固定为仓库内 ymodem 1.5.3，来源和许可证见 tests/third_party/README.md。

本轮使用 QEMU 11.1.2、GNU Arm GCC 16.2.0、CMake 4.4.4 和 GNU GDB 16.3.90.20250906-git。证据目录同时保存宿主编译器版本及交叉构建输出。

| 项目 | 本轮值 | 含义 |
|---|---|---|
| 执行地址 | 0x00010000 | A/B 的 payload 均链接到同一位置 |
| 镜像格式 | 32 字节头加 payload | 最大总文件 458752 字节 |
| 测试固件 | v1/v2/v3 各 1004 字节 payload | 每包 1036 字节；v1/v2 内容差异经过断言 |
| 正式 Bootloader | text 9452，data 0，bss 4284 字节 | 交付构建 arm-none-eabi-size；非优化前后对比 |
| 存储模型 | 1 MiB SRAM NOR | 4 KiB 擦除、4 字节编程；显式保存 |

本轮证据根目录（下文记为 E）：

`codes/logs/week5-20261106-043520-600037`

E/summary.json 保存 ELF SHA256、包长度和逐项结果。原始 UART/QMP、编译器输出和 NOR 快照提供复核证据。交付包清单登记本次交付使用的源码和固件版本。

<!-- pagebreak -->

## 宿主检查与协议互操作

| 检查 | 输入和断言 | 结果与 E 下证据 |
|---|---|---|
| 包格式 | 合法头、CRC、向量、容量、seed 边界；拒绝 ELF/全 NOR | PASS；package-unit-tests.txt、packer-rejects-elf.txt、packer-rejects-flash-dump.txt |
| 槽接口 | A/B、非法槽、坏 CRC/长度/向量、旧接口兼容 | PASS；native-slot-tests.txt |
| 参数事务 | 损坏、IO、正文/标记撕裂前缀、序号回绕和歧义 | PASS；native-meta-tests.txt |
| 协议状态机 | 首包、大小、帧 CRC、重复、取消、超时、sink 失败 | PASS；native-ymodem-tests.txt |
| 写入链路 | RECEIVING、擦除、分片写入、READY、失败、激活 | PASS；native-update-tests.txt |
| 重复包有界性 | 重复数据只能写一次，超过预算返回 RETRY_LIMIT | PASS；duplicate-bound-test.txt |
| CRC16 独立对照 | 10 个输入与第三方实现比较 | PASS；summary.json 的 crc16_cases |

下列互操作使用真正的第三方 Python 发送端和本工程 C 接收器。正常、丢包及 CRC 故障均核对接收内容；停滞用例预期超时退出而非成功传输。每行在 E/summary.json 的 interop 数组以及同名 .stderr 中有记录。

| 场景 | 预期 | 实际 |
|---|---|---|
| interop-1024 | 传输与接收内容一致 | PASS |
| interop-128 | 传输与接收内容一致 | PASS |
| interop-block0-ack-loss | 传输与接收内容一致 | PASS |
| interop-data-ack-loss | 传输与接收内容一致 | PASS |
| interop-eot-ack-loss | 传输与接收内容一致 | PASS |
| interop-bad-crc | 传输与接收内容一致 | PASS |
| interop-wrap-128 | 传输与接收内容一致 | PASS |
| interop-delayed-terminator | 传输与接收内容一致 | PASS |
| interop-stalled-link | 预期失败且有界退出 | PASS |

序号回绕使用 34000 字节、128 字节分帧；延迟终包重放用于检验最终 ACK 丢失窗口，finish 不重复执行。有界结束取决于可推进的单调时钟和符合 read/write 契约的底层适配器。

<!-- pagebreak -->

## QEMU 正常流程与选槽

步骤：seed 建立初始 v1；启动正式固件并经 UART 发送 v2；等待 COMMITTED 及版本输出；保存当前 NOR，关闭进程，再用保存文件启动新 QEMU；随后以相同方式上传 v3。验证活动槽、IDLE 参数、目标内容、未使用空间为 0xFF，以及旧槽全长逐字节不变。

结果：v2 对应 B，v3 对应 A，均运行并通过 HANDOFF/SVC 自检。上传记录为 E/upload-v2 和 E/upload-v3；重启记录为 E/reboot-v2 和 E/reboot-v3。每个目录含 uart.log 及 QMP/QEMU 日志，具体文件以目录为准。

以下为 summary.json 中非故障重启的启动记录。每条以同名目录中的串口记录为依据；RECOVERY 只代表保留恢复入口，不代表已自动回退。

| 场景 | 预期与实际 | 状态 |
|---|---|---|
| blank-meta-default-a | v1 / A | PASS |
| metadata-selects-b | v2 / B | PASS |
| uncommitted-state-1 | v1 / A | PASS |
| uncommitted-state-2 | v1 / A | PASS |
| uncommitted-state-3 | v1 / A | PASS |
| uncommitted-state-4 | v1 / A | PASS |
| bad-active-does-not-auto-fallback | RECOVERY，不执行应用 | PASS |
| no-active-image | RECOVERY，不执行应用 | PASS |
| double-metadata-damage | RECOVERY，不执行应用 | PASS |
| one-valid-metadata-copy | v2 / B | PASS |
| ambiguous-metadata | RECOVERY，不执行应用 | PASS |
| reboot-v2 | v2 / B | PASS |
| reboot-v3 | v3 / A | PASS |
| reboot-maximum-slot-upload | 458752 字节满槽升级后运行 v2 / B | PASS |

额外执行 E/explicit-recovery：两份参数损坏后通过 r 重建，重新上传并运行 v2。普通启动不会自行猜测活动槽。E/uart-retransmit 在真实 UART 上逐字节发送首包、注入坏 CRC16 并重复第一帧，最终内容与升级包一致，未重复写入。

E/maximum-slot-upload 将 v2 填充至满槽并从 UART 上传。B 槽的 458752 字节与升级包一致，A 槽全长保持不变。记录为 B 活动、IDLE，冷重启运行 v2。

<!-- pagebreak -->

## 失败处理与旧槽保护

下表用例均检查没有成功提交提示，或按预期返回失败，且持久 active_slot 仍为 A。镜像拒绝用例额外比较整个旧槽内容保持不变。Flash IO 与丢包标记通过测试专用故障固件注入，不混称为真实硬件错误。

| 场景 | 预期与实际 | 状态 |
|---|---|---|
| bad-crc32 | 拒绝或终止；旧活动选择保持 | PASS |
| bad-vector | 拒绝或终止；旧活动选择保持 | PASS |
| bad-vector-tail-halfword | 拒绝首半字不完整的 Reset 入口，A 槽 v1 继续运行 | PASS |
| truncated-file | 拒绝或终止；旧活动选择保持 | PASS |
| extra-file-tail | 拒绝或终止；旧活动选择保持 | PASS |
| file-over-capacity | 拒绝或终止；旧活动选择保持 | PASS |
| cancel | 拒绝或终止；旧活动选择保持 | PASS |
| timeout | 拒绝或终止；旧活动选择保持 | PASS |
| erase-io | 拒绝或终止；旧活动选择保持 | PASS |
| program-io | 拒绝或终止；旧活动选择保持 | PASS |
| activation-meta-io | 拒绝或终止；旧活动选择保持 | PASS |
| rx-loss-indication | 拒绝或终止；旧活动选择保持 | PASS |

对应 E 下同名目录。CRC32 损坏通过改 payload 字节注入；bad-vector 将 Reset 改为偶数并重算镜像 CRC，以单独触达向量拒绝；truncated-file 缺 7 字节；extra-file-tail 附加尾数据；file-over-capacity 超过槽容量 1 字节。

末字节用例将 Reset 指向奇数长度 payload 的最后一个字节，重新计算两处 CRC32。旧固件在复现时输出 COMMITTED。修复后，串口记录显示升级失败且启动 v1，参数保留 A 活动，写入 FAILED 和镜像错误码 4。E/metadata-inspect-failed.txt 保存参数查看命令的输出，E/metadata-inspect-ambiguous.txt 保存歧义记录的拒绝结果。

断连另列在 E/uart-disconnect：关闭实际 UART socket 后，固件依靠超时到达 FAILED；宿主暂停 CPU 导出当前 NOR，再在 E/restart-after-disconnect 冷重启，运行 v1。断连没有 UART EOF 信号，不能以宿主连接关闭代替固件超时判定。

E/real-rx-overflow 在测试专用存储延迟窗口实际发送 8192 个字节，填满中断驱动的 RX 环形队列。结果为 UPGRADE: FAIL 0x00000001，旧应用继续运行。它与 rx-loss-indication 直接设置丢包标记是两种独立机制。

接收超时配置为每字节 1000 ms、最多 8 次连续无进展重试、实际进展间隔预算 15000 ms。UART 协议输出每字节等待上限 250 ms。墙钟测量只用于限定当前测试退出，不作为物理串口吞吐率或实时性保证。

<!-- pagebreak -->

## 受控中断与冷重启

故障包装器仅链接 bootloader-week5-fault.elf。每例等待 fault_latched 达到对应编号，暂停 CPU，导出此时完整 NOR；然后使用正式 bootloader-week5.elf 加载该快照重启。旧 A 槽与初始完整内容比较相同。测试编号位于参数最后一页 0xFF000，正式固件不消费该编号。

| 中断位置 | 重启应用 | 快照 state / active | 结果 |
|---|---|---|---|
| target-erase-prefix73 | v1 | 1 / 1 | PASS |
| target-word-prefix2 | v1 | 1 / 1 | PASS |
| activation-erase-prefix24 | v1 | 3 / 1 | PASS |
| activation-body-prefix21 | v1 | 3 / 1 | PASS |
| activation-crc-prefix2 | v1 | 3 / 1 | PASS |
| activation-marker-prefix2 | v1 | 3 / 1 | PASS |
| activation-marker-complete | v2 | 0 / 2 | PASS |
| ready-not-activated | v1 | 3 / 1 | PASS |
| verifying-body-prefix21 | v1 | 1 / 1 | PASS |

state：0 IDLE，1 RECEIVING，2 VERIFYING，3 READY，4 FAILED；active：1 为 A，2 为 B。prefix 后的数字是本次人为截断字节数，不能解释成硬件原子写入粒度。

target-erase-prefix73 验证目标仅前 73 字节变为 0xFF；target-word-prefix2 验证目标前 66 字节等于包、其后未写。正文、CRC、标记不完整时新记录不可接受，重启选旧槽；完整标记提交后即使尚未完成调用者的最后读回，重启选新槽。READY 未激活时仍选旧槽。

各例快照为 E/场景名/nor-at-fault.bin，重启日志为 E/restart-场景名/uart.log。SHA256 和期望版本记录在 summary.json/fault_cases。E/retry-after-tear 从数据写入中断的快照重新完整上传，COMMITTED 并运行 v2；不支持中途续传。

这组实验验证的是 NOR 操作前缀中断模型和软件事务语义。没有模拟真实 Flash 的电压、擦写时序、磨损和缓存一致性，也没有把加载旧 seed 当作中断快照。

<!-- pagebreak -->

## 复现命令与交付检查

在 codes 目录执行：

```sh
source scripts/env.sh
make verify-all
python3 scripts/verify-handoff-contract.py
python3 scripts/verify-vector-bounds.py
python3 scripts/verify-irq-contract.py
python3 scripts/verify-tx-full.py
python3 scripts/verify-uart-baud.py
python3 scripts/verify-cli-week34.py
python3 scripts/verify-manifest.py
```

verify-all 包括 Week1 至 Week5 和反馈回归，不包括上面另列的全部专项。Week5 验证会重新编译正式与故障测试目标，不依赖交付目录中的既有 ELF。具体手动演示步骤见 week5-guide.md。

| 原始资料 | 用途 |
|---|---|
| E/summary.json | 逐项分组、版本、元数据、故障快照哈希 |
| E/build.txt、configure.txt、size.txt | 重新构建与段尺寸 |
| E/native-*-tests.txt | 宿主断言和 sanitizer 结果 |
| E/interop-*.stderr | 第三方互操作接收结果 |
| E/场景名/uart.log | 实际 UART 二进制日志，保留协议控制字节 |
| E/场景名/qmp.json | 暂停、导出与重启会话的 QMP 记录 |
| reports/week5/delivery-index.md | 本轮回归、共享向量专项和交付包入口 |

本轮未观察到脚本报告失败或 QEMU guest error。不同版本的 ELF 调试路径可能影响文件 SHA256，因此包内交付 ELF 另行登记哈希并冒烟；不将不同构建的 ELF 文件声称为字节相同。

## 未验证能力

固件来源认证、自动故障回退、看门狗和完整串口配置为 Week6 范围。CRC 不能替代签名。当前活动镜像损坏会进入恢复入口，不保证自动启动另一槽。FPU 活动上下文、任意外设残留状态、真实 NOR 自动掉电持久化和工业标准符合性不由本报告证明。

本报告和原始证据支持当前配置中的阶段成果，不等于带教验收完成；前五周完整要求的未闭环项见 planning/week1-5-conformance-2026-11-01.md。
