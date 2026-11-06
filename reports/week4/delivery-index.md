# Week 4 交付材料索引

更新日期：2026-11-01。范围：CRC32 镜像校验、应用加载、安全跳转及测试应用。Week4 阶段验证 27 组通过，包含 22 种镜像输入；当前向量专项为 321 条宿主断言及 11 条 CPU/GDB 路径通过，其中 11 月 1 日新增三条独立 CPU 镜像拒绝测试。

| 类别 | 文件位置 | 内容 |
|---|---|---|
| 周报 | [Markdown](week4-report.md)；[Word](Week4实习周报.docx)；[PDF](Week4实习周报.pdf) | 完成情况、实现思路、测试结果与后续计划 |
| 本周代码 | [src/week4](../../codes/src/week4/) | 镜像校验、加载、交接及应用实现 |
| 共享依赖 | src/week3 和 src/week2 | 串口、Flash、启动及板级接口 |
| 技术指南 | [week4-guide.md](../../codes/docs/week4-guide.md) | 模块说明和构建运行方法 |
| 测试报告 | [Markdown](../../codes/docs/week4-test-report.md)；[Word](../../codes/docs/week4-test-report.docx)；[PDF](../../codes/docs/week4-test-report.pdf) | 输入、步骤、预期结果、实际结果与日志 |
| 阶段证据 | [summary.json](../../codes/logs/week4-20261029-151421-074982/summary.json) | 27 组检查结果及 22 份镜像的 SHA256、头和向量 |
| 状态图 | [启动流程与失败状态](../../codes/docs/images/week4-boot-flow-reviewed.png) | 正常启动、错误状态及失败去向 |
| 清单格式 | [delivery-manifest.schema.json](../../codes/schemas/delivery-manifest.schema.json)；[示例](../../codes/schemas/examples/) | 文件路径、长度、SHA256 等约束 |

专项日志目录见周报末尾的材料与证据索引。固件使用 M7IM 二进制镜像头，交付文件清单使用 JSON，两者分别校验。

## 补充交付

- 项目根目录的 [项目规划书](../../Industrial%20Embedded%20Systems%20Bootloader%20Development.pdf) 与 planning 原件内容一致，README 提供首个入口。
- 周报新增异常与中断交接、FPU 配置与扩展要求，以及独立的安全边界章节；三个向量案例和 MSP 端点有明确输入、CRC 处理、预期及实际证据。
- 技术测试报告 Markdown、Word、PDF 同步更新，包含当前计数、VB-04 至 VB-06 的构造和 UART 证据、优先级实测编码、变更对照及证据索引。
- [新增专项脚本](../../codes/scripts/verify-handoff-contract.py) 和 [结果摘要](../../codes/logs/handoff-contract-20261031-161440/summary.json)：7 组通过。
- [当前向量复验（11 月 1 日）](../../codes/logs/vector-bounds-20261101-164709/summary.json)：321 条宿主断言及 11 条 CPU/GDB 路径全部通过，其中新增 VB-04（MSP=top+16）、VB-05（保留项 index 8 非零）、VB-06（IRQ31 Thumb 位错误）。
- [核心复验（10 月 31 日）](../../codes/logs/week4-20261031-161348-567486/summary.json) 27 组通过；[PRIMASK 复验（10 月 31 日）](../../codes/logs/irq-contract-20261031-161420/summary.json) 22 条断言通过。
- 10 月 31 日的 321 条宿主断言及 8 条 CPU/GDB 路径作为历史记录保留，见[历史摘要](../../codes/logs/vector-bounds-20261031-161335/summary.json)；当前向量结果以 11 月 1 日的 11 条路径为准。
- FPU 活动现场交接与真机验证列为待实现或待验证；没有将条件设计计为已通过测试。
