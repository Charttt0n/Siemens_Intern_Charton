# 第一周仿真平台说明

本工程使用 QEMU mps2-an500，内核是 Cortex-M7。链接布局来自 QEMU 的 MPS2 板级实现：

| 区域 | 本例使用的地址 | 大小 | 用途 |
|---|---|---|---|
| ZBT SSRAM1 | 0x00000000 | 4 MiB | 向量表、代码、常量、.data 初值 |
| ZBT SSRAM2/3 | 0x20000000 | 4 MiB | .data、.bss、栈 |

`CODE` 是链接脚本中为代码区域取的名字；这块模拟内存实际是 SRAM，并非已经实现擦写、掉电保存的 Flash。

## 本例启动过程

1. Cortex-M 从向量表首项加载 MSP，从第二项取得 Thumb 复位入口。
2. `Reset_Handler` 复制 `.data` 初值，清零 `.bss`，调用 C99 的 `main`。
3. C 程序读取 CPUID 的 PartNo，检查是否为 Cortex-M7（0xC27），并检查两个全局变量的初始化结果。
4. semihosting 输出验证结果并退出 QEMU；它是第一周工具链冒烟验证手段，后续 UART 日志需要另行开发。
5. GDB 经 QEMU 内置 gdbstub 连接，验证断点、单步和变量读取。

本例不配置真实 MCU 时钟、缓存、MPU 或外设；这些工作需根据后续选定硬件和需求补充。运行本例不意味着已经实现 bootloader、SIMATIC S7 启动架构或 IEC 62443 合规。

## 官方依据

- [QEMU MPS2 平台](https://www.qemu.org/docs/master/system/arm/mps2.html)
- [QEMU 板级实现及内存布局](https://github.com/qemu/qemu/blob/master/hw/arm/mps2.c)
- [QEMU GDB 调试](https://www.qemu.org/docs/master/system/gdb.html)

## 安装来源

Homebrew 7.0.9 官方发行包：https://github.com/Homebrew/brew/releases/tag/7.0.9

安装包 SHA-256：`e021295947a4a9b7ca1a8eff310c9ea1db14d923c5acf7d79e325b60a81f1a15`。

下载后通过 macOS `pkgutil --check-signature` 验证：Developer ID Installer: Patrick Linnane (927JGANW46)，Apple 公证服务信任。工具通过 Homebrew 官方 formula 安装；实际版本及验证结果记录在 `logs/`。

## 延伸学习

完整的工业需求调研、启动代码解读和第一周学习记录见 [第一周学习笔记](week1-learning-notes.md)。原始环境验证结果仍见 [验证报告](validation-report.md)。
