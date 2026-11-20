# Week 6 安全与恢复代码操作指南

本周在不改动 Week 5 已验收行为的前提下，增加 SHA-256 + RSA-2048 验签（Mbed TLS 3.6.7）、活动槽失败后的自动回退、试运行确认、CMSDK 看门狗，以及完整串口配置接口。本文只说明如何复现和验证，不是正式周报。

## 包格式

Week 5 包仍是 `32 字节头 + payload`。Week 6 **扩展包**为：

```
32 字节 Week 5 头 + payload + 272 字节签名尾（magic M6SG、type 1、256 字节 PKCS#1 v1.5 签名）
```

签名覆盖 `SHA256(头 || payload)`。32 字节头的字段、CRC32 和向量规则不变。Week 6 默认拒绝无签名旧包，不会激活它们。测试私钥在 `tests/third_party/mbedtls/keys/`，**不可用于生产**。

## 构建与演示

在 `codes/` 目录：

```sh
source scripts/env.sh
bash scripts/build-week6.sh
python3 scripts/seed-week6.py --slot-a build/week6/application-week6-v1.pkg \
    --output build/week6/demo-nor.bin --force
python3 scripts/send-week6.py --image build/week6/demo-nor.bin \
    --elf build/week6/bootloader-week6.elf \
    --package build/week6/application-week6-v2.pkg --evidence logs/demo-week6-v2
```

成功时串口会出现 `UPGRADE: COMMITTED`、`TRIAL: LAUNCH`、`APP: VERSION v2`、`APP: BOOT CONFIRMED`。自有应用仍须链接到 `0x00010000`，再用：

```sh
python3 scripts/pack-week6.py --app application.bin --version 4 --output application.pkg
```

## 启动与配置接口

输出 `BOOT6: command window` 后进入命令窗口（默认 2500 ms，可用参数修改）。单字母 `u`/`b`/`r`/`v` 立即执行，其余命令以回车结束。

| 命令 | 行为 |
|---|---|
| `u` / `upgrade` | YMODEM 上传已签名包到非活动槽，校验并提交后进入试运行 |
| `b` / `boot` | 立即按回退策略启动 |
| `r` / `recover` | 显式重建损坏的 Week 5 元数据，然后上传 |
| `v` / `version` / `status` | 查询 Bootloader 版本、两槽版本与验签、活动槽、试运行、参数 |
| `p` / `param` | 列出参数；`p window_ms 4000`、`p auto_boot 0` 修改并持久保存 |
| `f` / `factory` 后跟 `YES` | 出厂重置元数据与启动参数，不擦除 A/B 固件槽 |

活动槽损坏或试运行未确认时，会自动选择另一合法已签名槽并输出 `ROLLBACK:`，而不是只留在入口。两槽都非法时留在配置接口。

试运行：新版本提交后启动看门狗（第一次超时约 2 s，CMSDK 需第二次超时才复位）。确认版应用会写入配置页并停止看门狗。故意不确认的 `application-week6-hang` 用于演示回退。

## 验证入口

```sh
make verify-week6
```

或 `python3 scripts/verify-week6.py`。每次在证据目录交叉编译，运行宿主 sanitizer、已签名启动与升级、拒绝无签名包、损坏活动槽回退、双槽非法留在入口、参数持久化、出厂重置，以及试运行失败回退。摘要为 `logs/week6-*/summary.json`，最新目录写入 `logs/week6-latest.txt`。`make verify-week5` 与前四周入口保持不变；`make verify-all` 仍只覆盖 Week 1–5 与反馈，以免改变既有验收范围。

## 边界

- 存储仍是 SRAM 上的 NOR 规则模型。若 QEMU `system_reset` 不保留 `0x20300000`，试运行回退用例会从跳转前快照冷启动以验证固件逻辑。这不是真机看门狗或掉电认证。
- 看门狗是 AN500 的 CMSDK APB 外设（`0x40008000`），由产品固件驱动，不是宿主杀进程。
- 未做 IEC 62443 产品认证，也未改 Week 5 源码与周报定稿。
