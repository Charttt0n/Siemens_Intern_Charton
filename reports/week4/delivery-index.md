# Week 4 交付材料索引

整理日期：2026-10-25。范围：原计划第4周，CRC32 镜像校验 应用加载 安全跳转及测试应用。实验记录保留原日期，状态为本地材料已整理、待老师审核。

| 类别 | 正式维护位置 | 本周用途 |
|---|---|---|
| 周报 | [Markdown](week4-report.md)；[Word](Week4实习周报.docx)；[PDF](Week4实习周报.pdf) | 本周思路、架构、详细测试、反馈与边界 |
| 本周代码 | [src/week4](../../codes/src/week4/) | 本周功能实现 |
| 已有依赖 | src/week3/串口/Flash/启动与src/week2/板级/应用UART依赖 | 支持构建，不计入本周新增成果 |
| 技术指南 | [week4-guide.md](../../codes/docs/week4-guide.md) | 模块与运行说明 |
| 阶段测试 | [week4-test-report.md](../../codes/docs/week4-test-report.md) | 编号、输入、预期、实际及日志路径 |
| 阶段证据 | [最新生产回归](../../codes/logs/week4-20261025-015917-068252/summary.json) | 27组检查PASS |
| 反馈状态 | [逐项记录](../../planning/teacher-feedback-2026-10-24.md) | 当前均待老师确认 |
| 计划对应与包清单 | [两周交付总索引](../../planning/week3-week4-delivery-index.md) | 原计划条目、共享依赖与证据分配 |

补充材料：week4-image-inputs.json、vector-boundary-cases.json、week34-review-closure.md、delivery-manifest.schema.json、schemas/examples与完整状态图。固件M7IM头与交付manifest不是同一格式。

独立交付包：output/2026-10-25-week4-交付材料.zip。包内codes为本周及必要依赖的可构建快照，主工程仍唯一维护在根codes/。包根README.md为阅读与运行入口，delivery-manifest.json记录文件长度和SHA256。
