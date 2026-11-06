# 带教反馈复核记录

复核日期：2026-10-29。范围为本次提供的 Tyler 聊天截图可见内容、当前源码、测试脚本、原始证据及 Week4 周报。截图最上方不完整，不据此推断未显示的要求。前三周周报作为叙述方式参考，保留 Week3 与 Week4 的任务划分。

## 结论

截图可见的反馈均已有对应实现或材料，本轮 8 套现有测试全部通过。现有证据支持当前 QEMU AN500 工程的检查范围；不代表带教已经确认，也不代表真机时序、物理 UART 拥塞或安全认证已验证。Week5 仍待带教确认后推进。

Week4 原周报共 17 页，包含大量跨周专项明细及多个日期的结果。本次修订为 11 页，按完成情况、实现思路、验证过程、问题与计划展开，并集中列出镜像输入、范围规则、状态字典及清单约束。保留技术报告中的完整步骤，不把详细测试表简单删除。

## 逐条对应

| 截图可见反馈 | 当前实现或材料 | 本轮复验与判断 |
|---|---|---|
| 临界区恢复原来的 PRIMASK，原本屏蔽时返回后仍屏蔽 | codes/src/week2/irq.h 保存并恢复进入值；Week3 uart_init / uart_getc 调用；codes/docs/irq-contract-validation.md 说明嵌套契约 | verify-irq-contract.py：初值 0、1 各 11 断言通过，覆盖两层嵌套及真实 UART 调用。满足可见要求；不把 PRIMASK 解释成屏蔽 NMI/HardFault |
| 从 UART 命令完成到 QMP cont 的时序图 | Week3 周报第八节；codes/docs/images/uart-checkpoint-sequence.png；Session.checkpoint | verify-week3.py 的检查点成功、替换失败后恢复、重启均通过；write/qmp.json 留存 stop、pmemsave、cont 请求与响应。现有工作区另有启动提示符后的 stop/cont 修改，已保留并纳入本轮测试；它与保存流程的 stop/cont 分别说明 |
| TX 阻塞超时与异常控制 | 两版 uart_flush 最多轮询 1000000 次，失败向 putc/write 及 main 传播；codes/docs/tx-full-validation.md | 7 组通过，含两驱动 20 断言、生产符号隔离及 4 条上层错误路径；Bootloader 0x101、应用 4 均读回正确。属于有限轮询，未定义毫秒截止时间 |
| 第三阶段镜像输入列入表格 | 按实际源码与规划范围归入 Week4；week4-test-report.md 和 Week4 周报保留 22 行具体输入 | verify-week4.py：22 种输入均同时经宿主校验器与 QEMU 检查；27 组总结果通过。编号沿用历史 IMG-01..22，IMG-22 为最大长度镜像，不按脚本循环顺序重编号 |
| 检查向量合法范围 | image.c 的 vectors_ok；test_vector_bounds.c；范围规则已列入周报 | 321 个宿主断言通过；4 个 CPU 拒绝、2 个加载后向量复核、2 个状态路径通过。加载后向量注入先更新测试 CRC，确实到达向量拒绝分支 |
| 状态转移图 | 现有 boot-state-complete 图与源码对应；本次在周报补入便于阅读的 Week4 图 | 逐步核对 main.c、app/main.c 与 handoff.S，图示表达控制流程，不将 boot_status 当进度枚举；补充任何日志输出失败、应用错误提示输出失败的去向。未声称逐条注入全部状态分支 |
| REJECT 与 boot status 的格式、字段、错误码及映射 | reject()、image_result、exception_handler；周报及技术说明的状态表 | 负向镜像逐字节检查 REJECT；复制 CRC 错误 8、读取错误 1、HardFault 0x100、TX 0x101 均有实际运行证据。UART 故障不保证输出完整拒绝行；boot_status=0 不单独证明应用成功 |
| 第四阶段补 manifest schema | codes/schemas/delivery-manifest.schema.json；delivery_manifest.py；examples | verify-manifest.py：41 项通过；在隔离副本执行，保留已归档示例。当前定义是交付文件清单，固件仍使用 M7IM 二进制头；不把清单说成固件认证协议 |
| 记录每点修改，完成后再推进下一阶段 | 历史反馈编号 FB-01..12、版本及待确认状态均保留；本记录追加本次结果 | 本次未开始 Week5，未提交或推送本轮修改，未改变任何带教确认状态 |

## 本轮执行证据

以下路径相对 codes/。统一执行记录为 logs/feedback-review-20261029-151415/summary.json，执行起止时间采用北京时间。各套件在独立构建目录或隔离副本中运行；manifest 测试产生的例子仅留在本轮日志中，不覆盖仓库已有例子。

| 测试入口 | 本轮结果 | 证据目录 |
|---|---|---|
| scripts/verify-irq-contract.py | 2 组，22 断言通过 | logs/irq-contract-20261029-151415 |
| scripts/verify-tx-full.py | 7 组通过；两版驱动共 20 断言 | logs/tx-full-20261029-151415 |
| scripts/verify-week3.py | 11 组通过 | logs/week3-20261029-151418-332736 |
| scripts/verify-week4.py | 27 组通过，含 22 种镜像输入 | logs/week4-20261029-151421-074982 |
| scripts/verify-vector-bounds.py | 321 宿主断言及 8 条 QEMU/GDB 路径通过，summary 中为 9 个分组 | logs/vector-bounds-20261029-151425 |
| scripts/verify-uart-baud.py | 2 组读回通过 | logs/uart-baud-20261029-151428 |
| scripts/verify-cli-week34.py | 22 项通过，Week3 与 Week4 各 11 项 | logs/cli-week34-20261029-151429 |
| scripts/verify-manifest.py | 41 项通过 | logs/manifest-20261029-151458 |

上述数字有分组、断言、镜像输入及跨周共享测试等不同口径，不能相加宣称为独立 Week4 测试总数。本轮没有重新执行 Week1 与 Week2 的完整阶段套件；其周报只用作写法参考。

## 仍需保留的边界

1. TX 的有限轮询证明能返回错误，不证明指定毫秒内返回；本轮没有制造物理字符后端持续拥塞。
2. 向量检查约束入口起始地址、Thumb 位和保留项，不证明任意内部地址上的完整指令或函数语义；边界接受用例不用于执行任意修改后的入口。
3. 状态表是源码映射；load 参数、UART 初始化、应用 CRT/SVC 失败等全部子分支没有逐条故障注入，不将部分路径测试写成全部分支覆盖。
4. schema 的结构约束与文件存在性、重复路径、哈希和符号链接越界等语义检查共同构成当前校验器；交付清单不是签名，也不是带教验收证明。
5. 旧交付 ZIP 是历史快照，不含本轮改写。旧反馈版本指纹保留原始语义；本轮结果由独立记录追踪，不回写历史提交的指纹。

## 本次文件整理

Week4 的 Markdown、Word 与 PDF 同步更新；阶段测试报告、交付索引追加本轮证据。修订前的周报、索引和测试报告保存在 archive/feedback-review-20261029/before/，文件 SHA256 见同目录 inventory.json。原有 codes/scripts/week3_session.py 工作区修改未覆盖，复验时差异快照保存在统一日志目录 baseline-session.diff。
