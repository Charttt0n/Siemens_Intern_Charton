# Week 7 交付材料索引

2026-11-13 本机代码验收：`make verify-week7` 的 29 个验收分组全部通过，Unity 93 个用例全部通过。周报、系统测试报告和优化报告按该次原始摘要及同期性能、调试、回归摘要撰写。本阶段是 QEMU MPS2-AN500 仿真验收，不是真机、真实掉电或看门狗电气认证，也未做 IEC 62443 产品认证。规划书中的 GDB + OpenOCD，本轮实际使用 QEMU 官方 GDB remote stub，未连接真实 JTAG/SWD。测试密钥不能用于生产。老师审核待完成。远程仓库须经确认后再提交或推送。

| 原规划任务 | 实现与验收 |
|---|---|
| Unity 补全核心模块单元测试 | [tests/week7](../../codes/tests/week7)；`unity-all` 记录 7 个套件、93 个用例全部 PASS |
| QEMU 正常与异常集成测试 | [verify-week7.py](../../codes/scripts/verify-week7.py)；29 个分组全部 PASS，含签名、配置、存储故障、7 个中断冷启动、看门狗复位与未确认回退 |
| 调试与问题修复 | [src/week7](../../codes/src/week7)；[修复记录](../../codes/docs/week7-fix-record.md)；调试传输为 QEMU gdbstub |
| 代码体积与启动时间优化 | 正式目标 text+data 29132→27204 字节；128 KiB 验证加载路径中位仿真时间 289.0→165.0 ms；见优化报告 |

| 类别 | 入口 | 内容 |
|---|---|---|
| 周报 | [Markdown](week7-report.md)、[Word](Week7实习周报.docx)、[PDF](Week7实习周报.pdf) | 完成情况、29 项分组的测试输入、预期、实测输出与证据，以及回归、性能和适用范围 |
| 系统测试报告 | [Markdown](../../codes/docs/week7-test-report.md)、[Word](../../codes/docs/Week7系统测试报告.docx)、[PDF](../../codes/docs/Week7系统测试报告.pdf) | 环境、方法、Unity 计数、29 项摘要字段、调试与回归 |
| 体积与启动优化报告 | [Markdown](../../codes/docs/week7-optimization-report.md)、[Word](../../codes/docs/Week7代码体积与启动时间优化报告.docx)、[PDF](../../codes/docs/Week7代码体积与启动时间优化报告.pdf) | 对照 ELF 体积、三次重复的中位仿真时间、测量模型与限制 |
| 修复记录 | [week7-fix-record.md](../../codes/docs/week7-fix-record.md) | 本周修复说明；本地测试通过不等于老师确认 |
| 操作指南 | [week7-guide.md](../../codes/docs/week7-guide.md) | 构建、验收、签名升级演示、命令接口和限制 |
| 本轮验收摘要 | [summary.json](../../codes/logs/week7-20261113-204501-591995/summary.json) | 29/29 PASS，93 Unity 用例；20:45:01–20:48:20 Asia/Shanghai |
| 性能摘要 | [summary.json](../../codes/logs/week7-benchmark-20261113-204522/summary.json) | PASS；模型为 QEMU icount shift=3 |
| 调试摘要 | [summary.json](../../codes/logs/week7-debug-20261113-final/summary.json) | PASS；QEMU gdbstub，无实体 JTAG/OpenOCD |
| 旧缺陷复现 | [summary.json](../../codes/logs/week7-baseline-defects-20261113/summary.json) | REPRODUCED，修复前失败证据 |
| 前期回归 | [summary.json](../../codes/logs/week7-regression-20261113/summary.json) | Week1–6 与 feedback 全部 PASS |
| 开发拆解 | [week7-development-breakdown.md](../../planning/week7-development-breakdown.md) | 实现对照与验收边界，不替代正式周报 |

[Week 7 完整交付包](../../output/2026-11-20-week7-交付包.zip) 于 2026 年 11 月 20 日整理，测试日期以原始摘要为准。ZIP 内含源码、测试夹具、正式 ELF、Word 和 PDF、原始日志与 NOR 快照，以及逐文件 SHA256 清单。解压后使用 `python3 verify-package.py` 检查完整性。老师验收及实体硬件验证仍待完成。
