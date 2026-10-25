# Week 2 测试用例与验收报告

更新日期：2026-10-24。本轮为老师反馈修订，待老师审核。历史记录保留；下列新证据为本轮判定依据。测试中的预期拒绝不等于套件失败。

入口：在 codes/ 执行 make verify-week2。证据目录：`logs/week2-20261024-173006`。完整步骤及输入保存在对应 .gdb／命令日志／验证脚本；摘要记录本轮判定。

验证路径：干净构建 → 指定新 ELF → 输入或故障注入 → 输出与状态断言 → 原始日志 → summary.json。未出现预期输出、断言失败或意外超时均判失败。

| 用例编号与方式 | 实际输入与步骤 | 预期输出 | 实际输出与验证证据 |
|---|---|---|---|
| W2-01 模型时钟 | info qtree | CPU及5个UART输入25000000Hz | 25MHz读回通过；logs/week2-20261024-173006/model-clock.txt |
| W2-02 构建与ELF | 新目录构建demo.elf和bootloader.elf；objdump/nm | 无BKPT；-Werror构建成功 | 退出0；无semihosting指令；logs/week2-20261024-173006/build.txt |
| W2-03 前期回归 | 运行独立Week1 ELF | PASS并正常退出 | PASS，退出0；logs/week2-20261024-173006/week1-regression.txt |
| W2-04 重复启动 | 第1次独立QEMU实例 | 与verify-week2.py expected_uart逐字节相同 | 370字节含CRLF完全匹配；logs/week2-20261024-173006/uart-repeat-1-uart.log |
| W2-05 重复启动 | 第2次独立QEMU实例 | 与verify-week2.py expected_uart逐字节相同 | 370字节含CRLF完全匹配；logs/week2-20261024-173006/uart-repeat-2-uart.log |
| W2-06 重复启动 | 第3次独立QEMU实例 | 与verify-week2.py expected_uart逐字节相同 | 370字节含CRLF完全匹配；logs/week2-20261024-173006/uart-repeat-3-uart.log |
| W2-07 GDB初始化 | data20字节填CC、bss36字节填A5、末尾哨兵；运行main及boot_idle | data五字恢复，bss清零，哨兵不变；状态/寄存器符合 | boot_status=1，VTOR=0，CTRL=1，BAUDDIV=217；bss36字节清零；logs/week2-20261024-173006/poisoned-startup-and-registers-gdb.txt |
| W2-08 参数边界 | (clock,baud)=(0,115200),(25000000,0),(25000000,2000000),(25000000,1)；NULL；禁用TX | 各非法调用=-1且配置不变；有效配置成功 | 全部拒绝；有效分频217；继续启动状态1；logs/week2-20261024-173006/uart-parameter-boundaries-gdb.txt |
| W2-09 data损坏 | main入口 runtime_data[4]=0xdeadbeef | boot_status=2，FAIL，无PASS | 状态2；输出逐字节符合负向模板；logs/week2-20261024-173006/corrupt-data-gdb.txt |
| W2-10 bss损坏 | main入口 runtime_bss[6]=0xdeadbeef | boot_status=2，FAIL，无PASS | 状态2；输出逐字节符合负向模板；logs/week2-20261024-173006/corrupt-bss-gdb.txt |
| W2-11 UART错误传播 | 首次uart_write前 CTRL=0 | boot_status=3，UART零字节 | 状态3；进入boot_idle；零字节；logs/week2-20261024-173006/disabled-uart-gdb.txt |
| W2-12 异常路径 | main首条指令改UDF #0 | HardFault，exception_number=3 | 进入fault_idle；异常号及xPSR异常位均3；logs/week2-20261024-173006/undefined-instruction-gdb.txt |
