# 第二周前四阶段交付说明

项目：基于 QEMU 的 Cortex M7 工业 Bootloader

交付日期：2026 年 10 月 22 日　实际周次：第二周

## 一 交付范围和阅读顺序

本次交付覆盖原八周规划的前四个任务阶段：环境与学习、最小启动、驱动与存储、镜像校验与应用跳转。其中第三、第四阶段在实际第二周提前完成。week1 至 week4 是源码及脚本中的计划阶段编号，周报统一保存在 reports/week02。

建议先阅读第二周周报了解完成情况，再按本文运行完整启动演示，随后查阅第四阶段技术指南和各阶段测试摘要。该工程是 QEMU AN500 仿真实验，不是 SIMATIC S7 设备固件。

| 交付项 | 包内路径 | 验收依据 |
|---|---|---|
| 周报 | reports/week02/week2-report.md 和第二周实习周报.docx | 内容一致的文本与提交版 |
| 主代码 | codes/src、codes/CMakeLists.txt、codes/cmake | 四阶段独立目标可构建 |
| 构建运行工具 | codes/scripts | 可重建镜像并运行示例 |
| 自动测试 | codes/tests 及 verify 系列脚本 | 正常与故障检查 |
| 设计文档 | codes/docs/week2-guide.md、week3-guide.md、week4-guide.md | 启动、存储与交接契约 |
| 测试记录 | codes/docs 下测试报告及 codes/logs | 实测输出、断言、版本与摘要 |
| 规划和学习材料 | planning、notes | 原计划、当前进度与需求研究 |
| 交付索引 | delivery-manifest.json、SHA256SUMS.txt | 文件完整性和本次有效证据 |

交付包保留 project/codes 等相对目录结构；解压后以实际解压目录为根目录。日志中的旧绝对路径是执行时记录，不要求使用相同的用户名或文件路径。构建产物、缓存、下载包、Git 内部目录与临时渲染文件不进入交付包。

## 二 运行环境

本次在 Apple Silicon macOS 上验证。使用 QEMU 11.1.2、GNU Arm GCC 16.2.0，以及 CMake、Make、GNU Arm GDB、Python 3 和宿主 C 编译器。CRC／Flash 宿主测试启用 AddressSanitizer 和 UndefinedBehaviorSanitizer。

scripts/env.sh 配置 Homebrew 常用工具路径；已有工具链时无需重新安装。首次配置可参考 codes/README.md 和 scripts/install-tools.sh。当前脚本与串口连接使用 Unix socket 和文件锁，Windows 原生环境未验证。

<!-- pagebreak -->
## 三 完整启动演示

终端进入解压目录中的 codes，运行以下命令。正式工作目录为 /Users/charton/projects/Siemens_Intern/codes。

```sh
bash scripts/build-week4.sh
python3 scripts/run-week4.py
```

构建脚本在 build/week4 生成 Bootloader、独立应用和演示镜像。每次构建会重新生成 demo-flash.bin，仅用于演示；不要将重要实验数据存放在该演示文件中。应用 payload 本次为 972 字节。

```text
BOOT: stage4 image validation
IMAGE: CRC32 PASS
LOAD: verified at 0x00010000
JUMP: application
APP: CRT PASS
APP: HANDOFF PASS
APP: SVC PASS
APP: RUNNING
```

run-week4.py 在收到完整结果后停止宿主 QEMU；成功返回 0，镜像被拒绝或运行失败返回非零。这是演示脚本结束，不表示应用主动退出；固件完成日志后进入等待循环。

如需演示第三阶段存储持久化，执行：

```sh
bash scripts/build-week3.sh
python3 scripts/run-week3.py
```

依次输入 r、w、r、quit，读值由 0xffffffff 变为 0x4b4f3357。再次运行并输入 r，仍应读回该值；输入 e、quit 后再次启动，读值恢复为 0xffffffff。save 或正常退出才保存，Ctrl C 不保存当前未保存修改。

## 四 重新验收

```sh
python3 scripts/verify-startup.py
python3 scripts/verify-week2.py
python3 scripts/verify-week3.py
python3 scripts/verify-week4.py
```

每次验收使用全新临时构建目录；结果写入 codes/logs 下新的时间戳目录，详细结论读取 summary.json。第三、第四阶段测试使用独立镜像，不修改日常实验镜像。程序打印 FAIL 或 REJECT 的负向用例可能是预期现象，应以测试摘要及脚本退出码判定。

<!-- pagebreak -->
## 五 本次有效证据与边界

| 阶段 | 检查数 | 本次证据目录 |
|---|---|---|
| 环境和最小 Demo | 6 | week1-review-20261022-164544 |
| 最小 Bootloader | 12 | week2-20261022-164548 |
| UART 和存储 | 11 | week3-20261022-164553-309176 |
| 校验和应用启动 | 23 | week4-20261022-164853-369970 |

以上目录均位于 codes/logs，本次 summary.json 的 status 均为 PASS。第四阶段还保留首次未通过的故障注入记录，用于说明测试方式修正；最终判定以本表指定的完成记录为准。检查数包含工具、构建和回归分组，不能当作独立运行用例总数。

第四阶段的 dirty-state-handoff-and-app-crt.txt 检查入口 MSP、VTOR、中断状态、应用数据恢复和 SVC；reject-corrupted-loaded-copy.txt 检查运行区内容被修改后拒绝跳转；valid/uart.log 为完整正常演示日志。

CRC32 用于完整性检查，不能认证镜像来源。固件版本字段当前只记录信息，不实施防回滚。备份分区已经划定，但没有自动恢复逻辑。存储检查点不能替代真实 Flash 的断电安全性；尚未实现升级传输、签名验证、看门狗恢复或工业认证。

交接仅支持本工程约定的冷启动状态：特权线程模式、MSP、缓存与 MPU 未启用，AN500 的 32 个外部 IRQ。迁移到真实 Cortex M7 设备时，需另行处理时钟、Flash 驱动、外设复位、缓存一致性、MPU、DMA 和安全策略。

## 六 文件完整性与版本状态

交付包基于当前已验证工作区生成。SHA256SUMS.txt 记录包内各文件摘要，delivery-manifest.json 记录交付时间、实际周次、计划覆盖范围及四份有效测试摘要。新增代码与文档尚未形成新的 Git 提交并推送；不能将远程仓库旧提交视为本次完整交付版本。

在解压目录的 project 根目录执行以下命令，可核对交付文件是否发生变化。

```sh
shasum -a 256 -c SHA256SUMS.txt
```

源码、脚本、Markdown 和 Word 为正式交付内容；需要修改周报时，应同步维护两种格式。个人笔记放在 notes，任务进度放在 planning，技术指南和测试报告放在 codes/docs。
