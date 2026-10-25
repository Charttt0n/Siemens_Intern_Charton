# Week 1 测试用例与验收报告

更新日期：2026-10-24。本轮为老师反馈修订，待老师审核。历史记录保留；下列新证据为本轮判定依据。测试中的预期拒绝不等于套件失败。

入口：在 codes/ 执行 make verify-week1。证据目录：`logs/week1-review-20261024-173002`。完整步骤及输入保存在对应 .gdb／命令日志／验证脚本；摘要记录本轮判定。

验证路径：干净构建 → 指定新 ELF → 输入或故障注入 → 输出与状态断言 → 原始日志 → summary.json。未出现预期输出、断言失败或意外超时均判失败。

| 用例编号与方式 | 实际输入与步骤 | 预期输出 | 实际输出与验证证据 |
|---|---|---|---|
| W1-01 工具版本 | qemu/gcc/cmake/gdb/make/git --version | 工具存在；QEMU≥7.0，GCC≥12.3，CMake≥3.20 | 六工具检查通过；logs/week1-review-20261024-173002/qemu-system-arm-version.txt |
| W1-02 干净构建 | 新目录；CMake --target demo.elf；检查ELF段、向量和符号 | 编译退出0；生成同一被测ELF | 退出0，ELF/工具版本与源摘要见summary；logs/week1-review-20261024-173002/build.txt |
| W1-03 正常启动 | QEMU mps2-an500加载新demo.elf | PASS标记，退出0 | PASS: Cortex-M7; .data initialized; .bss zero; bare-metal C99；退出0；logs/week1-review-20261024-173002/nominal-qemu.txt |
| W1-04 GDB污染与单步 | 复位处data填CC、bss填A5，_ebss写0x12345678；运行main并stepi | data恢复，bss清零，哨兵不变，MSP/PC正确 | MSP=0x20400000，PC=0xc4；data=78563412；bss=8个00；哨兵0x12345678；单步PC=0x46；logs/week1-review-20261024-173002/poisoned-startup-gdb.txt |
| W1-05 负向data | main入口 initialized_data=0xdeadbeef | 固件FAIL，demo_result=0xdead，QEMU退出1 | FAIL及状态断言通过，退出1为预期；logs/week1-review-20261024-173002/corrupt-data-gdb.txt |
| W1-06 负向bss | main入口 zero_data=0xdeadbeef | 固件FAIL，demo_result=0xdead，QEMU退出1 | FAIL及状态断言通过，退出1为预期；logs/week1-review-20261024-173002/corrupt-bss-gdb.txt |
