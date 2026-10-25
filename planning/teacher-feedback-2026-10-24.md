# 老师反馈逐项修改与确认记录

2026-10-25更新。本表只记录已有修改与验证；12项均待老师确认，Week5未开始。状态“本地已验证”不表示已提交Git或已获批准。

基线提交B0 = ef7d99cc9b91e53e19515cd32c70f3b3aa94bc59。本轮修订尚未提交、尚未推送，版本栏因此明确写为B0上的工作区修订；内容指纹和逐文件SHA256见planning/feedback-version.json。提交后应补入真实新提交号，收到老师反馈后再填写确认日期与结论。

<!-- landscape -->

| 编号与反馈 | 修改位置 | 验证用例 | 证据 | 修订版本 | 本地状态 | 老师确认 |
|---|---|---|---|---|---|---|
| FB-01 阶段测试细节与周报同步 | codes/docs/week3-test-report.md；week4-test-report.md；reports/week3/week3-report.md；reports/week4/week4-report.md | W3-01..11；W4五项+IMG-01..22 | codes/logs/week3-20261024-173044-978155；week4-20261024-173015-915034 | B0工作区；未提交 | 已修改并验证 | 待确认；日期/结论未填写 |
| FB-02 一键入口失败与清理 | codes/scripts/verify-cli-week34.py；codes/docs/cli-week34-test-report.md | CLI34-W3/W4各11项 | codes/logs/cli-week34-20261024-234802 | B0工作区；未提交 | 已修改并验证 | 待确认；日期/结论未填写 |
| FB-03 波特率公式与读回 | codes/docs/uart-baud-validation.md；scripts/verify-uart-baud.py | BAUD-W3；BAUD-W4 | codes/logs/uart-baud-20261024-235338 | B0工作区；未提交 | 已修改并验证 | 待确认；日期/结论未填写 |
| FB-04 PRIMASK契约与恢复 | codes/src/week2/irq.h；tests/test_irq.c；docs/irq-contract-validation.md | IRQ-INIT-0/1；22个断言 | codes/logs/irq-contract-20261024-235737 | B0工作区；未提交 | 已修改并验证 | 待确认；日期/结论未填写 |
| FB-05 命令至QMP cont时序 | codes/docs/images/uart-checkpoint-sequence.png；scripts/week3_session.py | W3-06；W3-07 | codes/logs/week3-20261024-173044-978155/write/qmp.json | B0工作区；未提交 | 已修改并验证 | 待确认；日期/结论未填写 |
| FB-06 TX持续阻塞传播 | codes/tests/test_uart_tx_full.c；scripts/verify-tx-full.py；docs/tx-full-validation.md | 两驱动20断言；四错误路径；生产符号隔离 | codes/logs/tx-full-20261025-015904 | B0工作区；未提交 | 已修改并验证 | 待确认；日期/结论未填写 |
| FB-07 镜像输入逐行表 | codes/docs/week4-test-report.md；week4-image-inputs.json | 18行原输入+4边界；22镜像还原哈希一致 | codes/logs/week4-20261025-015917-068252 | B0工作区；未提交 | 已修改并验证 | 待确认；日期/结论未填写 |
| FB-08 向量范围完整覆盖 | codes/tests/test_vector_bounds.c；scripts/verify-vector-bounds.py；docs/vector-boundary-cases.json | VEC-001..321；四CPU拒绝；两LOAD-VECTOR | codes/logs/vector-bounds-20261025-021521 | B0工作区；未提交 | 已修改并验证 | 待确认；日期/结论未填写 |
| FB-09 完整状态转移图 | codes/docs/images/boot-state-complete.png及.mmd；scripts/render-boot-state.py | 正常W4-HANDOFF；错误码/TX/异常路径核对 | codes/logs/week4-20261025-015917-068252；codes/logs/vector-bounds-20261025-021521 | B0工作区；未提交 | 已修改并验证 | 待确认；日期/结论未填写 |
| FB-10 REJECT与状态字典 | codes/docs/week34-review-closure.md；teacher-feedback-technical.md | IMG负向；W4-COPY；STATUS-IO；STATUS-HARDFAULT；TX-W4 | codes/logs/vector-bounds-20261025-021521；codes/logs/tx-full-20261025-015904 | B0工作区；未提交 | 已修改并验证 | 待确认；日期/结论未填写 |
| FB-11 manifest schema与实例 | codes/schemas/delivery-manifest.schema.json；schemas/examples；scripts/delivery_manifest.py | MF-VALID及40个负向约束/文件用例 | codes/logs/manifest-20261025-021604 | B0工作区；未提交 | 已修改并验证 | 待确认；日期/结论未填写 |
| FB-12 逐项闭环与版本 | planning/teacher-feedback-2026-10-24.md；feedback-version.json | 12项引用存在性；源文件内容指纹；包manifest和ZIP校验 | planning/feedback-version.json；output交付包/delivery-manifest.json | B0工作区；未提交 | 已修改并验证 | 待确认；日期/结论未填写 |

<!-- portrait -->

2026-10-25分周归档：反馈按原计划Week3/Week4对应到两份周报及delivery-index.md；老师口述阶段编号仅作反馈标签，具体归属见[分周交付总索引](week3-week4-delivery-index.md)。本次文档整理及manifest单周枚举扩展未变更固件行为，历史实验原样保留；本地修订仍未提交/推送、待老师确认。
