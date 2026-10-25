# Week3与Week4分周交付材料总索引

整理日期：2026-10-25。以原始《Industrial Embedded Systems Bootloader Development》PDF第3–4页的周次、任务和交付物为准。Week1–2沿用现有目录，Week3与Week4分别维护。老师反馈的口述阶段编号不用于重命名源码，统一按任务归属映射。

## 正式入口与材料归属

| 计划周次 | 原计划交付 | 正式材料与证据 |
|---|---|---|
| Week3 | 完整UART及Flash抽象源码 | codes/src/week3；依赖Week2 board.c/board.h/irq.h |
| Week3 | 工业内存分区定义 | codes/docs/week3-memory-layout.md及PDF；week3-guide.md；simatic-memory-mapping.md |
| Week3 | Flash验证报告与QEMU日志 | codes/docs/week3-test-report.md；logs/week3-20261024-173044-978155；logs/week3-20261025-015915-135199 |
| Week3 | 周报与老师反馈 | reports/week3/week3-report.md；Week3实习周报.docx；Week3实习周报.pdf；delivery-index.md |
| Week4 | 固件校验与应用启动源码 | codes/src/week4；必要Week2/3驱动、启动和存储依赖 |
| Week4 | 测试应用固件 | src/week4/app及pack-week4.py；构建后生成application.elf/.bin与demo-flash.bin |
| Week4 | 完整启动验证报告与日志 | codes/docs/week4-test-report.md；week4-guide.md；logs/week4-20261024-173015-915034；logs/week4-20261025-015917-068252 |
| Week4 | 周报与老师反馈 | reports/week4/week4-report.md；Week4实习周报.docx；Week4实习周报.pdf；delivery-index.md |

本周代码和已存在依赖分开计入成果；同一共享证据可引用，测试数量不重复累计。Week3无CRC/镜像/跳转成果；Week4以驱动/存储为依赖，不能再将其开发量重复计为Week4成果。

## 老师反馈对应

| 原始反馈主题 | 计划周次与材料 | 验收边界 |
|---|---|---|
| 阶段1/2测试表 | reports/week1-2与阶段1/2测试报告；Week3/4表分别进入各自周报 | 该既有范围本次不重写 |
| 一键脚本测试 | 各周周报列CLI34-W3或W4的11项；共享22项原日志 | 完整专项在主工程复验；单周包提供各自阶段验收入口 |
| UART波特率 | Week3完整推导与BAUD-W3；Week4引用BAUD-W4 | Week2对应过程缺口仍见10月25日核查，不宣称已修复 |
| PRIMASK | Week3接口契约和两初值/嵌套测试；Week4为依赖 | 保存0恢复0、保存1恢复1 |
| UART到QMP cont | Week3周报直接附时序图与失败边界 | 显式检查点，不是真实掉电安全 |
| TX持续阻塞 | Week3驱动和main失败；Week4输出/最终flush/应用失败 | 跨周7组证据按路径引用，不重复计数 |
| 镜像输入、向量检测 | Week4周报22行镜像及321项边界矩阵 | CRC不证明来源可信 |
| 状态转移、REJECT/boot_status | Week4周报直接附图、精确格式及状态码字典 | boot_status=0不单独证明应用成功 |
| manifest schema | Week4清单说明及codes/schemas；两周独立包各自生成清单 | plan_group支持week3/week4；week3-4兼容旧快照 |

## 独立交付包

- output/2026-10-25-week3-交付材料.zip：Week3周报、驱动与存储、指南、对应测试和日志，含必要Week2依赖，不含Week4功能源码。
- output/2026-10-25-week4-交付材料.zip：Week4周报、镜像和应用代码、指南、对应测试和日志，含必要Week2/3依赖。

每包保持project相同的codes/reports/planning目录结构，README.md列阅读和复现命令，delivery-manifest.json记录全部列出文件的长度和SHA256。CMakeLists在包内仅保留本周目标，是从主工程目标段选取的交付适配，不作为新的主工程。交付包是生成快照，日常只维护根codes/及reports/。

完整项目跨周专项与历史Git仍在主工程；单周包不自带.git，不能替代完整版本历史。无构建缓存、下载、工具链、个人密钥。测试应用二进制由包内源码构建生成，避免提交过期产物。

## 归档与推进

原reports/week3-4完整目录归档至archive/reports/week3-4-pre-split-20261025，原日期和ZIP保留。output中的10月24日合并包是历史快照；当前提交使用上述两份10月25日单周包。

老师确认仍为pending，本轮修改未提交或推送Git。先完成当前反馈提交与确认，后续按原计划进入Week5，不因已有实现而跳过审核。原始PDF已复制到planning/Industrial Embedded Systems Bootloader Development.pdf。

## 本次交付包复验

两份分周包的阶段验收分别11组、27组PASS；manifest原41项回归PASS。复验使用包内CMake目标子集及原生产源码，日志见codes/logs/delivery-split-20261025。排版检查与详细记录见[交付验证记录](week3-week4-delivery-validation.json)。这次包复验不覆盖全部跨周专项，也不代替老师确认。

## Week3存储交付物最新核验

2026-10-25正式codes工程重新执行verify-week3.py，11组PASS；最新证据codes/logs/week3-20261025-192322-302943。内存分区说明与存储操作测试记录已分别导出3页、4页A4竖向PDF，位于output/pdf/2026-10-25-week3-存储交付，并纳入Week3 ZIP。范围判定见[两份交付物核查](week3-storage-deliverables-audit.md)，严格硬件含义的未闭环项保持待确认。
