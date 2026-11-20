# Week 7 系统测试报告

## 结论与范围

2026 年 11 月 13 日 20:45:01–20:48:20（Asia/Shanghai）从源码重新构建 Week 7 正式、调试及故障测试目标，并执行 `make verify-week7`。最终摘要为 PASS：29 个验收分组全部通过，失败列表为空；Unity 7 个套件、93 个用例全部通过，0 失败、0 忽略。验收分组包含构建、Unity 汇总和正式 ELF 无故障钩子检查。用例数与分组数分别统计，不合并为独立覆盖率。

本报告覆盖 QEMU MPS2-AN500 Cortex-M7 仿真上的启动、签名升级、配置、试运行确认、存储故障、升级中断冷启动和看门狗观察。Flash 使用 SRAM NOR 规则模型。升级中断在命中注入点后导出 NOR 快照，再以正式 ELF 冷启动，不是真实非易失 Flash 或掉电认证。规划书中的 GDB + OpenOCD，本轮调试使用 QEMU 官方 GDB remote stub，未连接真实 JTAG/SWD，也未执行 OpenOCD 硬件链路。结论不外推为真机验收、工业可靠性或 IEC 62443 认证。本地通过不等于老师验收。测试密钥不可用于生产。

## 环境与证据索引

| 项目 | 记录 |
|---|---|
| 目标平台 | QEMU MPS2-AN500 仿真，摘要范围写为 Week 7 QEMU AN500 simulation |
| 正式目标 | `bootloader-week7.elf`；调试目标 `bootloader-week7-debug.elf`；故障注入只链接测试 ELF |
| 固件格式 | 沿用 Week 6：32 字节头 + payload + 272 字节签名尾；SHA-256 与 RSA-2048 PKCS#1 v1.5 |
| 单元测试 | Unity，7 个套件；宿主启用 ASan/UBSan；Cortex-M7 目标机套件在 QEMU 内运行 |
| 工具版本 | 以本轮证据目录中的 version 日志和第三方来源清单为准，不在此另行推定 |

本文 E 表示 `codes/logs/week7-20261113-204501-591995`。`E/summary.json` 含 29 个分组、Unity 套件计数、起止时间和源码 SHA256；`logs/week7-latest.txt` 指向该目录。G 表示 `codes/logs/week7-debug-20261113-final`；R 表示 `codes/logs/week7-regression-20261113`；D 表示 `codes/logs/week7-baseline-defects-20261113`。交叉复现入口见 `codes/docs/week7-guide.md`。

## 方法与通过条件

`verify-week7` 默认在新的 `logs/week7-时间戳/build` 下从源码构建正式、调试、故障与目标机 Unity ELF。任何构建错误、断言失败或超时都使验收退出非零；`summary.json` 为 PASS 才算通过。正式 ELF 须通过 `production-excludes-fault-hooks`，确认不含故障注入入口。

非法升级不得被当作成功提交；存储故障场景要求保持旧槽或运行时值。中断场景保存 `nor-at-fault.bin` 后冷启动正式 ELF。看门狗与未确认回退分开记录：先观察 QMP `RESET`，再用复位前 NOR 快照做冷启动回退。

## Unity 用例

| 套件 | 用例数 | 失败 | 忽略 |
|---|---|---|---|
| flash | 1 | 0 | 0 |
| image | 1 | 0 | 0 |
| boot_meta | 16 | 0 | 0 |
| ymodem | 32 | 0 | 0 |
| update | 9 | 0 | 0 |
| core | 23 | 0 | 0 |
| cortex-m7 | 11 | 0 | 0 |
| 合计 | 93 | 0 | 0 |

flash 与 image 为既有复合合同，各计 1 个用例，内部断言数不另计。宿主 82 个用例，目标机 11 个用例。套件原始输出在 `E/unity` 与 `E/unity-target.txt`。上表只报告本轮实际执行的用例计数，不声称穷尽全部输入或全部硬件配置。

## 验收分组（29 项）

下列名称、附加字段和结果均来自 `E/summary.json`，未改数字。

### 构建与单元测试

| 检查 | 摘要字段 | 结果 |
|---|---|---|
| fresh-build | — | PASS |
| unity-all | tests=93 | PASS |
| production-excludes-fault-hooks | — | PASS |

### 已签名启动与升级

| 检查 | 摘要字段 | 结果 |
|---|---|---|
| signed-boot | — | PASS |
| signed-upgrade-and-confirm | — | PASS |
| maximum-signed-upgrade | package_bytes=458752，payload_bytes=458448 | PASS |

458752 等于 32 + 458448 + 272。

### 拒绝非法包与回退

| 检查 | 结果 |
|---|---|
| unsigned-rejected | PASS |
| bad-signature-rejected | PASS |
| corrupt-payload-rejected | PASS |
| signed-header-tamper-rejected | PASS |
| corrupt-active-fallback | PASS |
| both-invalid-remain-cli | PASS |

### 配置接口与存储故障

| 检查 | 结果 |
|---|---|
| cli-validation-persistence-factory | PASS |
| corrupt-config-fails-closed | PASS |
| erase-error-keeps-old | PASS |
| program-error-keeps-old | PASS |
| readback-error-keeps-old | PASS |
| trial-arm-error-keeps-old | PASS |
| param-write-error-keeps-runtime | PASS |
| active-read-error-fallback | PASS |

### 升级中断冷启动

各条均带 `snapshot` 路径（相对对应场景目录的 `nor-at-fault.bin`）。`active_slot` 为摘要记录值。

| 检查 | fault_id | active_slot | 结果 |
|---|---|---|---|
| target-erase-tear-cold-restart | 1 | 1 | PASS |
| target-write-tear-cold-restart | 2 | 1 | PASS |
| trial-body-tear-cold-restart | 3 | 1 | PASS |
| trial-marker-complete-cold-restart | 4 | 1 | PASS |
| activation-body-tear-cold-restart | 5 | 1 | PASS |
| activation-marker-tear-cold-restart | 6 | 1 | PASS |
| activation-marker-complete-cold-restart | 7 | 2 | PASS |

这些结果验证所选中断点下，按快照冷启动后的恢复策略，不代表真实器件的全部掉电电气行为。

### 看门狗与未确认回退

| 检查 | 摘要记录 | 结果 |
|---|---|---|
| watchdog-reset-observed | 顶层 `watchdog_events` 记录到一条 QMP `RESET`，`guest=true`，`reason=guest-reset` | PASS |
| unconfirmed-trial-rollback | method 为 explicit pre-reset NOR snapshot and cold restart | PASS |

复位观察与回退逻辑是两条独立检查，不把显式快照重启写成同一进程内的自动非易失保持。

## 调试与旧缺陷复现

`make debug-week7` 摘要（G）状态为 PASS，传输写明 QEMU gdbstub（no physical JTAG/OpenOCD adapter）。检查项为：激活前已持久化试运行意图；提交前旧活动槽为 A；提交后新活动槽为 B；试运行应用已确认。

旧版本复现摘要（D）状态为 `REPRODUCED`，用途写明是修复前的失败证据，不是产品通过验收。记录包括：出厂重置缺少有效确认；活动槽已为 B 但无试运行意图；冷启动缺少试运行看门狗；解析函数接受溢出并得到 `705032704`（同时注明 CLI 范围检查仍会拒绝该值）。修复说明见 `codes/docs/week7-fix-record.md`。

## 前期回归

R/`summary.json` 全部 PASS。

| 范围 | 检查分组 | 证据目录 | 结果 |
|---|---|---|---|
| week1-review | 6 | week1-review-20261113-204103 | PASS |
| week2 | 12 | week2-20261113-204107 | PASS |
| week3 | 11 | week3-20261113-204111-730949 | PASS |
| week4 | 27 | week4-20261113-204113-949230 | PASS |
| week5 | 75 | week5-20261113-204118-612249 | PASS |
| week6 | 11 | week6-20261113-204103-462094 | PASS |
| feedback | 30 | feedback-20261113-204515 | PASS |

Week 6 历史脚本的试运行项仍按其原有 snapshot-restart 口径解释；本周新增的看门狗 RESET 是独立断言，未回写历史报告。

## 复现与限制

在 `codes/` 目录执行 `make verify-week7`。默认使用新的日志构建目录，不依赖旧 ELF。可选性能与调试入口为 `make benchmark-week7` 和 `make debug-week7`。具体命令、串口接口和排错见 `codes/docs/week7-guide.md`。

100% 通过仅指本次 93 个 Unity 用例及 29 个分组，不表示 100% 代码覆盖率、零缺陷、真机或工业认证。本轮未提交或推送 Git，老师审核待确认。
