# 工业嵌入式 Bootloader

本工程在 QEMU MPS2-AN500 Cortex-M7 上实现裸机启动、签名校验、UART/YMODEM 升级、双槽切换与未确认应用回退。第八周正式固件的装载体积为 **24,216 字节**，相对本轮关闭 LTO 的对照减少 **2,988 字节（10.98%）**。仿真验收覆盖正常升级、拒绝无签名包、最大容量、存储操作错误、中断恢复与看门狗复位。

## 从空目录开始

准备 Arm 裸机 GCC/binutils 和 GDB、QEMU、Python 3、CMake、GNU Make、OpenSSL 及支持 ASan/UBSan 的宿主 C 编译器。首次构建自动生成本地实验密钥。完整版本和依赖来源见[代码工程 README](codes/README.md)。

```sh
cd codes
bash scripts/demo-week8.sh
make verify-release
```

演示会重新构建并检查四场实际会话。全量验收覆盖 Week 1–7、反馈和六个补充专项；任何优化之后均从这个入口回归。查看[演示指南和串口截图](codes/docs/week8-demo.md)或[逐套测试结果](reports/week8/最终测试报告.md)。

## 最终交付

| 需要做什么 | 从哪里开始 |
|---|---|
| 阅读八周成果 | [结项报告 PDF](reports/week8/结项报告.pdf) · [可编辑 Word](reports/week8/结项报告.docx) · [Markdown](reports/week8/结项报告.md) |
| 核对计划与本周产物 | [原始八周规划书](planning/Industrial%20Embedded%20Systems%20Bootloader%20Development.pdf) · [第八周交付索引](reports/week8/delivery-index.md) · [第八周周报](reports/week8/第八周实习周报.pdf) |
| 修改代码、查接口 | [架构、模块、开发说明及工业要求对应](codes/docs/week8-technical.md) · [Doxygen 离线 API](reports/week8/API参考.zip) |
| 核对体积及安全回归 | [测试与测量报告 PDF](reports/week8/最终测试报告.pdf) · [LTO 成对测量](reports/week8/evidence/lto-comparison.json) · [原始运行证据](reports/week8/evidence/运行证据.zip) |
| 复查早期决策 | [阶段进度](planning/stage-progress.md) · [各周报告](reports/) · [第一周学习记录](notes/week1-learning-notes.md) |

## 目录与历史

`codes/` 是唯一代码工程，包含平台与协议实现、第三方源码及许可、脚本、测试和技术文档。`reports/` 保存周报和正式提交材料。`planning/` 保存原任务及阶段核查，`notes/` 保存学习记录。

交付分支从第七周代码提交 `fef3e14` 派生，保留此前 Git 历史及正式目录中已有的周报修订。早期报告中的日志地址对应当时工作区；已提交的历史文件可用 `git show fef3e14:codes/logs/<原相对路径>` 核查。当前证据以第八周索引为准。清理后的工程仅在运行时创建 build 和 logs，旧构建树、重复交付包、缓存及实验私钥均不随源码分发。原始规划书在 planning 保存一份，根目录保留同名兼容入口。

实体 Flash 掉电、板级最坏时序、生产密钥和 OpenOCD/SWD 需在指定硬件上验收。仿真测试和带教签收分别记录。
