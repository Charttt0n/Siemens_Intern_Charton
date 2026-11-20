# Week 5 固件升级实习周报

## 本周完成情况

本周代码已完成单文件 YMODEM 接收、非活动槽写入、镜像完整性与向量验证、参数双页事务和活动槽切换。在 QEMU AN500 中，主机经 UART 将 v1 升级为 v2，再升级为 v3；重启后按持久参数选择新应用。传输失败和受控写入中断时，旧活动槽保持可启动；完整提交后重启选择新槽。

| 计划交付 | 本周成果 | 证据 |
|---|---|---|
| Ymodem 与串口升级源码 | ymodem.c、crc16.c、main.c、timebase.c | native 协议测试和第三方互操作；真实 UART 上传 |
| 主备管理与状态源码 | update.c、boot_meta.c；复用 Week3 Flash 和 Week4 镜像模块 | A/B 往返、参数读回、IO 和中断重启 |
| 成功失败中断测试报告 | week5-test-report.md/.pdf | 分场景验收，顶层分组全部通过 |
| 本周周报 | 本文及同目录 Word/PDF | 与原计划 Week5 范围对应 |

## 实现思路

升级最重要的约束是：新文件尚未完整校验和提交时，不改变旧活动槽，不擦除旧固件。两槽都是存储位置，实际执行地址统一为 0x00010000。活动槽由参数记录决定，Bootloader 从所选槽加载到该地址后交接；不会直接跳至 B 槽的存储地址。

代码分为协议、写入事务、参数事务和主流程四层。协议层只处理帧、顺序、CRC16 和传输终止；写入层处理镜像写入及 CRC32/向量验证；参数层保存可恢复的状态；主流程负责选择槽、接收命令和交接应用。

<!-- pagebreak -->

## 数据格式与提交规则

| 区域 | 存储偏移和大小 | 用途 |
|---|---|---|
| Boot | 0x00000，64 KiB | 只读逻辑预留区，运行 ELF 由 QEMU 装载 |
| A | 0x10000，448 KiB | 初始活动镜像；后续可作为升级目标 |
| B | 0x80000，448 KiB | 初始升级目标；提交后成为活动槽 |
| Params | 0xF0000，64 KiB | 首两页存事务记录，每页 4 KiB |

升级文件为 32 字节 M7IM 头加 payload，最大总长度 0x70000；最大 payload 为 0x6FFE0。当前 v1/v2/v3 的 payload 均为 1004 字节，包长 1036 字节，实际内容和输出版本不同。完整 1 MiB NOR 镜像和 ELF 都不能作为串口升级包发送。

状态顺序为 IDLE → RECEIVING → VERIFYING → READY → 新活动槽 IDLE。接受首包后，先写 RECEIVING，再擦除非活动槽。数据以 4 字节编程并读回，最后不足 4 字节用 0xFF 补齐；写入进度仅统计 payload。全文件校验通过后记 READY；激活时重新读取记录并校验候选，再提交 active_slot。

### 参数字段解释字典（meta_record，48 字节，小端）

说明：`magic` / `schema` / `generation` 为三个独立字段，不可混为一谈。

参数记录布局固定，定义见 `codes/src/week5/boot_meta.h`。两页各 4 KiB，记录区位于页首 48 字节；仅当 magic、schema、字段规则、CRC 与提交标记一致时视为有效记录。

| 偏移 | 大小 | 字段 | 取值 / 含义 |
|---|---|---|---|
| 0 | 4 | magic | 固定 `0x4D42354D`（ASCII `"M5BM"`） |
| 4 | 4 | schema | 固定 `1`（`META_SCHEMA`）；版本不匹配则整条拒绝 |
| 8 | 4 | generation | 单调序号，范围 1…UINT32_MAX，回绕到 1；用于比较两页新旧 |
| 12 | 4 | active_slot | 当前活动槽：`FLASH_APP`=A 或 `FLASH_BACKUP`=B |
| 16 | 4 | target_slot | 正在写入的目标槽；IDLE 时为 `META_SLOT_NONE`（`0xFFFFFFFF`） |
| 20 | 4 | state | `META_STATE_*`：0 IDLE，1 RECEIVING，2 VERIFYING，3 READY，4 FAILED |
| 24 | 4 | progress | 已验证写入的 payload 字节数，≤ expected_length |
| 28 | 4 | expected_length | 声明的 payload 长度；未知/IDLE 时为 0；合法范围 192…0x6FFE0 |
| 32 | 4 | candidate_version | 候选镜像版本号；无候选时为 0 |
| 36 | 4 | error | 仅 FAILED 时非零，语义对齐 `update_result`（见附录错误码） |
| 40 | 4 | crc | CRC-32（zlib 多项式），覆盖字节 0…39 |
| 44 | 4 | marker | 固定 `0x54494D43`（ASCII `"CMIT"`），最后写入；缺失则记录不可接受 |

读取时检查字段、CRC 和标记，只接受可排序的有效记录。两份均损坏或序号歧义时进入恢复入口，使用明确的 `r` 命令重建；不会在正常启动中猜测原活动槽。

## 接收和失败处理

支持 128/1024 字节帧、CRC16、包序号回绕、重复帧去重、取消、超时和有界重传。RX 环形队列总容量 128 字节，有效可排队 127 字节，独立于 1024 字节协议缓冲。软件丢包或硬件 overrun 会中止会话。

CRC16 只保护传输帧，CRC32 检查镜像头与完整 payload；二者均不认证固件来源。只有新数据或首包被接受才刷新无进展预算。成功终包 ACK 对应接收层完成，宿主仍需确认 COMMITTED、新版本输出和持久参数，不能仅凭 ACK 宣布升级成功。

<!-- pagebreak -->

## 测试结果（按功能 / 场景拆分）

2026 年 11 月 6 日重新从源码构建并执行 `verify-week5.py`。顶层检查分组全部通过。下表按功能与场景归类，不把各场景简单相加当作“总用例数”。完整逐项证据见 `codes/docs/week5-test-report.md` 与证据根目录 `codes/logs/week5-20261106-043520-600037`。

### A. 宿主单元与协议层

| 场景组 | 覆盖要点 | 结果 |
|---|---|---|
| 包格式 / 槽接口 | 合法头、CRC、向量、容量；拒绝 ELF/全 NOR；A/B 与坏镜像 | PASS |
| 参数事务 | 损坏、IO、正文/标记撕裂、序号回绕与歧义 | PASS |
| YMODEM 状态机 | 首包、帧 CRC、重复、取消、超时、sink 失败 | PASS |
| 写入链路 | RECEIVING→擦除→分片写入→READY/FAILED→激活 | PASS |
| CRC16 对照 | 10 组输入与第三方实现比对 | PASS |

### B. 第三方发送端互操作（9 场景）

| 场景 | 预期 | 结果 |
|---|---|---|
| interop-1024 / interop-128 | 收发内容一致 | PASS |
| interop-*-ack-loss（block0 / data / eot） | 丢 ACK 后仍一致 | PASS |
| interop-bad-crc / interop-wrap-128 | 纠错或回绕后一致 | PASS |
| interop-delayed-terminator | 终包延迟窗口正确 | PASS |
| interop-stalled-link | 有界失败退出 | PASS |

### C. 正常升级、选槽与容量

| 场景组 | 覆盖要点 | 结果 |
|---|---|---|
| v1→v2→v3 连续升级 | B 后 A 成为活动槽，输出版本正确 | PASS |
| 冷重启与旧槽保持 | 新进程仍跑新版本；旧槽逐字节不变 | PASS |
| 满槽升级 | 458752 字节包填满 B，冷重启跑 v2；A 不变 | PASS |
| 元数据选槽 / 空白 / 单份有效 | 按持久参数选槽；损坏不自动猜活动槽 | PASS |
| 显式恢复 `r` | 双份损坏后重建并可再升级 | PASS |

### D. 失败路径与旧槽保护

| 场景组 | 覆盖要点 | 结果 |
|---|---|---|
| 镜像拒绝 | bad-crc32 / bad-vector / bad-vector-tail-halfword / 截断 / 超长 / 超容量 | PASS，活动槽保持 A |
| 传输中止 | cancel、timeout、uart-disconnect、rx-loss、real-rx-overflow | PASS，有界退出 |
| Flash / 参数 IO | erase-io、program-io、activation-meta-io | PASS，不静默提交 |

本轮修复了 Reset 向量指向镜像最后一个字节时仍能提交的问题。修复后串口上报 `UPGRADE: FAIL`，参数保留 A 活动，并写入 FAILED 与镜像错误码。

### E. 受控中断与冷重启（9 快照）

| 中断位置 | 重启应用 | 快照 state / active | 结果 |
|---|---|---|---|
| target-erase / target-word 前缀 | v1 | RECEIVING / A | PASS |
| activation 正文/CRC/标记不完整 | v1 | READY / A | PASS |
| activation-marker-complete | v2 | IDLE / B | PASS |
| ready-not-activated / verifying 前缀 | v1 | READY 或 RECEIVING / A | PASS |

标记完整前提交前缀中断仍启动旧槽；完整 CMIT 后即使调用方读回未完，重启也选新槽。中断后支持完整重传，不支持断点续传。

### 实际输入输出

下列上传与读回命令按“复现”章节实际执行，输出未改写。UART 记录开头的 C 是接收端请求数据块的握手字节，每包一个，不是正文。

v2 上传的输入：

```sh
python3 scripts/send-week5.py --image build/week5/demo-nor.bin --package build/week5/application-v2.pkg --evidence logs/demo-v2
```

接收端原始 UART 记录（logs/demo-v2/uart.log）：

```text
BOOT5: command window
CCCCUPGRADE: READY
UPGRADE: COMMITTED
IMAGE: SLOT B CRC32 PASS
IMAGE: VERSION 0x00000002
LOAD: verified at 0x00010000
JUMP: application
APP: CRT PASS
APP: HANDOFF PASS
APP: VERSION v2
APP: SVC PASS
APP: RUNNING
>
```

不重新上传、只重启同一镜像，输入 `python3 scripts/send-week5.py --image build/week5/demo-nor.bin`，输出：

```text
IMAGE: SLOT B CRC32 PASS
IMAGE: VERSION 0x00000002
LOAD: verified at 0x00010000
JUMP: application
APP: CRT PASS
APP: HANDOFF PASS
APP: VERSION v2
APP: SVC PASS
APP: RUNNING
>
```

v3 上传后切回 A，输入 `python3 scripts/send-week5.py --image build/week5/demo-nor.bin --package build/week5/application-v3.pkg --evidence logs/demo-v3`，记录中为 `IMAGE: SLOT A`、`APP: VERSION v3`。参数读回：

```sh
python3 scripts/week5_meta.py build/week5/demo-nor.bin
```

```text
{"generation": 13, "active": 1, "target": 4294967295, "state": 0, "state_name": "IDLE", "progress": 0, "expected": 0, "version": 0, "error": 0}
```

拒绝路径取自本轮原始证据 bad-vector-tail-halfword。该用例在 v3 包尾加 1 字节，payload 为 1005 字节，用于构造入口落在末字节的情形：

```text
BOOT5: command window
CCCCUPGRADE: FAIL 0x00000006
IMAGE: SLOT A CRC32 PASS
IMAGE: VERSION 0x00000001
LOAD: verified at 0x00010000
JUMP: application
APP: CRT PASS
APP: HANDOFF PASS
APP: VERSION v1
APP: SVC PASS
APP: RUNNING
>
```

对应参数记录（metadata-inspect-failed.txt）：

```text
{"generation": 6, "active": 1, "target": 2, "state": 4, "state_name": "FAILED", "progress": 1005, "expected": 1005, "version": 0, "error": 4}
```

`0x00000006` 是接收层上报的写入失败（`YMODEM_SINK`），具体原因保存在参数 `error=4`（`UPDATE_IMAGE`，镜像校验失败）；活动槽仍为 A 并运行 v1。

## 问题与适用边界

Flash 为 SRAM 上的 NOR 规则模型。普通运行结束需显式 checkpoint；中断测试则在指定故障点锁存并暂停 CPU，导出当时完整 NOR，再用该快照启动全新正式固件进程。直接杀死 QEMU 后重新加载旧 seed 不等于升级中断恢复证据。本轮没有真实掉电或真实 Flash 试验。

参数事务每数据块可能擦写一页，真机移植需评估寿命、延迟和硬件写入原子性。系统时钟、UART 硬件时序、缓存/MPU/FPU 活动现场均受 AN500 当前配置限制。活动固件损坏后不自动搜索备份；自动运行故障回退属于 Week6。

## 复现与后续安排

从项目 codes 目录执行以下命令。首次 seed 建立 v1，后续必须经上传流程升级，避免用重新生成 seed 替代升级。

```sh
source scripts/env.sh
bash scripts/build-week5.sh
python3 scripts/seed-week5.py --slot-a build/week5/application-v1.pkg --output build/week5/demo-nor.bin
python3 scripts/send-week5.py --image build/week5/demo-nor.bin --package build/week5/application-v2.pkg --evidence logs/demo-v2
python3 scripts/send-week5.py --image build/week5/demo-nor.bin
python3 scripts/send-week5.py --image build/week5/demo-nor.bin --package build/week5/application-v3.pkg --evidence logs/demo-v3
python3 scripts/week5_meta.py build/week5/demo-nor.bin
make verify-week5
```

已有 seed 默认不覆盖；需要从头演示时先保留旧实验，再使用新的镜像文件名或明确的 `--force`。包中另附经过冒烟验证的固件副本，正式代码及可重复构建入口仍是主来源。

上述命令的实际输出见“实际输入输出”一节；不带 `--package` 的命令用于读取持久镜像并验证启动。

| 材料 | 位置 |
|---|---|
| 本周交付索引 | reports/week5/delivery-index.md |
| 技术测试报告 | codes/docs/week5-test-report.md/.pdf |
| 操作与接口说明 | codes/docs/week5-guide.md |
| 本轮 Week5 原始证据 | codes/logs/week5-20261106-043520-600037 |
| 本次复跑日志 | codes/logs/demo-v2、codes/logs/demo-v3 |
| 五周符合性与待补项 | planning/week1-5-conformance-2026-11-01.md |
| 统一交付入口 | planning/week1-5-delivery-index.md |

后续安排：按带教反馈修订本周材料；Week1 指定手册、S7 公开架构和工业要求适用性说明继续补齐，明确仿真替代方案的验收边界。老师确认后进入 Week6，推进认证、自动回退、看门狗和配置接口；后续功能不写成已实现，阶段验证不表述为工业认证或量产验证。

<!-- pagebreak -->

## 附录 A：错误码 namespace

下列枚举来自 Week5 头文件；串口 `UPGRADE: FAIL 0x…` 通常为 `ymodem_result`，参数记录中的 `error` 字段写入的是 `update_result`。读失败场景时需同时对照两层，不可混用同一数字表。

### A.1 `ymodem_result`（`ymodem.h`，接收层）

| 码 | 名称 | 定义 |
|---|---|---|
| 0 | YMODEM_OK | 单文件接收成功（含批终结） |
| 1 | YMODEM_IO | 读写回调失败或非法返回 |
| 2 | YMODEM_TIMEOUT | 无进展墙钟预算耗尽 |
| 3 | YMODEM_CANCELLED | 发送端 CAN CAN 中止 |
| 4 | YMODEM_PROTOCOL | 帧头 / 文件名 / 长度字段非法 |
| 5 | YMODEM_RETRY_LIMIT | 连续无进展事件超过上限 |
| 6 | YMODEM_SINK | begin/write/finish 失败（常见于镜像写入/校验失败） |
| 7 | YMODEM_TOO_LARGE | 声明长度超过 max_file_size |
| 8 | YMODEM_UNDERFLOW | EOT 时未收齐声明字节 |
| 9 | YMODEM_OVERFLOW | 声明长度之后仍有数据块 |
| 10 | YMODEM_MULTI_FILE | 终结 block0 又命名了另一文件 |
| 11 | YMODEM_ARGUMENT | 配置或回调非法 |

### A.2 `update_result`（`update.h`，写入 / 激活层；亦写入 meta `error`）

| 码 | 名称 | 定义 |
|---|---|---|
| 0 | UPDATE_OK | 成功 |
| 1 | UPDATE_IO | 存储读写失败 |
| 2 | UPDATE_ARGUMENT | 参数或 API 误用 |
| 3 | UPDATE_LENGTH | 长度不合法 |
| 4 | UPDATE_IMAGE | 镜像头 / CRC32 / 向量校验失败 |
| 5 | UPDATE_STATE | 事务状态不允许当前操作 |
| 6 | UPDATE_ERASE | 擦除失败 |
| 7 | UPDATE_WRITE | 编程失败 |
| 8 | UPDATE_META | 参数事务失败 |
| 9 | UPDATE_ABORTED | 会话中止 |

### A.3 `meta_result`（`boot_meta.h`，参数读写 API 返回值）

| 码 | 名称 | 定义 |
|---|---|---|
| 0 | META_OK | 记录已读或已提交/初始化 |
| 1 | META_IO | 存储读/编程/擦除失败；状态未知，须重读 |
| 2 | META_NO_RECORD | 两页均为空白，调用方可初始化 |
| 3 | META_RECOVERY_REQUIRED | 损坏且无有效记录，需显式恢复 |
| 4 | META_ARGUMENT | API 误用或提交字段非法 |
| 5 | META_VERIFY | 写后读回不匹配 |
| 6 | META_AMBIGUOUS | 两份有效记录无法排序 |
| 7 | META_CRC | 记录 CRC 不匹配 |
| 8 | META_FIELD | 字段取值非法 |
| 9 | META_MARKER_MISSING | 缺少 CMIT 提交标记 |

### A.4 `meta_state`（状态字段，非错误码）

| 码 | 名称 | 定义 |
|---|---|---|
| 0 | META_STATE_IDLE | 空闲 |
| 1 | META_STATE_RECEIVING | 正在接收/写入 |
| 2 | META_STATE_VERIFYING | 正在校验 |
| 3 | META_STATE_READY | 候选已校验，待激活 |
| 4 | META_STATE_FAILED | 失败；此时 `error` 非零 |
