# 计划第四阶段 固件校验与应用跳转

2026-10-22 实现并验收，实际归入实习第二周提前完成的任务。week4 为原计划阶段编号。

## 1 运行与目标

在 `codes/` 执行 `bash scripts/build-week4.sh`，再执行 `python3 scripts/run-week4.py`。前者生成 `build/week4/bootloader-week4.elf`、`application.elf`、`application.bin` 和 `demo-flash.bin`；后者加载 Bootloader ELF 和存储镜像，输出应用运行结果后停止 QEMU。

应用 ELF 不直接交给 QEMU。Bootloader 通过第三阶段的 `flash_read` 读取主应用存储槽，校验镜像并复制 payload 到应用运行地址，再执行交接。第一至第三阶段仍为独立构建目标。演示镜像每次构建会重建，不应当作保存用户实验数据的文件。

## 2 镜像格式

主应用分区起点为镜像文件偏移 `0x10000`，分区长 `0x70000`。前 32 字节为头，后面为应用原始 binary；应用 binary 自带向量表、代码、只读数据和 `.data` 初值。

| 头内偏移 | 字段 | 约定 |
|---|---|---|
| 0 | magic | 小端 `0x4d49374d`，字节文本 M7IM |
| 4 | format | 固定 1，不支持的格式拒绝 |
| 8 | header_size | 固定 32 |
| 12 | payload_size | 至少 192，最多 458720 字节 |
| 16 | load_address | 固定 `0x00010000`，拒绝任意地址写入 |
| 20 | firmware_version | uint32 信息字段，暂未实施防回滚 |
| 24 | payload_crc32 | 仅覆盖 payload |
| 28 | header_crc32 | 覆盖头的前 28 字节 |

所有整数都显式按小端解析，不直接将输入强制转换为 C 结构体。CRC 为 CRC-32/ISO-HDLC：反射多项式 `0xedb88320`、初始值及输出异或 `0xffffffff`，与 zlib 兼容；标准测试串 123456789 的结果为 `0xcbf43926`。CRC 是损坏检测，不是签名认证。

打包工具 `scripts/pack-week4.py` 默认拒绝覆盖已有输出，显式 `--force` 才允许替换；构建脚本仅对生成的演示镜像使用该选项。可用 `--version` 指定镜像版本。打包时也检查应用长度与向量，但固件仍独立校验，不信任宿主打包结果。

## 3 地址约定

| 区域 | 半开地址范围 | 用途 |
|---|---|---|
| Bootloader CODE | `[0x00000000, 0x00010000)` | 向量表与启动代码 |
| 应用 CODE | `[0x00010000, 0x00080000)` | 应用运行区，payload 上限预留 32 字节头空间 |
| Bootloader RAM | `[0x20000000, 0x20010000)` | 静态数据与 4 KiB 栈 |
| 应用 RAM | `[0x20010000, 0x20200000)` | 静态数据与末尾 4 KiB 栈 |
| 模拟存储 | `[0x20300000, 0x20400000)` | 第三阶段的 1 MiB NOR 模型 |

Bootloader 初始 MSP 为 `0x20010000`，示例应用初始 MSP 为 `0x20200000`。镜像要求 MSP 在应用 RAM 内且 8 字节对齐，允许指向顶端但不允许等于 RAM 下界。应用向量表 48 项、192 字节，起点按 256 字节对齐。保留项必须为零，其余项必须为 Thumb 地址，且落在 payload 内、向量表之后。这样同时限制了 reset 与异常／外部 IRQ 的处理入口。

第三阶段的逻辑 Bootloader 存储分区仍为预留只读区，不等于运行地址 0 的 ELF 副本。备份分区尚不参与启动选择；本阶段只从主应用区启动。

## 4 验证和交接顺序

1. 初始化 C 环境、板级状态和 UART。
2. 校验头格式与头 CRC；在读取 payload 前检查长度与固定目的地址。
3. 分块读取 payload 计算 CRC；检查完整向量表。
4. 将 payload 复制到应用运行区；重新计算 CRC 并复核向量。
5. 排空启动日志，屏蔽中断；关闭 UART、SysTick，禁止并清除 32 个 NVIC 外部 IRQ，清除 SysTick／PendSV 挂起。
6. 设置 VTOR 为 `0x00010000`，执行 DSB／ISB。
7. 汇编清 CONTROL、BASEPRI、FAULTMASK 和 PSP，切换 MSP，以 BX 进入 Thumb reset 入口；全局中断保持屏蔽，供应用完成初始化。
8. 应用初始化自己的 `.data` 和 `.bss`，检查交接状态，重启 UART，开启全局中断，通过 SVC 验证异常向量后输出 RUNNING。

切换 MSP 后没有 C 函数返回或旧栈访问；意外返回进入汇编等待陷阱。此函数只适用于本工程的特权线程模式、冷启动且缓存／MPU 未启用状态，不宣称可从任意中断或 RTOS 上下文安全跳转。实际硬件需扩展缓存维护、DMA 停止和外设复位等步骤。

## 5 错误与测试

错误码：1 存储／参数错误，2 格式错误，3 头 CRC，4 长度，5 加载地址，6 payload CRC，7 向量，8 复制后 CRC。被拒绝时输出 `REJECT`，保存 `boot_status` 并停留在 Bootloader；不执行应用。UART 错误为 `0x101`，默认异常为 `0x100` 并记录 `exception_number`。

运行 `python3 scripts/verify-week4.py` 获得全新构建与证据。测试包含独立 CRC oracle、宿主内存检查器、正常与错误镜像、最大长度镜像、GDB 交接状态检查、应用数据污染恢复和运行区损坏拒绝。详细结论见 [第四阶段测试报告](week4-test-report.md)。

## 6 实现参考

2026-10-22 核对 Arm 官方 CMSIS 的 SCB／NVIC／SysTick 定义及寄存器操作，代码采用项目所需的最小寄存器访问方式，不引入 CMSIS 库。

- [Arm CMSIS Cortex M7 Core 定义](https://github.com/ARM-software/CMSIS_6/blob/main/CMSIS/Core/Include/core_cm7.h)
- [Arm CMSIS GCC 寄存器与屏障操作](https://github.com/ARM-software/CMSIS_6/blob/main/CMSIS/Core/Include/cmsis_gcc.h)
- [第三阶段存储契约](week3-guide.md)
