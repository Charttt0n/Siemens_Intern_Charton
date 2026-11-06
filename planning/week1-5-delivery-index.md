# 前五周阶段交付总索引

2026-11-06 更新了 [Week 5 交付索引](../reports/week5/delivery-index.md)、周报和测试报告。复验版本在项目 `output/2026-11-06-week1-5-交付包.zip`，以下保留 11 月 1 日的整体验收记录。

更新日期：2026-11-01。当前交付范围是原计划 Week1 至 Week5；代码仿真功能通过，完整计划仍有 Week1 研究缺口及平台适配待确认，老师验收状态为待审核。

## 先读材料

1. [符合性核查报告 Markdown](week1-5-conformance-2026-11-01.md)、[Word](前五周开发符合性核查报告.docx)、[PDF](前五周开发符合性核查报告.pdf)：逐周要求、实测结果、适配差异和待补项。
2. [Week5 周报与交付索引](../reports/week5/delivery-index.md)：新增本周正式材料。
3. [本轮验证摘要](../codes/logs/week1-5-audit-20261101/summary.json)：13 套验证的固定目录与计数。
4. [项目原规划](Industrial%20Embedded%20Systems%20Bootloader%20Development.pdf)和[工时补充版](项目理解＆W1理解&八周规划_工时补充版.docx)：验收基线。

## 每周交付材料

| 周次 | 正式入口 | 源码和证据 |
|---|---|---|
| Week1-2 | [Markdown 周报](../reports/week1-2/week1-2-report.md)、[Word 周报](../reports/week1-2/Week1-2实习周报.docx) | codes/src 与 codes/src/week2；环境、启动说明和验证报告在 codes/docs |
| Week3 | [独立交付索引](../reports/week3/delivery-index.md) | UART/Flash 与分区；周报三格式，内存布局及测试报告 PDF |
| Week4 | [独立交付索引](../reports/week4/delivery-index.md) | CRC32、镜像、交接与应用；含 10 月 31 日 Mentor 反馈修订 |
| Week5 | [独立交付索引](../reports/week5/delivery-index.md) | YMODEM、A/B、参数事务；新增周报和技术测试报告 |

Week1-2 保留已归档的合并周报；原规划没有要求额外 PDF，本轮不为统一扩展名改写历史周报。Week3、Week4 原报告保留各自实验日期，不能把旧日期理解为本轮复测。最新状态以本索引及核查报告为准。

## 本轮复验记录

- week1-review：6 组 PASS；[summary.json](../codes/logs/week1-review-20261101-005706/summary.json)。
- week2：12 组 PASS；[summary.json](../codes/logs/week2-20261101-005710/summary.json)。
- week3：11 组 PASS；[summary.json](../codes/logs/week3-20261101-005715-135530/summary.json)。
- week4：27 组 PASS；[summary.json](../codes/logs/week4-20261101-005717-815700/summary.json)。
- week5：71 组 PASS；[summary.json](../codes/logs/week5-20261101-005722-628287/summary.json)。
- feedback：30 组 PASS；[summary.json](../codes/logs/feedback-20261101-010030/summary.json)。
- handoff-contract：7 组 PASS；[summary.json](../codes/logs/handoff-contract-20261101-005853/summary.json)。
- vector-bounds：9 组 PASS；[summary.json](../codes/logs/vector-bounds-20261101-005856/summary.json)。
- irq-contract：2 组 PASS；[summary.json](../codes/logs/irq-contract-20261101-005901/summary.json)。
- tx-full：7 组 PASS；[summary.json](../codes/logs/tx-full-20261101-005901/summary.json)。
- uart-baud：2 组 PASS；[summary.json](../codes/logs/uart-baud-20261101-005903/summary.json)。
- cli-week34：22 组 PASS；[summary.json](../codes/logs/cli-week34-20261101-005904/summary.json)。
- manifest：43 组 PASS；[summary.json](../codes/logs/manifest-20261101-005933/summary.json)。

Week5 顶层 71 组中包含 9 项互操作、23 条启动记录、11 条 negative_cases、9 个故障快照场景；分类互相重叠。向量 9 个分组包含 321 条断言及 8 条 CPU/GDB 路径；PRIMASK 2 个分组共 22 条断言。不要将所有数量相加成“独立用例总数”。

## 交付包与复现

本地完整快照为项目 output/2026-11-01-week1-5-交付包.zip。解压后先读包根 DELIVERY.md，内含当前源码、脚本、规划、周报、技术报告、当前及历史证据、licenses 和本次演示固件；历史失败/中间日志仅作追溯，不作为当前验收结果。

包内 delivery-manifest.json 用相对路径、字节数和 SHA256 固定实际文件版本。包根执行 `python3 codes/scripts/delivery_manifest.py .` 验证；工作区 HEAD 为历史基线且有未提交修改，不能仅凭 commit ID 还原本轮状态。ZIP 自身摘要在 output/2026-11-01-week1-5-交付包.zip.sha256。

进入 codes 后执行 `source scripts/env.sh`、`make verify-all`；专项另见核查及测试报告。首次运行所需 QEMU、Arm 工具链、GDB、CMake/Make 和宿主 C 编译器由 codes/README.md 说明。Python 第三方发送器已随 tests/third_party 固定，不需要在线安装。

[交付固件冒烟记录](../codes/logs/week1-5-audit-20261101/delivery-smoke.json)验证构建产物通过 v2 上传、重启和 v3 上传。源码交付可重建，包中 binaries/week5 为便利副本；复制时按 DELIVERY.md 指令指定 ELF 路径。

本次只生成本地交付材料，未提交 Git、未推送远端、未向老师发送。前五周仿真验证完成不代表研究缺口已补齐，不代表老师验收或工业量产符合性。
