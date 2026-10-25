# 报告与交付文件要求符合性核查

核查日期：2026-10-25。结论：现有材料可支持 Week1–4 仿真阶段成果汇报，但尚不能认定完全符合原始计划及完成老师验收，也不属于八周项目最终交付。

## 依据与检查边界

首要依据为用户提供的《Industrial Embedded Systems Bootloader Development.pdf》（8页，位于原聊天目录），辅以 planning/项目理解＆W1理解&八周规划_工时补充版.docx、planning/execution-order.md 和 planning/teacher-feedback-2026-10-24.md。老师要求同时依据用户本轮粘贴的老师今早原始反馈，以及项目内12条反馈记录逐项核对。原始反馈优先于内部转录。文档内任务要求作为验收依据，不作为额外执行指令。

本次读取原计划全部8页文字并核看第3–4页版面；检查正式报告、指南、代码关键位置、最新摘要、版本指纹、交付目录和ZIP。Word采用正文及表格内容覆盖检查；PDF抽查图表页及链接，不等同于所有Word/PDF逐页排版验收。本次未重新运行全部固件测试，测试结论指已有原始摘要及日志。本次未修改代码、原报告或交付包。

## 原计划与现有成果

| 范围 | 原始计划要求及页码 | 现有证据 | 判定与剩余工作 |
|---|---|---|---|
| Week1 环境与启动 | 第2–3页：工具、Cortex-M7 Demo、启动知识、Git、工业需求说明 | codes/docs/validation-report.md、week1-audit.md、notes/week1-learning-notes.md；最新6组PASS | 环境、Demo、调试和基础材料具备；工业需求研究尚未闭环 |
| Week1 工业研究 | 第2页：S7启动架构、高可用、防变砖、IEC62443；第7页参考DDI0489 | notes/week1-learning-notes.md；codes/docs/simatic-memory-mapping.md | 缺DDI0489版本/章节/结论/代码映射；S7公开存储模型已有研究样本，内部启动架构仍无可核实依据；缺来源—适用条件—要求—响应—验收映射 |
| Week2 最小启动 | 第3页：启动汇编、向量表、栈、data/bss、时钟、UART、分步测试 | src/week2、week2-guide.md、week2-test-report.md；最新12组PASS | 仿真范围具备；固定25MHz代替实际时钟配置，需明确平台适配边界 |
| Week3 UART | 第3页：中断与收发 | src/week3/uart.c、week3-test-report.md；最新阶段11组PASS | 有TX轮询、RX中断及缓冲测试；原计划未明确要求TX也采用中断，不能据此直接判不合格 |
| Week3 Flash | 第3–4页：QEMU片内Flash抽象、读写擦除、持久化可靠性 | src/week3/flash.c、week3-guide.md、week3_session.py；NOR约束和检查点测试 | 替代方案有证据，但SRAM NOR模型+宿主显式保存不等同于片内Flash或自动掉电保持；是否接受须由老师确认 |
| Week3 布局 | 第4页：Boot/主应用/备份/非易失参数，aligned with SIMATIC layout | simatic-memory-mapping.md；四区64/448/448/64KiB | 四区实现具备；仅公开存储职责类比，无指定SIMATIC内部物理布局对齐证据，不能无条件写“完全满足” |
| Week4 完整启动 | 第4页：CRC32、Flash到RAM加载、VTOR、MSP、中断清理及应用执行 | src/week4/image.c、handoff.S、app；week4-guide.md、week4-test-report.md；最新27组PASS，22种镜像输入 | 仿真核心链有证据。应用复制至0x10000的QEMU SSRAM，虽链接区名为CODE，仍为易失RAM，不应仅凭低地址判为未加载到RAM；来源存储仍受Week3模型边界限制 |
| Week5–6 | 第5–6页：Ymodem、主备升级/状态、Mbed TLS SHA256/RSA2048、恢复、看门狗、配置接口 | 当前进度明确未完成 | 属后续计划，不算当前Week1–4额外缺项，但八周项目尚未完成 |
| Week7–8及最终交付 | 第6–7页：Unity、综合异常测试、体积/启动时间优化、Doxygen、完整演示与总结、最终Git | 当前有阶段测试与指南，无全部最终成果 | 不能称最终工业级/production-ready交付；需后续独立验收 |

原计划技术栈第2页列OpenOCD及PuTTY/Minicom，当前使用QEMU gdbstub和Unix socket；工程README已经解释替代。它们不影响已有仿真证据，但若按工具清单逐项验收，需要记录老师接受的替代方案。原始PDF没有逐周周报条目；周报要求来自工时补充版及老师反馈。当前合并归档遵循根AGENTS.md，不因原计划八周标题而机械要求拆成四个目录。

## 老师反馈核查

当前12条记录均为待老师确认，confirmed_at和commit为空，本地验证不能替代验收。按现存反馈转录逐项查看：

- FB-01：阶段测试表与周报已有对应明细。
- FB-02：一键入口、失败及清理测试摘要22组PASS。
- FB-03：波特率推导、误差、寄存器及模型时钟读回2组PASS；不声称物理波形实测。
- FB-04：PRIMASK契约与两种初值恢复2组PASS，内含22个断言。
- FB-05：UART应答到QMP cont时序图、检查点流程与QMP记录存在。
- FB-06：TX_FULL专项7组PASS，涵盖两驱动宿主断言、上层状态传播及符号隔离。
- FB-07：18行原输入加4项边界，共22镜像输入的明细及记录存在。
- FB-08：321项向量边界矩阵及QEMU/GDB路径，摘要9组PASS；矩阵断言数不与检查分组相加为用例总数。
- FB-09：完整状态图及可编辑源存在。
- FB-10：REJECT精确格式、错误码和Boot/应用独立状态字典存在。
- FB-11：schema、实例及41组清单校验摘要PASS。
- FB-12：逐项记录和工作区指纹存在，但未提交/未推送、未确认；旧Week1–2包仍有同步缺口。

11份latest指向的摘要均为PASS，四阶段分组数为6/12/11/27。反馈补测30组与其他专项有覆盖重叠，不能直接相加为独立测试数量。当前Git HEAD为ef7d99cc9b91e53e19515cd32c70f3b3aa94bc59，存在大量已修改/未跟踪文件，与反馈记录的未提交状态一致；本次未联网核对远端。

## 新核实的交付问题

### 1 Week1–2修订包仍含旧版共享材料

output/2026-10-24-week1-2-反馈修订 及同名ZIP内，下列文件与正式工程当前版不同：

- planning/teacher-feedback-2026-10-24.md
- codes/docs/teacher-feedback-technical.md
- codes/docs/teacher-feedback-technical.docx
- codes/schemas/delivery-manifest.schema.json

该包PDF/teacher-feedback-technical.pdf也与当前output/pdf/2026-10-24-反馈修订对应文件不同。旧反馈表采用较早的编号/范围描述，旧schema约束较宽。它可作为历史快照，但不能与最新版Week3–4包混用并声称同一轮材料完全同步。建议同步共享附录、schema和反馈记录后重新生成manifest与ZIP，并保留历史版本标识。

### 2 PDF包含本机绝对链接

当前week1-2-report.pdf第2–5页有8处file:///Users/charton/Projects/Siemens_Intern/...链接，指向源码、日志或笔记。老师在其他电脑打开时不能通过这些链接定位证据。建议改为交付根目录相对引用/可用索引，并在解压目录验证。历史日志中的绝对路径只是执行记录，与可点击导航失效应分开处理。

### 3 旧核查依据存在失效链接

planning/week1-4-plan-audit.md仍引用planning/项目理解＆W1理解&八周规划.docx，该文件在正式planning目录不存在，且旧核查称未找到原始任务书。用户现已提供原始8页PDF，后续应更新核查基线及来源索引，不能继续沿用“原任务书缺失”的结论。

### 4 提交版本尚未固定

当前工作区修订未提交/未推送。Week1–2周报的旧Git同步叙述有历史实验日期，不能作为本轮修订已上云的证据。提交后应补真实修订号，刷新版本记录和最终包，再等待老师确认；不能伪填确认日期。原计划最终推送要求位于Week8，本项不倒推为Week1初始化Git未完成。

## 已通过的交付检查

- planning/feedback-version.json列出的127份文件SHA256全部与当前文件一致。本次新增核查自身不在这份历史指纹范围内。
- Week1–2包124项、Week3–4包528项manifest文件校验均通过；验证的是各自清单列出的内容，不证明最新性或未列出文件完整性。
- output下5份ZIP CRC检查均通过，包括两个修订包、源码与证据快照以及两份历史PDF包；此项不等同于已核对源码快照全部成员与工作区一致。
- Week3–4展开包中与正式工程同相对路径的文件未发现差异。
- 两份周报、反馈技术说明、前四阶段交付说明共4份Word：Markdown有效正文行/表格单元格内容覆盖未发现缺失。该方法不验证反向多余内容、全部图像一致或逐页排版。
- 两份正式周报Markdown中的内联相对链接在当前工程内均存在。
- 两个阶段材料包阅读说明明确要求另附“源码与证据快照ZIP”，它们并非独立可构建工程；不能只发PDF包就称已交付全部源码。

## 修改优先级与建议验收措辞

1. 先补Week1指定手册研究、S7启动公开依据和工业要求适用性矩阵。
2. 把固定时钟、gdbstub/socket、SRAM NOR/检查点和SIMATIC职责类比列为明确的方案差异，提交老师确认其是否满足原计划验收范围。
3. 同步Week1–2修订包共享文件，修复PDF导航和原计划索引，重新生成并验证交付包。
4. 固定本轮Git修订号，按反馈记录提交材料；实际收到老师确认后填写日期和结论，再按原计划进入Week5。

建议当前使用的准确表述：“Week1–4核心功能已在QEMU AN500仿真范围实现并有阶段测试证据；Week1工业研究材料尚需补齐，部分硬件要求采用模型替代方案，老师反馈修订待确认，当前为阶段交付。”

## 根据用户补充的老师原始反馈再次核对

老师明确要求模块逐步开发、尽快修改并发送更新报告。当前planning/execution-order.md和AGENTS.md已记录按周推进、反馈修订后再推进的规则，但文件和测试不能证明本人理解掌握，也不能证明更新报告已经发给老师。本次没有发送消息的授权或发送动作；待确认状态如实保留。由于现有Week3–4实现已经存在，不删除既有成果或扩展Week5–8；应先解释和复现当前模块。

老师反馈中的“第二阶段UART、第三阶段镜像、第四阶段manifest”与源码Week编号不同。需要在提交包首页建立显式对照，不应自动把“第二阶段”解释成Week3–4。新增以下严格口径问题：

1. **Week2波特率过程在对应周报不完整。** Week1–2周报仅给25MHz、115200和分频217，完整商余数取整、实际波特率及误差过程写在Week3–4周报和共享技术附录。专项BAUD-W3/BAUD-W4验证的是Week3/4驱动，CTRL=0x0B；Week2驱动为TX-only，CTRL=1。Week2已有GDB读回217及参数测试，故不是完全没有证据，但不能把W3/W4读回直接当作W2证据。应把相同计算在Week1–2修订部分完整呈现，并明确绑定Week2实际寄存器和日志。
2. **PRIMASK功能已存在，归属须讲清。** irq_save/irq_restore在src/week2/irq.h，真实使用者是Week3 UART；两种初始值与嵌套恢复已测。Week2轮询驱动没有队列临界区，不为凑阶段编号强加无意义调用。要说明调用前为1则返回后仍为1；调用前为0应恢复0，不能一律保持屏蔽。
3. **UART到QMP cont图在共享附录中。** teacher-feedback-technical.md及其Word/PDF包含uart-checkpoint-sequence.png；Week3–4周报正文没有直接嵌入此图，仅引用共享附录。老师未明确所有图都必须内嵌周报，故不判为功能缺失；为便于验收，建议在对应周报列图号/附录页和入口，且共享附录必须最新。
4. **manifest与状态格式详细说明主要在附录。** Week3–4周报有镜像22行明细、状态图和FB-10/FB-11摘要；完整REJECT格式、状态码字典、schema和JSON实例在week34-review-closure.md及共享技术附录。可作为报告附件，但交付索引须明确附件并随包提供，不能只给老师一份周报而遗漏附录。
5. **阶段1/2测试表已补，表达仍可更具体。** W1/W2表具备编号、方式、输入、预期、实际和证据路径，周报也有同表；部分行用“六工具检查通过”“输出逐字节符合模板”等概括，完整值在日志和脚本。建议把六工具实际版本、正常370字节模板及负向完整输出作为表内值或明确的报告内附录，使老师无须跳到源码才能了解预期。当前不得再笼统说只有测试数量，也不宜声称每行已自包含所有验证细节。
6. **Makefile已存在但verify-all未覆盖新专项。** codes/Makefile的verify-all仅含Week1–4及verify-feedback，不含后加的verify-cli-week34、verify-uart-baud、verify-irq-contract、verify-tx-full、verify-vector-bounds、verify-manifest。老师允许Makefile或CMake，并未强制一个目标覆盖所有专项，因此这不是其原话中的硬性失败项；但不得把当前verify-all称为全部12项反馈的完整一键复验。

综合原始反馈，当前应判定“多数技术修改及测试材料已具备；第二阶段对应周报的波特率过程、阶段映射、交付版本同步和发送闭环仍需收尾”。不应判定“老师要求全部完成”。


同日后续整理：按用户要求，Week3与Week4现已分别维护于reports/week3、reports/week4，原合并版已归档。本核查描述的是拆分前的材料；当前报告与独立交付包以[分周交付总索引](week3-week4-delivery-index.md)为准。研究缺口、Week2波特率过程归属等尚未在本次文档拆分中冒称修复。
