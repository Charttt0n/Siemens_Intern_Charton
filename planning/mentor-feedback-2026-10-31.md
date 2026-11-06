# Week4 Mentor 反馈修改与验收记录

日期：2026-10-31。范围：Cortex-M7 AN500 的 Week4 校验及软件跳转；没有更改镜像格式或公开接口。保留工作区此前的修改。

| 反馈要求 | 落实位置与验收 |
|---|---|
| 三个向量边界用例 | 周报第五章、技术测试报告末尾：VB-01/02/03 对应 VEC-016/019/021；明确偏移、CRC、预期、实测及不重复计数 |
| MSP 区间 | 第五章明确 (base,top]，四个端点判定及栈容量限制 |
| 异常及中断交接 | 第六章涵盖五种异常的优先级、屏蔽、MSP/PSP、EXC_RETURN、三阶段检查；7 组新专项实测 |
| FPU | 第七章分当前软浮点读回与未来 FPU 要求；覆盖 FPCCR、CONTROL、CPACR、FPSCR、FPCAR 等 |
| 安全边界单列 | 第八章独立讨论检查范围、认证缺口、MPU、异常停留与 QEMU/真机适用范围 |
| 规划入口 | 根目录同名 PDF 和 README 首个链接；原件保留 |

固件代码改动仅修正应用 Reset 注释；新增验证脚本在临时 RAM 注入真实异常。现有代码不会在交接时重置所有优先级或处理任意活动浮点上下文，报告明确记录这些限制。

## 本次执行证据

- 核心：codes/logs/week4-20261031-161348-567486/summary.json，27 组通过。
- 向量：codes/logs/vector-bounds-20261031-161335/summary.json，321 条宿主断言及 8 条 CPU/GDB 路径通过。
- 交接：codes/logs/handoff-contract-20261031-161440/summary.json，7 组通过。
- PRIMASK：codes/logs/irq-contract-20261031-161420/summary.json，22 条断言通过。

周报 Markdown、Word、PDF 与技术测试报告 Markdown、PDF 同步交付；导出后执行内容核对与逐页版式检查。文档更新不会重写历史实验日期。FPU 扩展帧、非精确总线异常和真机硬件时序仍为明确的后续验证项。

## 文档交付检查

周报与技术测试报告各 17 页，已逐页检查 PNG；Markdown 正文及表格与 Word 内容核对通过，PDF 中的架构参考链接保留。规划 PDF 两份 SHA256 一致，README 与交付索引的本地链接均存在。版式及内容核对记录位于 output/mentor-feedback-20261031/artifact-qa.json。
