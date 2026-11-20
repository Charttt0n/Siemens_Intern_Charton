# Week 7 操作与复现指南

Week 7 正式目标为 `bootloader-week7.elf`，平台是 QEMU MPS2-AN500 Cortex-M7。代码与工具从项目 `codes/` 目录执行。原 Week 1 至 Week 6 目标保留；Week 7 默认需要签名镜像，沿用 Week 6 的 32 字节头与 272 字节签名尾。

## 构建与验收

环境需要 Python 3、CMake、make、Arm bare-metal GCC 与 GDB、qemu-system-arm、宿主 C 编译器和 OpenSSL。Unity、Mbed TLS 与 YMODEM 测试依赖已经包含在 tests/third_party 中；当前来源和版本以各子目录来源清单为准。

```sh
source scripts/env.sh
bash scripts/build-week7.sh
make verify-week7
make benchmark-week7
make debug-week7
```

`verify-week7` 默认创建独立的 `logs/week7-时间戳/build`，重新构建正式、调试、故障与目标机 Unity ELF，执行宿主和 QEMU 测试。`summary.json` 为 PASS 才算通过；任何构建错误、断言失败或超时都退出非零。最新目录由 `logs/week7-latest.txt` 指向。失败时先读 `failure.txt`、相应场景 `uart.log` 和 `qemu.log`，不要只看终端已打印的 PASS 行。

正式目标没有故障注入包装器。`bootloader-week7-fault.elf`、`unity-week7.elf` 和 `bootloader-week6-review-fault.elf` 仅供测试。`bootloader-week7-debug.elf` 保留 -Og 便于调试；正式固件使用 -Os，二者均保留符号。

## 已签名升级演示

以下示例使用新的 NOR 文件名。若同名文件已存在，seed 脚本会拒绝覆盖；确需重做时自行选择新的名字或显式加 --force。

```sh
python3 scripts/seed-week6.py \
  --slot-a build/week7/application-week7-v1.pkg \
  --output build/week7/demo-v1.bin
python3 scripts/send-week7.py \
  --elf build/week7/bootloader-week7.elf \
  --image build/week7/demo-v1.bin \
  --package build/week7/application-week7-v2.pkg \
  --evidence logs/week7-demo-v2
```

输出应含 `UPGRADE: COMMITTED`、`TRIAL: LAUNCH`、`APP: VERSION v2` 和 `APP: BOOT CONFIRMED`。输入 NOR 文件不被覆盖；运行后的显式 NOR 快照在 evidence 目录的 `nor.bin`。从该快照验证冷启动：

```sh
python3 scripts/send-week7.py \
  --image logs/week7-demo-v2/nor.bin \
  --evidence logs/week7-demo-restart
```

`send-week7.py` 在建立 Session 时已读取启动窗口标记，不会重复等待一次不会再出现的 banner。已有 `send-week6.py` 面向旧 banner，不能用于 Week 7。新的入口已完成真实上传与快照冷启动冒烟测试。

自有应用需遵守既有链接和应用确认合同。仍用 `pack-week6.py` 打包，因为包格式不变；不要更改版本字段后直接复用原签名。测试私钥位于 tests/third_party/mbedtls/keys，仅可用作本实验的可复现夹具，不可用于生产。

## 串口命令与配置

| 输入 | 行为 |
|---|---|
| `version` 或 `status` | 查询 Bootloader 版本、两槽签名和版本、活动槽及配置 |
| `boot` | 按已签名校验和回退策略启动 |
| `upgrade` | 开始 YMODEM 接收签名包 |
| `recover` | 显式恢复损坏的升级元数据，然后进入接收 |
| `p` 或 `param` | 查询参数 |
| `p window_ms 4000` | 持久化命令窗口，允许 200 至 60000 ms |
| `p auto_boot 0` | 关闭窗口后的自动启动；允许值 0 或 1 |
| `factory` 后紧接 `YES` | 重置元数据与配置，保留固件槽；任何其他命令撤销确认 |

完整命令以回车结束，建议整行发送。u/b/r/v/h 可单独发送，在 250 ms 无后续字节后执行，也可直接加回车。逐字人工输入以这些字母开头的完整命令时可能触发快捷键，宜用粘贴或行模式发送。输入最多 47 个可打印字符；超长整行拒绝，不执行截断内容。参数写入失败时不改变当前运行时值。配置损坏时只允许查询、帮助及显式出厂重置，不带着默认试运行状态启动。

## 故障测试与性能测量

`verify-week7.py` 中的 fault_id 与 `tests/week7/fault.c` 配对。1 至 7 覆盖部分擦除、数据写入、试运行正文/标记、激活正文/标记；10 至 15 覆盖擦除、编程、读回、活动槽读取、试运行配置、参数写入错误。选择字位于 NOR 最后 4 字节，仅测试 ELF 读取。中断点命中后停止 CPU 并保存快照，冷启动正式 ELF；并非真实 Flash 断电实验。

`benchmark-week7.py` 使用相同输入、固定 icount、TIMER0 和 GDB 断点比较三个目标，每种条件默认重复 3 次。窗口固定 200 ms，仅作为双方相同的实验参数，正式默认仍为 2500 ms。时间是仿真模型指标，不是板上最坏启动时间。详见 [优化报告](week7-optimization-report.md)。

`debug-week7.py` 在 update_activate 前后检查 ARMED 与活动槽顺序，GDB 脚本和原始输出随日志保存。连接使用 localhost 上的 QEMU gdbstub；实体 OpenOCD/SWD/JTAG 未执行。配置真实 OpenOCD 必须有确定的调试器、板卡及目标配置，本项目不提供无法在当前环境验证的虚构板级配置。

## 交付与边界

正式材料见 [Week 7 交付索引](../../reports/week7/delivery-index.md)。周报只在 reports/week7，技术材料在 codes/docs。重建产物和日志可由脚本再生成；ZIP 附源码、依赖、正式文档、最终 ELF 和本轮证据及 SHA256 清单。未做 Git 提交或推送，老师审核状态保持待确认。
