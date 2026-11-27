# Week 6 交付材料索引

2026-11-10 本机代码验收：`make verify-week6` 的 11 个顶层检查全部通过。周报按该次摘要撰写。本阶段是 QEMU 仿真验收，不是真机掉电或看门狗认证，也未做 IEC 62443 产品认证。测试密钥不能用于生产。Week 6 默认拒绝无签名的 Week 5 包。老师审核待完成。远程仓库须经确认后再提交或推送。

| 原规划任务 | 实现与验收 |
|---|---|
| SHA256 + RSA-2048 验签（Mbed TLS） | [验签模块](../../codes/src/week6/secure_image.c)，PKCS#1 v1.5，公钥编入 Bootloader；宿主 sanitizer 与已签名启动/升级通过，无签名包被拒绝 |
| 故障安全自动回退 | [启动与回退](../../codes/src/week6/main.c)、[试运行确认](../../codes/src/week6/app_confirm.c)；活动槽损坏回退到备份槽，试运行未确认后回退到上一已确认槽 |
| QEMU 仿真看门狗 | [CMSDK 驱动](../../codes/src/week6/watchdog.c)，基址 `0x40008000`。本轮试运行回退采用复位前快照冷启动，因 `system_reset` 未保留 SRAM NOR |
| 串口配置接口 | 版本查询、启动参数读写、出厂重置；出厂重置只恢复参数，不擦 A/B 固件槽 |

| 类别 | 入口 | 内容 |
|---|---|---|
| 周报 | [Markdown](week6-report.md)、[Word](Week6实习周报.docx)、[PDF](Week6实习周报.pdf) | 完成情况、格式与字段字典、11 项检查分组、原始串口摘录、边界与三项既定约定 |
| 升级与安全源码 | [src/week6](../../codes/src/week6/) | 验签、配置/试运行、看门狗、写入路径、主流程与确认版应用 |
| 共享依赖 | [Week3](../../codes/src/week3/)、[Week4](../../codes/src/week4/)、[Week5](../../codes/src/week5/) | UART/Flash、镜像与交接、YMODEM 与升级元数据；Week 5 源文件本周未改 |
| 技术测试报告 | [Markdown](../../codes/docs/week6-test-report.md) | 逐项输入、预期、实际结果和日志位置 |
| 操作指南 | [week6-guide.md](../../codes/docs/week6-guide.md) | 构建、打包、发送、命令接口、验证入口和限制 |
| 本轮原始证据 | [summary.json](../../codes/logs/week6-20261110-205019-465762/summary.json) | 11/11 PASS；含 UART/QMP/NOR 快照 |
| 开发拆解 | [week6-development-breakdown.md](../../planning/week6-development-breakdown.md) | 实现对照与验收矩阵，不替代正式周报 |

本轮顶层检查分组全部通过（按功能分 A–E，详见周报）。故障恢复中的试运行用例如上所述，采用快照冷启动验证固件逻辑。三项已确认约定：RSA 继续 PKCS#1 v1.5，公钥编进 bootloader，出厂重置不擦 A/B 槽固件。
