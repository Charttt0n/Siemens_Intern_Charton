# Week 6 安全与恢复实习周报

## 本周完成情况

本周在不改动 Week 5 已验收源码与周报定稿的前提下，增加了 SHA-256 + RSA-2048 验签、活动槽失败后的自动回退、试运行确认、CMSDK 看门狗驱动，以及完整串口配置接口。2026 年 11 月 10 日 20:50–20:51（Asia/Shanghai）在本机执行 `make verify-week6`，`summary.json` 记录 11 个顶层检查全部 PASS。

本轮是 QEMU 仿真验收，不是真机掉电或看门狗认证，也未做 IEC 62443 产品认证。测试密钥不可用于生产。Week 6 默认拒绝无签名的 Week 5 包，不会激活它们。

| 计划交付 | 本周成果 | 证据 |
|---|---|---|
| 安全固件验证模块源码 | `secure_image.c`，精简 Mbed TLS 3.6.7；扩展包为 32 字节头 + payload + 272 字节签名尾 | 宿主 sanitizer 与已签名启动/升级 |
| 故障安全恢复与看门狗源码 | `boot_config.c`、`app_confirm.c`、`watchdog.c`；产品固件驱动 AN500 CMSDK 看门狗（`0x40008000`） | 活动槽损坏回退、试运行挂死回退 |
| 串口配置接口 | 命令窗口提供版本查询、启动参数读写、出厂重置，并保留升级/启动/恢复 | 参数跨重启保持；出厂重置后仍能启动原已签名固件 |
| 安全与可靠性测试报告 | 本文及同目录 Word/PDF，另附 `codes/docs/week6-test-report.md` | 证据根目录见下 |

## 实现思路

验签、回退、看门狗和配置接口分层实现。Week 5 的 32 字节镜像头、CRC32、向量规则和 `M5BM` 升级元数据布局保持不变。签名尾写在 payload 之后、同一固件槽内；配置与试运行记录使用参数区第 2、3 页（`M6CF`），不改第 0、1 页的升级事务格式。

启动时先校验活动槽的完整性与签名。若试运行已启动但尚未被应用确认，或活动槽损坏、校验失败、签名失败，则自动选择另一合法已签名槽并提交新的 `active_slot`，而不是只留在入口。两槽都非法时留在配置接口。新升级提交成功后进入试运行：启动看门狗（第一次超时约 2 s；CMSDK 需第二次超时才复位），应用须在超时前写入确认记录并停止该次看门狗。

当前已确认并冻结的三项实现约定：RSA 填充为 PKCS#1 v1.5；验签公钥编译进 Bootloader 只读段；出厂重置只恢复元数据与启动参数，不擦除 A/B 槽固件。若改为 RSA-PSS、独立密钥区或同时清空应用槽，需要改协议或另行实现，本周不改。

<!-- pagebreak -->

## 数据格式与提交规则

Week 5 包仍是 `32 字节头 + payload`。Week 6 **扩展包**为：

```
32 字节 Week 5 头 + payload + 272 字节签名尾
```

文件长度必须恰好等于 `32 + payload + 272`。签名覆盖 `SHA256(头 || payload)`。头字段、CRC32 和向量规则与 Week 5 一致。槽内容量上限变为 `0x70000 - 272`。本轮验收中，已签名 v1/v2/v3 包各 3468 字节，故意不确认的 hang 包为 1240 字节。

### 签名尾（272 字节，小端）

| 偏移 | 大小 | 字段 | 取值 / 含义 |
|---|---|---|---|
| 0 | 4 | magic | 固定 `0x4753364D`（ASCII `"M6SG"`） |
| 4 | 4 | type | `1` = RSA-2048 + SHA-256 + PKCS#1 v1.5 |
| 8 | 4 | sig_len | 256 |
| 12 | 4 | reserved | 0 |
| 16 | 256 | signature | 对 `SHA256(头 \|\| payload)` 的 PKCS#1 v1.5 签名 |

密码库为官方 `mbedtls-3.6.7.tar.bz2`（SHA256 `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`），许可证 Apache-2.0 或 GPL-2.0-or-later。固件只编译 SHA256、RSA 验签所需模块，不链接 TLS/X.509/PSA 完整栈。测试私钥在 `codes/tests/third_party/mbedtls/keys/`，与编入 `test_public_key.h` 的测试公钥配对，**不可用于生产**。

### 配置记录字段字典（cfg_record，56 字节，小端）

参数区第 2、3 页各 4 KiB，页内偏移相对于 `FLASH_PARAMS` 为 `0x2000` 与 `0x3000`。定义见 `codes/src/week6/boot_config.h`。

| 偏移 | 大小 | 字段 | 取值 / 含义 |
|---|---|---|---|
| 0 | 4 | magic | 固定 `0x4643364D`（ASCII `"M6CF"`） |
| 4 | 4 | schema | 固定 `1` |
| 8 | 4 | generation | 单调序号，用于比较两页新旧 |
| 12 | 4 | window_ms | 命令窗口，默认 2500，范围 200…60000 |
| 16 | 4 | auto_boot | `1` 窗口结束后按回退策略启动；`0` 留在配置入口 |
| 20 | 4 | trial_slot | 正在试运行的槽；无试运行时为无效槽号 |
| 24 | 4 | last_confirmed_slot | 上一份已确认的合法槽，供试运行失败回退 |
| 28 | 4 | trial_pending | `1` 表示存在未完成的试运行 |
| 32 | 4 | trial_stage | `0` 无，`1` 已武装，`2` 已启动应用 |
| 36 | 12 | reserved | 保留 |
| 48 | 4 | crc | 覆盖正文的 CRC-32 |
| 52 | 4 | marker | 固定 `0x54494D43`（ASCII `"CMIT"`），最后写入 |

## 接收、回退与配置接口

输出 `BOOT6: command window` 后进入命令窗口。单字母 `u`/`b`/`r`/`v` 立即执行，其余命令以回车结束。

| 命令 | 行为 |
|---|---|
| `u` / `upgrade` | YMODEM 上传已签名包到非活动槽，校验并提交后进入试运行 |
| `b` / `boot` | 立即按回退策略启动 |
| `r` / `recover` | 显式重建损坏的 Week 5 元数据，然后可再上传 |
| `v` / `version` / `status` | 查询 Bootloader 版本、两槽版本与验签、活动槽、试运行、参数 |
| `p` / `param` | 列出参数；`p window_ms 4000`、`p auto_boot 0` 修改并持久保存 |
| `f` / `factory` 后跟 `YES` | 出厂重置元数据与启动参数，不擦除 A/B 固件槽 |

本轮 Bootloader 版本字符串为 `week6-1`。正式目标 `bootloader-week6.elf` 的 `arm-none-eabi-size` 为 text 29124、data 8、bss 41176；text+data = 29132 字节，未超出 64 KiB 代码区。交叉编译器为 GNU Arm GCC 16.2.0（见本轮 cmake 配置记录）。


## 测试结果（按功能 / 场景拆分）

2026 年 11 月 10 日从源码重新构建并执行 `make verify-week6`（即 `python3 scripts/verify-week6.py`）。顶层检查 11 项全部通过，`failed` 为空。下表按功能与场景归类，不把各场景简单相加当作“总用例数”。逐项证据见 `codes/docs/week6-test-report.md` 与证据根目录 `codes/logs/week6-20261110-205019-465762`。

### A. 构建与宿主验签

| 检查 | 覆盖要点 | 结果 |
|---|---|---|
| build-and-pack | 重新交叉编译 Bootloader 与 v1/v2/v3/hang 应用，打包已签名包；text+data 未越界 | PASS |
| host-parse-unsigned-rejected | 离线解析拒绝无签名包 | PASS |
| native-sanitizer-verify | 宿主 ASan/UBSan：合法签名通过，坏签名与翻转 payload 均拒绝 | PASS |

### B. 已签名启动与升级

| 检查 | 覆盖要点 | 结果 |
|---|---|---|
| boot-signed-v1 | 种子已签名 v1，窗口后启动并出现 `APP: BOOT CONFIRMED` | PASS |
| upgrade-signed-v2 | UART 上传已签名 v2，出现 `UPGRADE: COMMITTED`、`TRIAL: LAUNCH`、`APP: VERSION v2` 与确认 | PASS |

### C. 拒绝无签名包与回退

| 检查 | 覆盖要点 | 结果 |
|---|---|---|
| reject-unsigned | 在已运行的已签名 v2 上上传无签名 v3 包，报告 `UPGRADE: FAIL 0x00000006`，仍运行 v2 | PASS |
| rollback-corrupt-active | 损坏活动槽 A 后自动 `ROLLBACK: SELECT B`，启动已签名 v2 | PASS |
| both-invalid-stay-cli | 两槽 payload 均损坏，查询显示 `INVALID`，不进入应用 | PASS |

`0x00000006` 是接收层 `YMODEM_SINK`（与 Week 5 相同）；活动槽保持已签名 v2。损坏回退日志中的 `ACTIVE INVALID 0x00000002` 对应 `SECURE_IMAGE`。

### D. 配置接口与出厂重置

| 检查 | 覆盖要点 | 结果 |
|---|---|---|
| status-and-param-persist | `v` 查询到 `BOOT6: BL week6-1` 与 `SIG OK`；`p window_ms 4000` 保存后，冷启动读回 `window_ms=04000` | PASS |
| factory-reset-keeps-firmware | 出厂重置输出 `FACTORY: DONE` 后，已签名 v1 仍可启动并确认 | PASS |

### E. 试运行挂死回退

| 检查 | 覆盖要点 | 结果 |
|---|---|---|
| trial-hang-rollback | 先确认 v1，再提交故意不确认的 hang 应用（日志为 `APP: VERSION vhang`，无 `BOOT CONFIRMED`）；随后回退到 A 槽 v1 | PASS；方法为 `snapshot-restart` |

摘要明确写道：本次 QEMU `system_reset` 未能在进程内保留 SRAM NOR 模型，固件回退是用复位前快照冷启动验证的。这证明所建模的试运行未确认回退路径，不能称为真机看门狗或真实掉电认证。

## 实际输入输出

下列记录取自本轮证据目录中的原始 `uart.log`。升级会话在命令窗口后会出现握手字节 `C` 以及 ACK（0x06）或 CAN（0x18）等协议字节；下文引用时省略这些字节，只保留可读日志。`UPGRADE: COMMITTED` 之后、镜像校验输出之前，原始记录中还有两行 `0x00000000` 与 `0x00000004`，此处按原文保留。

已签名 v1 启动（`boot-v1/uart.log`）：

```text
BOOT6: command window
IMAGE: SLOT A SIG PASS
IMAGE: VERSION 0x00000001
LOAD: verified at 0x00010000
JUMP: application
APP: CRT PASS
APP: HANDOFF PASS
APP: VERSION v1
APP: BOOT CONFIRMED
APP: SVC PASS
APP: RUNNING
>
```

已签名 v2 升级（`upgrade-v2/uart.log`）：

```text
BOOT6: command window
UPGRADE: READY
UPGRADE: COMMITTED
0x00000000
0x00000004
IMAGE: SLOT B SIG PASS
IMAGE: VERSION 0x00000002
LOAD: verified at 0x00010000
TRIAL: LAUNCH
JUMP: application
APP: CRT PASS
APP: HANDOFF PASS
APP: VERSION v2
APP: BOOT CONFIRMED
APP: SVC PASS
APP: RUNNING
>
```

拒绝无签名包（`reject-unsigned/uart.log`）在同一已提交的 v2 镜像上上传无签名 v3 后为 `UPGRADE: FAIL 0x00000006`，随后仍是 `IMAGE: SLOT B SIG PASS`、`APP: VERSION v2` 与 `APP: BOOT CONFIRMED`。

活动槽损坏回退（`rollback-corrupt/uart.log`）：

```text
BOOT6: command window
ROLLBACK: ACTIVE INVALID 0x00000002
ROLLBACK: ACTIVE INVALID
ROLLBACK: SELECT B
ROLLBACK: COMMITTED
IMAGE: SLOT B SIG PASS
IMAGE: VERSION 0x00000002
LOAD: verified at 0x00010000
JUMP: application
APP: CRT PASS
APP: HANDOFF PASS
APP: VERSION v2
APP: BOOT CONFIRMED
APP: SVC PASS
APP: RUNNING
>
```

参数持久化：`status/uart.log` 在查询后出现 `BOOT6: BL week6-1`、`SLOT A: VER 0x00000001`、`SIG OK`、`PARAM window_ms=02500 auto_boot=1` 与 `CONFIG: SAVED`；用该快照冷启动后，`status-restart/uart.log` 读回 `PARAM window_ms=04000`。出厂重置（`factory/uart.log`）为 `FACTORY: send YES to confirm` 与 `FACTORY: DONE`；随后 `factory-boot/uart.log` 仍启动已签名 v1 并出现 `APP: BOOT CONFIRMED`。

试运行未确认后的冷启动回退（`trial-hang-cold/uart.log`）：

```text
BOOT6: command window
ROLLBACK: TRIAL UNCONFIRMED
ROLLBACK: SELECT A
ROLLBACK: COMMITTED
IMAGE: SLOT A SIG PASS
IMAGE: VERSION 0x00000001
LOAD: verified at 0x00010000
JUMP: application
APP: CRT PASS
APP: HANDOFF PASS
APP: VERSION v1
APP: BOOT CONFIRMED
APP: SVC PASS
APP: RUNNING
>
```

## 问题与适用边界

存储仍是 SRAM 上的 NOR 规则模型。看门狗是 AN500 已建模的 CMSDK APB 外设，由产品固件驱动，不是宿主杀进程。若 QEMU `system_reset` 不保留 `0x20300000`，试运行回退用例会从跳转前快照冷启动以验证固件逻辑。本轮 `trial-hang-rollback` 正是这种情况。这不是真机看门狗认证，也不是真实掉电认证。

未做 IEC 62443 产品认证。不得把“与规划书中的 IEC 62443 要求对齐并实现验签、回退和看门狗”写成“已通过工业认证”。测试密钥与编入 Bootloader 的测试公钥只用于实验室，不能作为生产信任根。

Week 6 默认拒绝无签名旧包，不提供运行时关闭验签的开关。Week 5 Bootloader 仍会因“文件长度必须等于 32 + payload”拒绝 Week 6 扩展包，这是预期，本周未改 Week 5 行为。`make verify-all` 仍只覆盖 Week 1–5 与反馈，以免改变既有验收范围。

出厂重置不擦除 A/B 固件；公钥不在安全元件中；填充方案不是 RSA-PSS。上述三项按已确认口径保持现状。

## 复现与后续安排

从项目 `codes/` 目录执行。演示升级前先保留旧实验，或使用新的镜像文件名 / `--force`。

```sh
source scripts/env.sh
bash scripts/build-week6.sh
python3 scripts/seed-week6.py --slot-a build/week6/application-week6-v1.pkg \
    --output build/week6/demo-nor.bin --force
python3 scripts/send-week6.py --image build/week6/demo-nor.bin \
    --elf build/week6/bootloader-week6.elf \
    --package build/week6/application-week6-v2.pkg --evidence logs/demo-week6-v2
make verify-week6
```

成功演示时串口会出现 `UPGRADE: COMMITTED`、`TRIAL: LAUNCH`、`APP: VERSION v2`、`APP: BOOT CONFIRMED`。自有应用仍须链接到 `0x00010000`，再用 `python3 scripts/pack-week6.py --app application.bin --version 4 --output application.pkg` 打包。摘要为 `logs/week6-*/summary.json`，最新目录写入 `logs/week6-latest.txt`。本轮最新目录即 `codes/logs/week6-20261110-205019-465762`。

| 材料 | 位置 |
|---|---|
| 本周交付索引 | reports/week6/delivery-index.md |
| 技术测试报告 | codes/docs/week6-test-report.md |
| 操作与接口说明 | codes/docs/week6-guide.md |
| 本轮原始证据 | codes/logs/week6-20261110-205019-465762 |
| 开发拆解（不预写成功结论） | planning/week6-development-breakdown.md |

后续安排：按带教反馈修订本周材料。老师确认前不把本阶段写成工业认证或量产验证；不擅自将 Week 6 提交或推送到远程仓库。Unity 全面测试、体积与启动时间优化、Doxygen 与最终演示属后续周次。


## 附录 A：本周新增结果码

Week 5 的 `ymodem_result` / `update_result` / `meta_result` 仍然适用。串口 `UPGRADE: FAIL 0x00000006` 仍是接收层 `YMODEM_SINK`。下面只列出 Week 6 新增码，避免与旧表混用。

### A.1 `secure_result`（`secure_image.h`，验签层）

| 码 | 名称 | 定义 |
|---|---|---|
| 0 | SECURE_OK | 槽校验与签名均通过 |
| 1 | SECURE_IO | 存储读写失败 |
| 2 | SECURE_IMAGE | Week 5 镜像校验失败（本轮损坏回退见到 `0x00000002`） |
| 10 | SECURE_TRAILER | 签名尾字段不合法 |
| 11 | SECURE_UNSIGNED | 无签名尾，默认拒绝 |
| 12 | SECURE_TYPE | 签名类型不受支持 |
| 13 | SECURE_HASH | SHA-256 计算失败 |
| 14 | SECURE_SIGNATURE | 签名值不匹配 |
| 15 | SECURE_KEY | 公钥不可用 |
| 16 | SECURE_INIT | Mbed TLS 缓冲分配或初始化失败 |

### A.2 `cfg_result`（`boot_config.h`，配置 / 试运行记录）

| 码 | 名称 | 定义 |
|---|---|---|
| 0 | CFG_OK | 记录已读或已提交 |
| 1 | CFG_IO | 存储读写失败 |
| 2 | CFG_NO_RECORD | 两页均无有效记录 |
| 4 | CFG_ARGUMENT | API 误用或字段非法 |
| 5 | CFG_VERIFY | 写后读回不匹配 |
| 6 | CFG_AMBIGUOUS | 两份有效记录无法排序 |
