# Week 3–4 UART 波特率验证报告

## UART 波特率计算与寄存器验证

本项补齐Week 3–4 UART0的计算依据、取整过程、寄存器读回及仿真边界。专项验证对两个阶段的生产固件分别干净构建并运行，两项均通过。Week4使用全FF镜像使Bootloader停在拒绝分支，读回的是Bootloader初始化后的寄存器，不是应用重新初始化后的状态。

### 公式与取整

本工程外设为CMSDK APB UART。设UART输入时钟为 fPCLK、目标波特率为 Btarget、整数分频为 D，则 Dideal = fPCLK / Btarget；分频后的理论波特率 Bactual = fPCLK / D。此BAUDDIV直接记录时钟分频值，无需在分母额外乘16。依据见Arm CMSDK手册4.3节、表4-7及4-10页的分频示例。[Arm CMSDK技术手册 DDI0479D](https://documentation-service.arm.com/static/5e8f224c7100066a414f7810)

输入 fPCLK = 25000000 Hz，Btarget = 115200 baud：

```text
Dideal = 25000000 / 115200 = 217.0138888889
整数商 q = 217
余数 r = 25000000 - 217 * 115200 = 1600
四舍五入阈值 = ceil(115200 / 2) = 57600
1600 < 57600，因此不进位，D = 217 = 0x000000D9
```

实际代码在 src/week3/uart.c 的 uart_init 中先做整除，再判断 r >= baud - baud/2 才进位；采用商和余数避免 clock_hz + baud/2 的加法溢出。两个输入须非零，取整后D须落在[16, 0xFFFFF]；不合格时返回-1，在修改寄存器之前退出。Week4复用同一驱动。此处记录源码规则，本次专项只新增默认配置的读回验证，不声称重跑了所有非法参数测试。

### 实际波特率及量化误差

```text
Bactual = 25000000 / 217 = 115207.3732718894 baud
差值 = Bactual - Btarget = +7.3732718894 baud
相对误差 = (Bactual - Btarget) / Btarget
         = 0.00006400409626
百分比误差 = 相对误差 * 100% = +0.006400409626%
ppm误差 = 相对误差 * 1000000 = +64.00409626 ppm
理论位宽 = D / fPCLK = 8.68 us
8N1连续发送一帧为10位，理论帧时间 = 86.8 us（不含帧间空闲）
```

正号表示分频结果略快于目标。误差只含整数分频量化误差，未计入晶振偏差、PLL/时钟树误差、抖动和对端时钟偏差，不能据此直接判断真机链路的总体容差。

### 寄存器读回与证据

复验命令：在 codes/ 执行 `python3 scripts/verify-uart-baud.py`。脚本在临时目录构建两个Bootloader，用QMP的info qtree定位MMIO基址0x40004000对应的UART0，读取其pclk-frq，随后经GDB读回32位寄存器并断言。证据根目录 BAUDLOG = `logs/uart-baud-20261024-235338`（相对 codes/）。该目录也保存构建输出、QEMU版本、GDB命令文件和ELF SHA256；本轮QEMU为11.1.2。

| 固定编号 | 操作与具体输入 | 预期 | 实际与判定 |
|---|---|---|---|
| BAUD-W3 | 干净构建Week3；默认空白存储；启动至UART提示符；读qtree和寄存器 | UART0 pclk-frq=25000000；BAUDDIV=217；CTRL=0x0B | 25000000；0x000000D9 (217)；0x0000000B；PASS |
| BAUD-W4 | 干净构建Week4；全FF镜像；停在REJECT后读qtree和寄存器 | 同上，读取Bootloader状态 | 25000000；0x000000D9 (217)；0x0000000B；PASS |

BAUD-W3证据：BAUDLOG/week3-qtree.txt、BAUDLOG/week3-registers.gdb、BAUDLOG/week3-registers.txt、BAUDLOG/week3/uart.log。BAUD-W4对应week4同名文件；汇总为BAUDLOG/summary.json。两个registers.txt均包含UART_BAUD_READBACK_PASS。

UART0基址为0x40004000；BAUDDIV偏移0x10，读回地址为0x40004010。CTRL偏移0x08，读回地址0x40004008；0x0B表示bit0 TX使能、bit1 RX使能、bit3 RX中断使能，bit6高速测试模式未开启。GDB可用 `x/wx 0x40004010` 和 `x/wx 0x40004008` 人工复核。

### 仿真验证边界

本轮证明模型UART0的时钟属性、固件写入的分频和使能寄存器符合设计，且启动字符可通过模型后端接收。QEMU 11.1.2的cmsdk-apb-uart.c通过整数除法将pclk_frq/bauddiv传给字符后端的串口参数接口，数值为115207；它与上述精确分频推导值的表示精度不同。[QEMU 11.1.2 UART模型源码](https://github.com/qemu/qemu/blob/v11.1.2/hw/char/cmsdk-apb-uart.c)

本工程使用Unix socket字符后端，接收的是字节流，没有测量物理TX/RX波形。因此终端字符正确、PONG应答成功、宿主日志时间间隔都不能作为115200物理线速、8.68 us位宽、对端容差或实物时钟准确性的实测证据。真机验收仍需确认实际UART输入时钟，并用示波器或逻辑分析仪测量重复0x55等数据的TX位宽，按 Bmeasured = 1/Tbit 计算实测波特率，再与目标及对端容差比较。此项尚未开展。
