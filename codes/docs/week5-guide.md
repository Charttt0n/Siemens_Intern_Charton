# Week 5 升级代码操作指南

本周实现单文件 YMODEM 接收、非活动槽写入、CRC32/向量验证、参数双页事务、活动槽提交及按持久状态启动。应用 v1/v2/v3 的实际 payload 和串口版本输出不同。

## 构建与连续升级

在项目 `codes/` 目录执行：

```sh
source scripts/env.sh
bash scripts/build-week5.sh
python3 scripts/seed-week5.py --slot-a build/week5/application-v1.pkg --output build/week5/demo-nor.bin
python3 scripts/send-week5.py --image build/week5/demo-nor.bin --package build/week5/application-v2.pkg --evidence logs/demo-v2
python3 scripts/send-week5.py --image build/week5/demo-nor.bin
python3 scripts/send-week5.py --image build/week5/demo-nor.bin --package build/week5/application-v3.pkg --evidence logs/demo-v3
```

首次 seed 仅用于建立 v1 初始环境；后续升级必须使用串口发送入口。已有 seed 不默认覆盖，只有明确重新开始实验时才加 `--force`。主机脚本运行 QEMU 并连接 UNIX UART socket，不连接真实串口，不依赖 pyserial，也不需要在线下载依赖。

v2 成功时输出 `UPGRADE: READY`、`UPGRADE: COMMITTED`、`IMAGE: SLOT B`、`APP: VERSION v2`；v3 切回 A，并输出 `APP: VERSION v3`。不带 `--package` 时检查保存后的启动版本。`--packet-size 128` 可选择小帧；默认 1024 字节。

自有应用需要链接到执行地址 `0x00010000`，向量与 RAM 契约沿用 Week 4。打包 raw binary：

```sh
python3 scripts/pack-week5.py --app application.bin --version 4 --output application.pkg
```

升级包为 32 字节镜像头加 payload，最大文件长度 `0x70000`。发送入口使用打包后的 `.pkg` 文件。CRC16 校验协议帧，CRC32 验证镜像头和完整 payload。Reset 和其他必填向量的首个 2 字节 Thumb 半字必须完整落在 payload 内。

## 启动入口与恢复

正常固件启动时输出 `BOOT5: command window`，提供 1500 ms 等待窗口：

| 输入 | 行为 |
|---|---|
| `u` | 从可信参数记录选择另一槽，接收一个文件 |
| `b` | 立即校验并启动持久记录指定的活动槽 |
| 无请求 | 等待窗口结束后启动活动槽 |
| `r` | 明确重建损坏/歧义参数记录，然后上传 |

空白参数区首次初始化为 A 活动。当前镜像无效时保持恢复入口；参数有效时仍可用 `u` 重传。参数双份损坏或序号有歧义时不猜测原活动槽，使用：

```sh
python3 scripts/send-week5.py --image build/week5/demo-nor.bin --package build/week5/application-v2.pkg --recover
```

`--recover` 对应 `r`：仅在参数不可用时显式重建。其选择策略为合法 A 优先、否则合法 B、否则预留 A；重建参数本身不擦除任何固件槽，随后上传到另一槽。已有有效记录时保留其活动槽。元数据读取 IO 错误不触发猜测或重建。

当前活动槽坏镜像不会自动搜索另一槽；试运行确认、自动故障回退、看门狗、签名验证等属于后续阶段。

## 提交与错误语义

正常状态为 `IDLE → RECEIVING → VERIFYING → READY → 新活动槽 IDLE`。

- 接受首包后，先持久记录 RECEIVING，再擦除非活动槽。
- 按 4 字节写入并读回；最后不足 4 字节用 `0xFF` 补齐。进度不包括 32 字节头或协议填充，未写入的末尾暂存字节不计入进度。
- 全文件通过长度、头、CRC32、地址与向量验证后记录 READY。终包 ACK 表示文件已由 sink 接受，主机还应等待活动提交和新程序输出。
- `update_activate()` 重新读取 READY 及校验候选，最后以双页事务提交新的 active_slot。提交前重启选择旧槽，完整标记提交后选择新槽。
- 擦除、正文、CRC、标记和最终读回的任意 IO 错误都可能使调用者不知道最后一次提交是否已生效。启动路径总是重新读取持久状态，不根据旧上下文或返回码猜测活动槽。
- 传输失败保留原活动槽。无法写入 FAILED 时，最近有效记录可能仍是 RECEIVING/VERIFYING/READY；这些状态均保留原活动槽，不自动激活候选。

`UPGRADE: FAIL 0x...` 使用 `ymodem.h` 的错误码：1 IO/串口丢包，2 超时，3 取消，4 协议，5 重试上限，6 sink/存储/镜像验证失败，7 超长，8 过短，9 额外数据块，10 多文件。`COMMIT ERROR` 和参数记录中的 `error` 使用 `update.h` 的状态码。

对已保存的 NOR 镜像或故障快照，可直接查看活动槽、候选槽、状态、进度和错误码：

```sh
python3 scripts/week5_meta.py build/week5/demo-nor.bin
```

输出为 JSON。没有可用记录、两份记录顺序有歧义或镜像大小错误时，命令返回非零退出码。

RX 环形队列仍为 128 字节，实际可排队 127 字节；协议缓冲为独立的 1024 字节帧。出现软件丢包或硬件 overrun 即终止会话。协议 UART 写入使用 250 ms 单调计时上限，接收每字节 1000 ms、连续无进展最多 8 次重试、每次实际进展间隔最多 15000 ms。只有接受首包或新数据才刷新无进展预算，重复包和 EOT 不刷新。结束后为终包 ACK 丢失保留有界重传等待；故成功发送到应用启动之间存在数秒等待。

主机使用固定的第三方 ymodem 1.5.3 发送器。其等待部分 ACK 时不处理 CAN，适配层会在连续两个 CAN 到来时停止发送；完整二进制接收日志仍保留，避免丢失拒绝后的应用输出。依赖修订、校验和和许可证见 `tests/third_party/README.md`。

## 验证入口与故障模型

```sh
make verify-week5
make verify-all
```

`verify-week5.py` 每次从源码在临时目录交叉构建，运行 native ASan/UBSan、第三方发送端互操作、真实 UART 上传、A→B→A 连续升级、冷启动、拒绝/取消/超时/断连和受控写入中断测试。逐项结果、ELF 哈希、UART/QMP 日志和中断 NOR 快照位于 `logs/week5-<时间>/`，最新入口是 `logs/week5-latest.txt`。

三个 Bootloader 目标：

| 目标 | 用途 |
|---|---|
| `bootloader-week5.elf` | 正式本周行为，包含上传、提交与按参数启动 |
| `bootloader-week5-upgrade.elf` | READY 边界测试，成功上传后仍启动原活动槽 |
| `bootloader-week5-fault.elf` | 测试专用 Flash 操作包装器；不用于正常演示 |

故障固件根据最后一个参数页 `0xFF000` 的测试编号，模拟部分目标擦除/编程，以及活动提交时部分擦除、正文、CRC、标记和完整标记后中断；另覆盖 VERIFYING/READY 中断、IO 错误、丢包标志及真实 RX 环形队列溢出。故障点锁存后，宿主暂停 CPU、导出当前完整 NOR，使用该快照启动全新的正式固件进程。native 参数测试还逐字节枚举正文和标记撕裂前缀。

模拟 Flash 位于 SRAM。只有显式宿主 checkpoint/故障快照保存的状态会跨进程保留；直接杀死 QEMU 然后加载旧 seed 不能证明写入中断恢复。本周验证范围是 QEMU 仿真和受控故障模型，尚未进行真实 Flash 或硬件掉电试验。故障包装器仅链接进 fault 目标，正式固件不读取测试编号。

2026-11-01 补充：本周正式周报、Word/PDF、技术测试报告及本地交付包已编写，见 [Week5 交付索引](../../reports/week5/delivery-index.md) 与 [前五周总索引](../../planning/week1-5-delivery-index.md)。代码实现日期与本次复验日期分开记录，老师审核仍待完成。
