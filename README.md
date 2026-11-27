# 工业嵌入式 Bootloader（Arm Cortex-M7 / QEMU MPS2-AN500）

本工程在 **QEMU MPS2-AN500（Arm Cortex-M7）** 上完整实现了裸机启动初始化、UART0 串口日志与中断接收、1 MiB 模拟 NOR 四分区存储、镜像结构与向量表校验、汇编控制权交接、YMODEM 双槽固件升级、RSA-2048/SHA-256 签名验证、试运行确认与 CMSDK 看门狗回退、七点写入中断恢复以及密码路径链接时优化（LTO）。

第八周正式固件 `bootloader-week7.elf` 装载体积为 **24,216 字节**（`.text` 24,208 B + `.data` 8 B，64 KiB 代码区余量 41,320 B），相对同轮关闭密码路径 LTO 对照（27,204 B）减少 **2,988 字节（10.98%）**。全量自动化验收覆盖 **14 套回归套件** 与 **Unity 七套 93 个用例**（宿主机 ASan/UBSan 82 例 + Cortex-M7 目标机 11 例），全部零失败通过。

## 核心能力一览

- **裸机启动与异常诊断**：链接脚本边界 `ASSERT` 校验、`.data` 复制与 `.bss` 清零、关闭半主机后的 UART0（115200 8N1）有界发送与中断环形队列接收、异常号与状态变量留存。
- **分区存储与安全镜像**：1 MiB 模拟 NOR（基址 `0x20300000`）划分为启动预留（64 KiB，接口层写保护）、A/B 双应用槽（各 448 KiB）与参数区（64 KiB）；32 B 镜像头（`M7IM`）+ 最大 458,448 B 应用载荷 + 272 B RSA-2048/SHA-256 签名尾（`M6SG`）；源槽校验、执行区（`0x00010000`）复制回读校验、48 项向量表边界检查与汇编交接。
- **双槽原子升级与回退**：YMODEM 单文件批次接收（防重复写入与无进展超时）、参数区双页交替提交 48 B 升级元数据（`M5BM` + `CMIT`）与 56 B 配置/试运行记录（`M6CF` + `CMIT`）；先持久化试运行意图（`CFG_TRIAL_ARMED`）再切换活动槽；新应用超时未确认由 CMSDK 看门狗触发复位并自动回退至上一已确认槽。
- **故障注入与可复现交付**：独立 `--wrap` 故障目标覆盖 7 个写入切点冷启动恢复；一键生成四场演示、全量回归与 Doxygen 离线 API 文档。

## 从空目录开始

准备 Arm 裸机工具链（`arm-none-eabi-gcc`/`binutils`/`gdb`）、QEMU（含 `mps2-an500`）、Python 3、CMake、GNU Make、OpenSSL、支持 ASan/UBSan 的宿主 C 编译器（生成 API 另需 Doxygen）。首次构建自动在 `codes/build/test-signing/` 生成本地实验签名密钥，不随源码分发私钥。完整环境版本与安装说明见 [代码工程 README](codes/README.md)。

```sh
cd codes
source scripts/env.sh          # 若工具链位于 Homebrew 默认路径
bash scripts/demo-week8.sh     # 构建正式目标并运行四场端到端演示
make verify-release            # 串行执行全部 14 套全量回归（含 ASan/UBSan）
make api                       # 可选：生成 Doxygen HTML API 文档
```

四场演示依次验证 **签名 v1 启动 → YMODEM 升级至签名 v2 并确认 → 拒绝无签名 v3 且保住 v2 → 活动 B 槽损坏后自动回退至 v1**。详细操作与串口标记见 [QEMU 最终演示指南](codes/docs/week8-demo.md)，逐套测试结果与 LTO 对照数据见 [最终测试报告](reports/week8/最终测试报告.md)。

## 最终交付导航

| 需要做什么 | 入口文档 |
|---|---|
| 阅读八周结项报告 | [结项报告 PDF](reports/week8/结项报告.pdf) · [可编辑 Word](reports/week8/结项报告.docx) · [Markdown](reports/week8/结项报告.md) |
| 核对规划与第八周索引 | [原始八周规划书](planning/Industrial%20Embedded%20Systems%20Bootloader%20Development.pdf) · [第八周交付索引](reports/week8/delivery-index.md) · [第八周实习周报](reports/week8/第八周实习周报.pdf) |
| 了解架构、模块与 API | [技术与开发说明](codes/docs/week8-technical.md) · [代码工程 README](codes/README.md) · [离线 Doxygen API](reports/week8/API参考.zip) |
| 复核测试与 LTO 测量 | [最终测试报告 PDF](reports/week8/最终测试报告.pdf) · [LTO 成对测量](reports/week8/evidence/lto-comparison.json) · [原始运行证据](reports/week8/evidence/运行证据.zip) |
| 复查各周演进记录 | [阶段进度](planning/stage-progress.md) · [各周实习周报](reports/) · [第一周学习笔记](notes/week1-learning-notes.md) |

## 工程目录结构

```text
.
├── README.md                  # 仓库总览与快速复核入口
├── AGENTS.md                  # 目录规范与维护约定
├── codes/                     # 唯一主代码工程（CMake/Makefile、源码、脚本、测试与技术文档）
│   ├── CMakeLists.txt         # 主构建配置与分周目标定义
│   ├── Makefile               # 统一验收入口（verify-release / verify-week7 / benchmark / api）
│   ├── Doxyfile               # 15 个公开头文件 API 文档配置
│   ├── README.md              # 详细构建、测试、LTO 复测与代码定位说明
│   ├── cmake/                 # 工具链配置及 Week 6/7 构建规则（含 BOOT_CRYPTO_LTO）
│   ├── src/                   # 分周演进的 Bootloader 与应用源码（最终入口 src/week7/main.c）
│   ├── scripts/               # 构建、打包、演示、基准测试与全量验收脚本
│   ├── tests/                 # 宿主机/目标机 Unity 测试、故障注入与离线第三方依赖
│   ├── schemas/               # 交付清单 JSON Schema 与校验样例
│   └── docs/                  # 架构说明、演示指南、测试与优化记录
├── reports/                   # 分周正式周报（week1-2 至 week8）及结项报告、证据与校验和
├── planning/                  # 八周规划书、符合性核查与阶段进度
└── notes/                     # 个人学习笔记
```

## 仓库整洁度与验收边界

- **干净源码树**：本仓库仅保留活跃源码、测试、技术文档与正式报告；运行期产物（`codes/build/`、`codes/logs/*`）、本地实验私钥（`*.pem`/`*.key`）、旧交付压缩包（`output/`）与历史中间文件（`archive/`）均由 `.gitignore` 排除，不随源码提交。早期周报涉及的历史日志可通过 `git show fef3e14:codes/logs/<原相对路径>` 查阅，当前有效证据以 `reports/week8/` 为准。
- **仿真与真机边界**：本工程全部自动化验证基于 QEMU MPS2-AN500 模拟平台与显式 NOR 快照完成；实体 Flash 掉电与擦写时序、真实主频/缓存下的最坏启动时间与栈高水位、物理 OpenOCD/SWD 调试、生产密钥管理及工业安全组件认证（IEC 62443-4-2）需在指定硬件上独立验收。
