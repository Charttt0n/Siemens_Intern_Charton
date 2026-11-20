# Week 6 安全与恢复测试报告

2026 年 11 月 10 日 20:50:19 至 20:51:12（Asia/Shanghai）在本机项目目录执行 `make verify-week6`，即 `python3 scripts/verify-week6.py`。测试在证据目录内重新交叉编译正式 Week 6 目标，覆盖已签名启动与升级、拒绝无签名包、活动槽损坏回退、双槽非法留在入口、参数持久化、出厂重置，以及试运行未确认回退。宿主 C 测试使用 ASan/UBSan。

本报告只记录本次 QEMU 仿真结果，不是真机掉电或看门狗认证，也不是 IEC 62443 产品认证。

## 结果与环境

`summary.json` 中 `status` 为 `PASS`，`scope` 为 `week6-code-complete`，`failed` 为空，顶层检查 11 项全部通过。这些检查按功能分组列出，不把分组相加当作“总用例数”。

平台为 QEMU MPS2-AN500 Cortex-M7。交叉编译器为 GNU Arm GCC 16.2.0（见本轮 `cmake-week6.txt`）。看门狗使用已建模的 CMSDK APB 外设，基址 `0x40008000`，时钟 25 MHz，由产品固件驱动，不是宿主杀进程。存储为 1 MiB SRAM NOR 规则模型。

| 项目 | 本轮值 | 含义 |
|---|---|---|
| 执行地址 | 0x00010000 | A/B 的 payload 均链接到同一位置 |
| 镜像格式 | 32 字节头 + payload + 272 字节签名尾 | 文件长度必须恰好等于三者之和 |
| 本轮已签名包 | v1/v2/v3 各 3468 字节；hang 1240 字节 | 见证据目录中的 `.pkg` |
| 正式 Bootloader | text 29124，data 8，bss 41176 字节 | text+data = 29132，未超出 64 KiB 代码区 |
| Bootloader 版本串 | `week6-1` | `v` / `status` 查询可见 |
| 存储模型 | 1 MiB SRAM NOR | 升级与配置仍用显式快照保存 |

本轮证据根目录（下文记为 E）：

`codes/logs/week6-20261110-205019-465762`

`E/summary.json` 保存逐项名称、试运行回退方法说明和起止时间。各场景子目录保存原始 `uart.log`、`qmp.json` 与 `qemu.log`；NOR 快照为同目录下的 `after-*.bin` 等文件。

<!-- pagebreak -->

## 构建与宿主检查

| 检查 | 输入和断言 | 结果与 E 下证据 |
|---|---|---|
| build-and-pack | cmake 后构建 `bootloader-week6.elf` 与 application-week6-v1/v2/v3/hang，打包已签名包；text+data ≤ 64 KiB | PASS；`cmake-week6.txt`、`build-week6.txt`、`bootloader-size.txt`，summary 记录 text=29124、data=8 |
| host-parse-unsigned-rejected | 离线解析器接受已签名包，拒绝无签名 Week 5 格式包 | PASS |
| native-sanitizer-verify | 宿主可执行文件 `test_secure_image` 在 ASan/UBSan 下运行 | PASS；`run-host-secure.txt` 与 `run-host-unsigned.txt` 均为 `PASS signed-ok` / `PASS bad-rejected` / `PASS payload-flip` |

## QEMU 已签名启动、升级与拒绝

| 检查 | 输入和断言 | 结果与 E 下证据 |
|---|---|---|
| boot-signed-v1 | 仅在 A 槽放置已签名 v1，启动日志须含 `APP: VERSION v1` 与 `APP: BOOT CONFIRMED` | PASS；`boot-v1/uart.log`，快照 `after-v1.bin` |
| upgrade-signed-v2 | 在上一快照上经 `u` 上传已签名 v2，须出现 `UPGRADE: COMMITTED`、`APP: VERSION v2` 与确认 | PASS；`upgrade-v2/uart.log`，并含 `TRIAL: LAUNCH` |
| reject-unsigned | 在已提交的 v2 镜像上上传无签名 v3 包，不得出现 `UPGRADE: COMMITTED` 或 `APP: VERSION v3`，须仍运行 v2 | PASS；`reject-unsigned/uart.log` 为 `UPGRADE: FAIL 0x00000006`，随后 `APP: VERSION v2` |

`0x00000006` 为 `YMODEM_SINK`。原始升级 UART 在可读日志前含握手字节 `C` 与 ACK/CAN；`COMMITTED` 之后还有两行 `0x00000000` 与 `0x00000004`，引用时按原文保留。

## 回退、配置与出厂重置

| 检查 | 输入和断言 | 结果与 E 下证据 |
|---|---|---|
| rollback-corrupt-active | A 放 v1、B 放 v2 后，翻转 A 槽 payload 的一个字节；启动须出现 `ROLLBACK:` 并运行 v2 | PASS；`rollback-corrupt/uart.log` 为 `ACTIVE INVALID 0x00000002`（`SECURE_IMAGE`），随后 `ROLLBACK: SELECT B` 与 `APP: VERSION v2` |
| both-invalid-stay-cli | 同时损坏 A、B payload，在命令窗口查询 `v`，须见到 `INVALID` 且不得出现 `APP: RUNNING` | PASS；`both-invalid/uart.log` 含 `SLOT A: INVALID 0x00000002`、`SLOT B: INVALID 0x00000002`。该会话读到 `PARAM window_ms=` 即结束，日志在此处截断 |
| status-and-param-persist | 查询须含 `BOOT6: BL week6-1`；执行 `p window_ms 4000` 后用快照冷启动，读回须含 `4000` 或 `04000` | PASS；`status/uart.log` 有 `SIG OK` 与 `CONFIG: SAVED`；`status-restart/uart.log` 为 `PARAM window_ms=04000` |
| factory-reset-keeps-firmware | 在已修改参数的快照上执行 `factory` 与 `YES`，随后冷启动仍须能启动已签名应用 | PASS；`factory/uart.log` 为 `FACTORY: DONE`；`factory-boot/uart.log` 仍为 `APP: VERSION v1` 与 `APP: BOOT CONFIRMED`，说明未擦除固件槽 |

## 试运行挂死回退

先在仅有已签名 v1 的镜像上启动并确认，使 `last_confirmed_slot` 指向 A（`trial-prep/uart.log`）。再上传故意不确认的 hang 应用。`trial-hang/uart.log` 显示 `UPGRADE: COMMITTED`、`TRIAL: LAUNCH`、`APP: VERSION vhang` 与 `APP: RUNNING`，**没有** `APP: BOOT CONFIRMED`。

脚本随后等待进程内看门狗复位。本轮 QEMU `system_reset` 未能在同一进程内保留 SRAM NOR，因此改用跳转前快照 `trial-pre-reset.bin` 冷启动。`trial-hang-cold/uart.log` 出现 `ROLLBACK: TRIAL UNCONFIRMED`、`ROLLBACK: SELECT A`、`APP: VERSION v1` 与 `APP: BOOT CONFIRMED`。`summary.json` 将该检查记录为：

```text
"name": "trial-hang-rollback",
"method": "snapshot-restart",
"note": "QEMU system_reset did not keep the SRAM NOR model in-process; firmware rollback was verified by restarting from a pre-reset snapshot."
```

该项判定为 PASS，但适用范围仅限于所建模的复位/回退路径，不能写成真机看门狗已通过，也不能写成真实掉电认证已完成。

## 适用边界与复现

- 测试密钥位于 `codes/tests/third_party/mbedtls/keys/`，与 `codes/src/week6/test_public_key.h` 配对，不可用于生产。
- Week 6 默认拒绝无签名 Week 5 包，不提供关闭验签的运行时开关。
- 未修改 Week 5 源码；`make verify-week5` 入口保持不变。`make verify-all` 仍只覆盖 Week 1–5 与反馈。
- RSA 目前为 PKCS#1 v1.5，公钥编进 Bootloader，出厂重置不擦 A/B 槽。这三项按已确认口径保持现状。

在 `codes/` 目录复现本轮入口：

```sh
source scripts/env.sh
make verify-week6
```

摘要写入 `logs/week6-*/summary.json`，最新目录名写入 `logs/week6-latest.txt`。操作步骤另见 `codes/docs/week6-guide.md`。正式周报见 `reports/week6/week6-report.md`。
