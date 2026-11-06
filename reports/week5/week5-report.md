# Week 5 固件升级实习周报

Industrial Embedded Systems Bootloader Development 第 5 周。2026 年 11 月 6 日复验，供带教审阅固件升级实现、验证结果和适用边界。

## 本周完成情况

本周代码已完成单文件 YMODEM 接收、非活动槽写入、镜像完整性与向量验证、参数双页事务和活动槽切换。在 QEMU AN500 中，主机经 UART 将 v1 升级为 v2，再升级为 v3；重启后按持久参数选择新应用。传输失败和受控写入中断时，旧活动槽保持可启动；完整提交后重启选择新槽。

| 计划交付 | 本周成果 | 证据 |
|---|---|---|
| Ymodem 与串口升级源码 | ymodem.c、crc16.c、main.c、timebase.c | native 协议测试和第三方互操作；真实 UART 上传 |
| 主备管理与状态源码 | update.c、boot_meta.c；复用 Week3 Flash 和 Week4 镜像模块 | A/B 往返、参数读回、IO 和中断重启 |
| 成功失败中断测试报告 | week5-test-report.md/.pdf | 本轮 75 个检查分组全部通过 |
| 本周周报 | 本文及同目录 Word/PDF | 与原计划 Week5 范围对应 |

本周未记录实际投入工时。工时补充版中的 20 小时为计划估算，不作为实际工作量。当前成果是阶段仿真实现，尚待老师审核。

## 实现思路

升级最重要的约束是：新文件尚未完整校验和提交时，不改变旧活动槽，不擦除旧固件。两槽都是存储位置，实际执行地址统一为 0x00010000。活动槽由参数记录决定，Bootloader 从所选槽加载到该地址后交接；不会直接跳至 B 槽的存储地址。

代码分为协议、写入事务、参数事务和主流程四层。协议层只处理帧、顺序、CRC16 和传输终止；写入层处理镜像写入及 CRC32/向量验证；参数层保存可恢复的状态；主流程负责选择槽、接收命令和交接应用。

<!-- pagebreak -->

## 数据格式与提交规则

| 区域 | 存储偏移和大小 | 用途 |
|---|---|---|
| Boot | 0x00000，64 KiB | 只读逻辑预留区，运行 ELF 由 QEMU 装载 |
| A | 0x10000，448 KiB | 初始活动镜像；后续可作为升级目标 |
| B | 0x80000，448 KiB | 初始升级目标；提交后成为活动槽 |
| Params | 0xF0000，64 KiB | 首两页存事务记录，每页 4 KiB |

升级文件为 32 字节 M7IM 头加 payload，最大总长度 0x70000；最大 payload 为 0x6FFE0。当前 v1/v2/v3 的 payload 均为 1004 字节，包长 1036 字节，实际内容和输出版本不同。完整 1 MiB NOR 镜像和 ELF 都不能作为串口升级包发送。

状态顺序为 IDLE → RECEIVING → VERIFYING → READY → 新活动槽 IDLE。接受首包后，先写 RECEIVING，再擦除非活动槽。数据以 4 字节编程并读回，最后不足 4 字节用 0xFF 补齐；写入进度仅统计 payload。全文件校验通过后记 READY；激活时重新读取记录并校验候选，再提交 active_slot。

参数记录为 48 字节，包含 magic、schema、generation、active_slot、target_slot、state、progress、expected_length、candidate_version、error、CRC32 和最后写入的 CMIT 标记。读取时检查字段、CRC 和标记，只接受可排序的有效记录。两份均损坏或序号歧义时进入恢复入口，使用明确的 r 命令重建；不会在正常启动中猜测原活动槽。

## 接收和失败处理

支持 128/1024 字节帧、CRC16、包序号回绕、重复帧去重、取消、超时和有界重传。RX 环形队列总容量 128 字节，有效可排队 127 字节，独立于 1024 字节协议缓冲。软件丢包或硬件 overrun 会中止会话。

CRC16 只保护传输帧，CRC32 检查镜像头与完整 payload；二者均不认证固件来源。只有新数据或首包被接受才刷新无进展预算。成功终包 ACK 对应接收层完成，宿主仍需确认 COMMITTED、新版本输出和持久参数，不能仅凭 ACK 宣布升级成功。

<!-- pagebreak -->

## 测试结果

2026 年 11 月 6 日重新从源码构建并执行 verify-week5.py，75 个顶层检查分组全部通过。其中包含 24 条启动记录、12 条负向记录和 9 个故障快照场景。

| 测试内容 | 实测结果 | 可支持的结论 |
|---|---|---|
| v1→v2→v3 连续升级 | B 后 A 成为活动槽，输出各自版本 | UART 接收至新应用运行的完整流程通过 |
| 重启与旧槽保持 | 新进程仍运行新版本，旧槽逐字节不变 | 选槽遵从保存的参数，非依赖旧运行内存 |
| 最大容量升级 | 458752 字节包填满 B，冷重启运行 v2 | 满槽写入与校验通过，A 保持原样 |
| CRC32、向量、长度和超容量 | 拒绝候选，运行 v1 | 无效镜像不触发活动提交 |
| CRC16、丢 ACK、重复帧及回绕 | 独立发送端 9 场景通过 | 接收器可与固定版本第三方发送端互操作 |
| 取消、停滞、断连及 RX 溢出 | 有界退出，旧活动选择保持 | 失败路径不会静默提交残缺镜像 |
| 擦除、数据及参数提交中断 | 9 份当前 NOR 快照冷重启符合预期 | 标记完整前旧槽启动；完整标记后新槽启动 |
| 中断后重新上传 | 从数据写入中断快照完整重传，运行 v2 | 支持重试；未实现断点续传 |

本轮修复了 Reset 向量指向镜像最后一个字节时仍能提交的问题。打包器和固件现在都要求入口的首个 Thumb 半字完整落在 payload 内。QEMU 复验确认候选被拒绝，活动槽仍为 A 并运行 v1。参数查看命令可读取保存的 FAILED 和错误码。前四周主回归为 6、12、11、27 组通过，共享向量边界专项通过 323 条宿主断言和 11 条 CPU/GDB 路径。

## 问题与适用边界

Flash 为 SRAM 上的 NOR 规则模型。普通运行结束需显式 checkpoint；中断测试则在指定故障点锁存并暂停 CPU，导出当时完整 NOR，再用该快照启动全新正式固件进程。直接杀死 QEMU 后重新加载旧 seed 不等于升级中断恢复证据。本轮没有真实掉电或真实 Flash 试验。

参数事务每数据块可能擦写一页，真机移植需评估寿命、延迟和硬件写入原子性。系统时钟、UART 硬件时序、缓存/MPU/FPU 活动现场均受 AN500 当前配置限制。活动固件损坏后不自动搜索备份；自动运行故障回退属于 Week6。

<!-- pagebreak -->

## 复现与后续安排

从项目 codes 目录执行以下命令。首次 seed 建立 v1，后续必须经上传流程升级，避免用重新生成 seed 替代升级。

```sh
source scripts/env.sh
bash scripts/build-week5.sh
python3 scripts/seed-week5.py --slot-a build/week5/application-v1.pkg --output build/week5/demo-nor.bin
python3 scripts/send-week5.py --image build/week5/demo-nor.bin --package build/week5/application-v2.pkg --evidence logs/demo-v2
python3 scripts/send-week5.py --image build/week5/demo-nor.bin
python3 scripts/send-week5.py --image build/week5/demo-nor.bin --package build/week5/application-v3.pkg --evidence logs/demo-v3
python3 scripts/week5_meta.py build/week5/demo-nor.bin
make verify-week5
```

已有 seed 默认不覆盖；需要从头演示时先保留旧实验，再使用新的镜像文件名或明确的 --force。包中另附经过冒烟验证的固件副本，正式代码及可重复构建入口仍是主来源。

预期可见 UPGRADE: READY、UPGRADE: COMMITTED、IMAGE: SLOT B、APP: VERSION v2；下一次升级切回 A 并运行 v3。还应看到 APP: HANDOFF PASS 和 APP: SVC PASS。不带 --package 的命令用于读取持久镜像并验证启动。

| 材料 | 位置 |
|---|---|
| 本周交付索引 | reports/week5/delivery-index.md |
| 技术测试报告 | codes/docs/week5-test-report.md/.pdf |
| 操作与接口说明 | codes/docs/week5-guide.md |
| 本轮 Week5 原始证据 | codes/logs/week5-20261106-043520-600037 |
| 五周符合性与待补项 | planning/week1-5-conformance-2026-11-01.md |
| 统一交付入口 | planning/week1-5-delivery-index.md |

下一步先提交阶段材料供带教审核，并补齐 Week1 指定手册、S7 公开架构和工业要求适用性说明，明确仿真替代方案的验收边界。老师确认后，再按 Week6 规划推进认证、自动回退、看门狗和配置接口。当前不将后续功能写成已实现，也不将阶段验证表述为工业认证或量产验证。
