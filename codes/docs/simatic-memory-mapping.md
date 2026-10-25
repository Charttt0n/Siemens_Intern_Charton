# SIMATIC 存储职责与本项目内存布局对应依据

核查日期：2026-10-23。适用范围：原计划 Week 3 内存映射交付，以及 Week 4 应用加载地址说明。

## 1. 参考对象与结论

本说明以 SIMATIC S7-1500 的公开存储模型为依据，选择 **CPU 1511-1 PN，订货号 6ES7511-1AK02-0AB0** 作为文档研究样本。该型号是本次研究的选择，不代表带教已指定目标硬件，也不代表本项目在该 CPU 上运行。官方产品页列出程序工作存储器 150 KB、数据工作存储器 1 MB，并要求 SIMATIC Memory Card；这些参数用于标识样本，不作为本项目的容量配置。[S1]

本项目运行于 QEMU MPS2-AN500 Cortex-M7。可以建立的对应是**存储职责对应**：保存程序、运行代码和数据、保存需跨重启保留的参数。现有官方资料不能支持将本项目四分区地址称为 SIMATIC CPU 内部固件地址，也不能证明 SIMATIC 使用本项目的 A/B 双槽机制。

因此，Week 3 的内存映射交付已有公开依据及实现映射；若验收要求严格复刻指定 SIMATIC 型号的内部固件布局，仍需指定型号及可核实的内部布局资料，当前不能据此宣告满足该物理布局要求。

## 2. 可核实的官方依据

| 编号 | 官方资料与位置 | 支持的事实 | 在本项目中如何使用 |
|---|---|---|---|
| S1 | CPU 1511-1 PN 官方产品页，Article Number / Product Description | 指定订货号的程序与数据工作存储容量；需要存储卡 | 固定研究样本，说明不同产品容量不能直接套用到 AN500 |
| S2 | STEP 7 V20，11/2024，CPU memory areas，Load memory / Work memory | 装载存储位于存储卡；CPU 内工作存储分为代码和数据职责 | 作为“保存镜像”和“运行代码／数据”分离的设计参考 |
| S3 | STEP 7 V20，11/2024，Retentive memory areas，Introduction | 指定数据可跨掉电保留；容量依具体 CPU 技术规格 | 作为参数独立存储的职责参考，不推导持久化实现方式 |
| S4 | STEP 7 V20，11/2024，What you need to know about SIMATIC memory cards，Setting the card type / Folders and files | 程序卡与固件更新卡用途不同，分别涉及 SIMATIC.S7S、FWUPDATE.S7S；另有 Backups 文件夹 | 区分用户程序、更新文件和备份文件；不能把文件夹解释成内部主备 Flash 槽 |

本说明的对应关系是依据上述事实作出的项目设计解释。PLC 用户程序（OB/FB/FC/DB 等）与本工程 Cortex-M 裸机应用不是相同格式或软件层次，不具备互相加载的兼容性。固件更新卡上的文件也不等于 Bootloader 内部执行区。[S2][S4]

## 3. 从官方职责到项目分区的对应

所有地址和容量均来自本项目代码。以下使用半开区间，单位 KiB = 1024 字节。

| 本项目区域 | 镜像偏移范围／大小 | 对应的公开职责或来源 | 对应程度与限制 |
|---|---|---|---|
| FLASH_BOOT | [0x00000, 0x10000)，64 KiB | 原项目计划要求独立 Bootloader 区域 | 项目自定义只读预留槽；没有查得该 SIMATIC 样本的 Boot ROM 地址或容量。当前运行 Bootloader 由 QEMU 直接加载，不从此槽启动 |
| FLASH_APP | [0x10000, 0x80000)，448 KiB | S2/S4 中保存程序与运行程序相分离的职责 | 职责类比。槽内保存本项目 M7IM 镜像，非 SIMATIC.S7S 文件格式；仅承担裸机应用镜像的装载来源 |
| FLASH_BACKUP | [0x80000, 0xf0000)，448 KiB | 原计划明确要求备份固件区，后续 Week 5 设计主备升级 | 项目扩展；S4 的 Backups 文件夹不能证明固件双 bank。当前仅实现分区及读写擦除，未实现升级切换或自动恢复 |
| FLASH_PARAMS | [0xf0000, 0x100000)，64 KiB | S3 中需跨重启保存的数据职责 | 职责类比。目前只存 W3OK 演示数据，依赖宿主显式保存；未实现 SIMATIC 的掉电保持机制，也未实现升级状态记录 |

镜像总长为 1 MiB，映射至 AN500 的 [0x20300000, 0x20400000)。分区连续且互不重叠，由 [flash.c](../src/week3/flash.c) 中的 partitions 数组实现。4 KiB 擦除和 4 字节编程粒度由 [flash.h](../src/week3/flash.h) 定义，是本项目 NOR 规则模型的契约，不是 SIMATIC 官方参数。

64/448/448/64 KiB 的容量安排用于固定大小的实验镜像：主备等容量支持后续容纳相同固件；启动和参数各预留 64 KiB，剩余均分为两槽。各边界满足 4 KiB 擦除对齐。其合理性来自本工程空间约束与后续升级目标，不来自 S7 CPU 的物理容量或分区表。

## 4. 装载区与运行区的对应

| 本项目运行区 | AN500 地址范围 | 与 SIMATIC 职责的关联 | 实现依据 |
|---|---|---|---|
| Bootloader 代码 | [0x00000000, 0x00010000) | 项目启动层，未获得 SIMATIC 内部启动区地址依据 | src/week4/linker.ld |
| 应用代码 | [0x00010000, 0x00080000) | 类比 S2 的代码工作存储职责；此区域为 QEMU SSRAM | src/week4/app/linker.ld |
| Bootloader RAM | [0x20000000, 0x20010000) | 项目启动层运行数据和栈 | src/week4/linker.ld |
| 应用 RAM | [0x20010000, 0x20200000) | 类比 S2 的数据工作存储职责，含应用 data、bss 和栈 | src/week4/app/linker.ld |

Week 4 从 FLASH_APP 偏移 32 字节后的 payload 读取应用，校验后复制到 0x00010000，再复核 CRC 和向量。32 字节头不进入应用运行区；448 KiB 槽的有效 payload 上限为 458720 字节。向量表、MSP 及中断交接按本项目 Cortex-M7 契约执行。[image.c](../src/week4/image.c)、[image.h](../src/week4/image.h)、[第四阶段指南](week4-guide.md)

这体现了保存与执行分离的设计，但不是 SIMATIC PLC 操作系统的程序装载实现。应用运行区与数据 RAM 同为易失存储；镜像 SRAM 的内容也只有在宿主保存检查点后才能跨 QEMU 进程恢复。[第三阶段指南第6节](week3-guide.md)

## 5. 已验证内容与验收边界

2026-10-23 在 codes 目录执行的第三阶段验收为 11 项 PASS，第四阶段为 23 项 PASS。本说明复核已有结果和源码，不将文档补充记作新执行的测试。

| 设计主张 | 可检查证据 | 可支持的结论 |
|---|---|---|
| 分区完整、对齐、隔离，Boot 区不可经 API 改写 | [test_flash.c](../tests/test_flash.c)；[Week 3 摘要](../logs/week3-20261023-211721-326453/summary.json) | 同组断言在宿主和 Cortex-M7 执行，覆盖边界、写保护、NOR 规则和擦除隔离；不是硬件写保护 |
| 参数保存后重启可恢复 | 同一 Week 3 摘要；[第三阶段测试报告](week3-test-report.md) | 保存／重启、擦除／重启、SIGKILL 后恢复最后检查点、保存失败保留旧镜像；不是自动掉电保持 |
| 从存储槽校验、加载并运行应用 | [Week 4 摘要](../logs/week4-20261023-211727-570744/summary.json)；[正常串口日志](../logs/week4-20261023-211727-570744/valid/uart.log) | 当前应用成功启动，错误镜像被拒绝；QEMU 未直接加载应用 ELF |
| 加载和交接符合固定地址约定 | 同一 Week 4 目录中的 dirty-state-handoff-and-app-crt.txt、reject-corrupted-loaded-copy.txt | 检查 MSP、VTOR、中断、应用 CRT、SVC 及复制后损坏拒绝；不证明真实 SIMATIC 硬件行为 |

当前没有证据支持 SIMATIC 内部 Boot ROM、密钥区、主备固件槽地址、镜像选择元数据或自动回滚流程与本项目相同。上述内容保留为未知，不能由公开 PLC 存储区或存储卡目录反推。若带教要求与真实硬件逐地址对齐，需先明确目标 CPU 订货号、固件版本及授权可用的内部布局资料，再另行评审迁移方案。

交付时可表述为：**“内存设计参考 SIMATIC S7-1500 装载、运行和保持性数据分离的公开职责模型，按项目任务定义 Bootloader、主应用、备份和参数四区；地址、容量、主备槽和持久化机制是 AN500 仿真实现，不宣称复刻 SIMATIC 内部固件布局。”**

## 6. 官方来源

均于 2026-10-23 检索。网页版本和栏目在第2节列出，文中为归纳说明；产品页检索结果可核实型号描述，直接打开产品页时返回 403，故未据此扩展任何内部参数。

- [S1：Siemens CPU 1511-1 PN 官方产品页](https://mall.industry.siemens.com/mall/en/gb/Catalog/Product/6ES75111AK020AB0)
- [S2：Siemens CPU memory areas (S7-1500)，STEP 7 V20](https://docs.tia.siemens.cloud/r/en-us/v20/functional-description-of-s7-1500-cpus-s7-1500/memory-areas-s7-1500/cpu-memory-areas-s7-1500)
- [S3：Siemens Retentive memory areas (S7-1500)，STEP 7 V20](https://docs.tia.siemens.cloud/r/en-us/v20/functional-description-of-s7-1500-cpus-s7-1500/memory-areas-s7-1500/retentive-memory-areas-s7-1500)
- [S4：Siemens What you need to know about SIMATIC memory cards (S7-1500)，STEP 7 V20](https://docs.tia.siemens.cloud/r/en-us/v20/functional-description-of-s7-1500-cpus-s7-1500/memory-areas-s7-1500/what-you-need-to-know-about-simatic-memory-cards-s7-1500)
