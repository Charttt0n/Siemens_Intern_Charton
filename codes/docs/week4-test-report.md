# 第四阶段测试报告

验收日期：2026-10-22。实际实习第二周提前完成原计划第四阶段。本次最终 **23 项检查通过**。

最终证据：[summary.json](../logs/week4-20261022-164853-369970/summary.json)。入口：在 `codes` 执行 `python3 scripts/verify-week4.py`。每次使用全新临时构建目录及独立镜像，不依赖 `build/` 缓存。

## 1 覆盖与结果

| 分组 | 项数 | 实际检查 |
|---|---|---|
| 构建与地址 | 1 | 独立 Bootloader／应用 ELF；栈与向量地址一致；反汇编无 BKPT |
| CRC oracle | 1 | 标准串及空数据，8 个确定性数据长度（0 至 458720）与 zlib 比较；ASan／UBSan |
| 镜像输入 | 18 | 同一输入分别经过宿主 image_validate 和真实 QEMU 启动；两处结果一致 |
| 状态交接与应用 CRT | 1 | 实际执行 CPSID 后注入 NVIC、SysTick、PendSV；入口状态正确；应用 data／bss 污染恢复及哨兵保留；SVC 执行一次 |
| 加载副本损坏 | 1 | 复制后、再次 CRC 之前翻转运行区一位；拒绝码 8，未进入应用 |
| 模型日志 | 1 | 所有正常与拒绝场景无 guest-error 诊断 |

18 个镜像输入为：正常、全空白、错误 magic、不支持的格式、错误头大小、头 CRC 错、空 payload、超长 payload、错误加载地址、内容损坏、MSP 未对齐、MSP 落在 Boot RAM 边界、reset 非 Thumb、reset 越界、reset 落在向量表中、外部 IRQ 向量无效、保留项非零、最大长度 payload。CRC 和向量错误测试独立构造；向量错误用例重新计算两层 CRC，确保拒绝来自向量检查。

正常 UART 输出包含 IMAGE CRC32 PASS、LOAD、JUMP、应用 CRT PASS、HANDOFF PASS、SVC PASS 与 RUNNING。内容逐字节比较，不用超时或 QEMU 终止码代替固件状态判定。负向用例逐字节匹配指定 REJECT 码，不能出现应用运行日志。

## 2 实际构建数据

| 目标 | text | data | bss |
|---|---:|---:|---:|
| bootloader-week4.elf | 2316 | 0 | 4276 |
| application.elf | 960 | 12 | 4132 |

单位为字节。bss 包含各自预留的 4096 字节栈，不是实测栈高水位；镜像 payload 为 972 字节。ELF SHA256 见摘要，工具版本和反汇编见同目录。这些结果未测量启动耗时或真实 MCU 性能。

## 3 测试问题与修正

首轮证据 `week4-20261022-164438-430084` 的状态为 FAIL：直接通过 GDB 写 PRIMASK 后，本机 QEMU 未按预期改变屏蔽状态，注入的 PendSV 提前触发默认异常，未抵达应用入口。修正为单步执行 Bootloader 中真实 CPSID 指令并读回 PRIMASK，再注入挂起状态。最终完整验收重新执行并通过；没有将首轮超时判为成功。测试工具也补充了超时输出保存。

GDB 最终检查原始入口 MSP 为 `0x20200000`、VTOR 为 `0x10000`、PRIMASK 为 1、CONTROL／BASEPRI／FAULTMASK 为 0、外部 IRQ 禁止且无挂起、SysTick 和 UART 已停用。之后在应用初始化前污染其 data 与 bss，最终应用状态为成功，data 初值与 bss 数组恢复、末尾哨兵不变、SVC 计数为 1。

## 4 前期回归与覆盖边界

同日第一阶段 6 项、第二阶段 12 项、第三阶段 11 项全部通过。目录分别为 `week1-review-20261022-164544`、`week2-20261022-164548` 和 `week3-20261022-164553-309176`，均位于 `codes/logs`。

CRC 不能认证镜像来源；无签名、防回滚或恶意代码沙箱。当前只启动主应用，不进行备份恢复。未测试真实 Flash、电源中断、缓存启用、MPU、DMA、任意中断上下文跳转及物理 UART 时序。应用必须遵守本项目固定地址和状态交接约定，不能直接启动任意二进制。
