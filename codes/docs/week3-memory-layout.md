# Week3 内存分区说明

更新日期：2026-10-25　平台：QEMU MPS2 AN500 Cortex M7

## 一 交付结论与适用范围

本说明对应原八周规划第3周的内存分区交付物。当前代码已定义Bootloader、主应用、备份固件和参数四区，连续覆盖1 MiB模拟存储；地址、容量、访问权限、操作粒度、错误返回和验证依据均在本文列明。最新正式工程验收11组检查通过。

存储后端为SRAM实现的NOR规则模型，持久化依赖宿主显式检查点。本说明满足当前仿真实现的分区定义与可复核材料要求；原计划中的片内Flash、自动掉电保持及SIMATIC内部物理布局对齐，尚无等价硬件证据，不能据此宣称已完成真机验收或工业认证。

原计划依据：planning/Industrial Embedded Systems Bootloader Development.pdf第3至4页，以及工时补充版的Week3任务。预计2小时为计划投入，不是实际计时记录。固件校验和应用跳转按计划归Week4，不计入本说明的Week3成果。

## 二 运行地址与存储地址的区别

CPU运行区由src/week3/linker.ld定义；镜像文件偏移由src/week3/flash.c分区表定义。两者不能混用。

| 区域 | CPU半开地址范围 | 容量与用途 |
|---|---|---|
| 启动代码与向量表 | [0x00000000, 0x00010000) | 64 KiB；QEMU加载Bootloader ELF；实际为SSRAM |
| C运行RAM | [0x20000000, 0x20300000) | 3 MiB；静态数据和栈 |
| 预留栈 | [0x202FF000, 0x20300000) | 4 KiB；包含于上述C运行RAM；初始MSP=0x20300000 |
| 模拟NOR存储 | [0x20300000, 0x20400000) | 1 MiB；位于ELF各段之外，由宿主加载镜像 |

启动代码不会清零模拟存储，flash_sim_bind只绑定已有内存，不执行格式化。首次创建镜像时由宿主填充0xFF。镜像中的Bootloader预留槽不是当前运行ELF的副本，当前启动入口仍由QEMU加载地址0的向量表提供。

## 三 四分区定义

以下区间均为左闭右开，KiB=1024字节。全部偏移相对于1 MiB镜像起点。

| 分区 | 镜像偏移范围 | 容量 |
|---|---|---|
| FLASH_BOOT | [0x00000, 0x10000) | 64 KiB |
| FLASH_APP | [0x10000, 0x80000) | 448 KiB |
| FLASH_BACKUP | [0x80000, 0xF0000) | 448 KiB |
| FLASH_PARAMS | [0xF0000, 0x100000) | 64 KiB |

| 分区 | CPU映射范围 | 用途与API权限 |
|---|---|---|
| FLASH_BOOT | [0x20300000, 0x20310000) | 启动层逻辑预留；允许读，禁止编程和擦除 |
| FLASH_APP | [0x20310000, 0x20380000) | 主应用存储槽；允许读写擦除，Week3不解析或启动镜像 |
| FLASH_BACKUP | [0x20380000, 0x203F0000) | 备份存储槽；允许读写擦除，未实现主备切换或自动恢复 |
| FLASH_PARAMS | [0x203F0000, 0x20400000) | 参数存储；演示命令操作起始4字节与第一个4 KiB擦除块 |

64+448+448+64=1024 KiB。四区连续、无重叠，起点与长度均按4 KiB擦除粒度对齐。启动和参数各预留64 KiB，余量等分主备槽，用于后续相同容量镜像；这属于本工程容量设计，不是SIMATIC官方分区容量。

地址换算规则：CPU地址=0x20300000+分区镜像偏移+分区内偏移。例如flash_read(dev,FLASH_PARAMS,0,buf,4)读取CPU地址0x203F0000起4字节，对应文件偏移0xF0000，而不是CPU地址0xF0000。

## 四 接口约束与错误码

| 操作 | 输入约束与成功效果 |
|---|---|
| flash_read | 任意字节对齐；范围有效时长度0允许分区末尾和NULL输出；非零长度须有有效输出且不得与后端重叠 |
| flash_program | 偏移和长度按4字节对齐；长度非0；仅允许1变0；先校验整个请求，再修改内存 |
| flash_erase | 偏移和长度按4096字节对齐；长度非0；擦除后全部为0xFF |
| 通用范围检查 | offset不得超过分区长度；size不得超过part_size-offset，避免offset+size溢出 |
| 调用与保护 | 前台单调用者；Boot区由API软件写保护；无MPU硬件隔离或中断并发写保证 |

| 返回值 | 名称 | 触发示例 |
|---|---|---|
| 0 | FLASH_OK | 有效读写擦除，或合法的零长度读 |
| -1 | FLASH_ARGUMENT | 无效设备/分区、NULL缓冲、零长度写擦、缓冲区重叠 |
| -2 | FLASH_RANGE | 越界、跨分区，或UINT32_MAX等超范围输入 |
| -3 | FLASH_ALIGNMENT | 编程未按4字节、擦除未按4 KiB对齐 |
| -4 | FLASH_PROTECTED | 向FLASH_BOOT编程或擦除 |
| -5 | FLASH_NEEDS_ERASE | 试图把已编程的0位写回1，须先擦除 |

错误码为有符号int。诊断命令将负值按32位十六进制显示，例如-1对应0xFFFFFFFF、-3对应0xFFFFFFFD。它们与Week4的IMAGE_*正数错误码不是同一枚举。多个条件同时非法时，以当前函数的检查顺序决定实际返回值。

## 五 检查点持久化与限制

宿主收到完整UART响应和提示符后，发送QMP stop，执行pmemsave导出0x20300000起1 MiB，检查长度，写同目录临时文件并fsync，再os.replace和fsync目录，finally尝试cont恢复执行。

已验证保存后重启恢复、擦除保存后恢复全FF、未保存擦除在SIGKILL后丢失、替换失败保持旧镜像及同镜像互斥。replace之前失败保护旧检查点；目录fsync若在replace之后失败，新镜像可能已经可见，不承诺回退。QMP断开时cont可能失败，错误须向上报告。

这里验证的是显式保存语义，不模拟真实Flash控制器、擦写耗时、寿命、掉电撕裂写入或自动落盘，也未开展宿主机突然断电试验。相同大小文件的内容完整性不由Week3存储层校验。

## 六 SIMATIC对应与验收判断

沿用2026-10-23的SIMATIC公开存储职责研究：装载、代码/数据运行和保持性参数相分离。研究样本、原始官方来源与限制见codes/docs/simatic-memory-mapping.md。该研究支持职责类比，不能推出内部Boot ROM、主备槽、密钥区地址或自动回滚机制。

| 原计划要求 | 当前材料与实现 | 判定 |
|---|---|---|
| 启动/主应用/备份/参数四区 | 四区地址、容量、权限及源码对应齐全 | 仿真范围已具备 |
| 分区定义文档 | 本说明列偏移、CPU地址、粒度、错误码和边界 | 已具备 |
| QEMU读写擦除与可靠性证据 | 存储操作测试报告及最新UART/GDB/QMP日志 | 已验证 |
| 片内Flash和SIMATIC布局对齐 | 当前为SRAM NOR模型、公开职责类比 | 严格硬件含义仍待老师确认 |

## 七 版本与验证依据

最新证据根目录（相对codes/）：logs/week3-20261025-192322-302943。summary.json为PASS，11组全部通过；生产ELF SHA256为a8b558aab31f286f01b3e643c236ce6568064ec0c10ce3763192bc580fc24477。完整源文件摘要和断言位置见codes/docs/week3-storage-validation.json。

| 源文件 | SHA256前16位 |
|---|---|
| codes/src/week3/flash.c | 135d5ef788d2b0dd |
| codes/src/week3/flash.h | 3415282061792154 |
| codes/src/week3/linker.ld | e24501f439a8e136 |
| codes/src/week3/main.c | abb6bf6c8e1fcc02 |
| codes/src/week3/startup.S | a281c0dea4a75477 |
| codes/tests/test_flash.c | 1145282071a31f87 |
| codes/scripts/verify-week3.py | 4df63c48fdad7486 |
| codes/scripts/week3_session.py | e6ed8e83e2e4a018 |

复验：在codes目录执行python3 scripts/verify-week3.py。具体存储操作步骤、实际输入、预期和实际结果见codes/docs/week3-test-report.md。源码、原始日志与两份PDF随Week3交付包提供；老师确认状态仍为待审核。
