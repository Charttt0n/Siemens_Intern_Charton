# Week 5 交付材料索引

2026-11-06 复验通过。原规划第 5 页的四项开发任务和交付材料均已覆盖，工时补充版要求的周报已同步更新。老师审核待完成。

| 原规划任务 | 实现与验收 |
|---|---|
| YMODEM 经 UART 接收 | [协议与主流程](../../codes/src/week5/)，128 和 1024 字节帧与独立主机发送端互操作 |
| 双槽升级与中断保护 | [写入与激活](../../codes/src/week5/update.c)，验证 A→B→A，9 例受控中断按提交边界选择旧槽或新槽 |
| 参数区记录进度、状态和错误 | [参数事务](../../codes/src/week5/boot_meta.c)和[快照查看命令](../../codes/scripts/week5_meta.py)，复验 FAILED 读回与歧义拒绝 |
| QEMU 完整升级流程 | [原始摘要](../../codes/logs/week5-20261106-043520-600037/summary.json)，含满槽 458752 字节上传、冷重启、失败拒绝和旧槽保护 |

| 类别 | 入口 | 内容 |
|---|---|---|
| 周报 | [Markdown](week5-report.md)、[Word](Week5实习周报.docx)、[PDF](Week5实习周报.pdf) | 定稿版（含字段字典、A–E 场景拆测、附录错误码）；交老师用此三件 |
| 升级源码 | [src/week5](../../codes/src/week5/) | YMODEM、CRC16、写入、参数、主流程、计时 |
| 共享依赖 | [Week3](../../codes/src/week3/)、[Week4](../../codes/src/week4/) | UART/Flash、镜像与交接 |
| 技术测试报告 | [Markdown](../../codes/docs/week5-test-report.md)、[PDF](../../codes/docs/week5-test-report.pdf) | 成功、拒绝、IO、中断快照和冷重启 |
| 操作指南 | [week5-guide.md](../../codes/docs/week5-guide.md) | 构建、上传、恢复、接口和限制 |
| 本轮回归 | [Week 4 主回归](../../codes/logs/week4-20261106-043516-053335/summary.json)、[共享向量边界](../../codes/logs/vector-bounds-20261106-043551/summary.json) | 27 组主回归，323 条宿主断言与 11 条 CPU/GDB 路径 |
| 交付固件 | [构建与冒烟结果](../../codes/logs/week5-delivery-20261106/summary.json) | 包内固件完成 v2 上传、冷重启和 v3 上传 |
| 历史五周核查 | [报告](../../planning/week1-5-conformance-2026-11-01.md) | 11 月 1 日的验收记录、Week 1 研究缺口和平台适配差异 |
| 完整交付入口 | [总索引](../../planning/week1-5-delivery-index.md) | 前五周材料及各版本交付包 |

本轮 Week 5 顶层检查分组全部通过（按功能/场景分 A–E，详见周报），含互操作、启动记录、负向记录和中断快照等。故障快照采用 QEMU 受控 NOR 写入前缀模型。真实硬件掉电、自动运行故障回退和签名属于后续范围。旧名 `Week5实习周报-revised.docx` 已并入正式 `Week5实习周报.docx`。
