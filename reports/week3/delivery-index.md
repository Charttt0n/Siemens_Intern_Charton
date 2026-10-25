# Week 3 交付材料索引

整理日期：2026-10-25。范围：原计划第3周，UART收发与中断 Flash抽象 四区布局及检查点。实验记录保留原日期，状态为本地材料已整理、待老师审核。

| 类别 | 正式维护位置 | 本周用途 |
|---|---|---|
| 周报 | [Markdown](week3-report.md)；[Word](Week3实习周报.docx)；[PDF](Week3实习周报.pdf) | 本周思路、架构、详细测试、反馈与边界 |
| 本周代码 | [src/week3](../../codes/src/week3/) | 本周功能实现 |
| 已有依赖 | src/week2/board.c、board.h、irq.h | 支持构建，不计入本周新增成果 |
| 内存分区交付物 | [说明原文](../../codes/docs/week3-memory-layout.md)；[PDF](../../codes/docs/week3-memory-layout.pdf) | 四区偏移、CPU地址、容量、权限、错误码与仿真边界 |
| 技术指南 | [week3-guide.md](../../codes/docs/week3-guide.md) | 模块与运行说明 |
| 存储操作测试交付物 | [记录原文](../../codes/docs/week3-test-report.md)；[PDF](../../codes/docs/week3-test-report.pdf) | 存储API及持久化输入/预期/实际，附完整11组阶段记录 |
| 阶段证据 | [最新生产回归](../../codes/logs/week3-20261025-192322-302943/summary.json) | 11组检查PASS |
| 反馈状态 | [逐项记录](../../planning/teacher-feedback-2026-10-24.md) | 当前均待老师确认 |
| 计划对应与包清单 | [两周交付总索引](../../planning/week3-week4-delivery-index.md) | 原计划条目、共享依赖与证据分配 |

补充材料：simatic-memory-mapping.md、irq-contract-validation.md、uart-baud-validation.md、tx-full-validation.md、cli-week34-test-report.md及UART至QMP时序图。本周正文已归纳对应结果；跨周专项须在完整工程复验。

独立交付包：output/2026-10-25-week3-交付材料.zip。包内codes为本周及必要依赖的可构建快照，主工程仍唯一维护在根codes/。包根README.md为阅读与运行入口，delivery-manifest.json记录文件长度和SHA256。

2026-10-25存储专项交付核验：正式codes工程重新运行11组PASS，内存说明3页、测试记录4页均为A4竖向。源码指纹和范围判定见[核查记录](../../planning/week3-storage-deliverables-audit.md)。周报中的历史实验日期和旧表保留原义，当前两份独立交付物以本次PDF及最新证据为准。
