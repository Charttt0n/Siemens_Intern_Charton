# 第八周交付索引

| 计划与待办 | 交付及验收 |
|---|---|
| 最终代码、标准注释、Doxygen | [操作说明](../../codes/README.md)、[15 个公开头文件的生成配置](../../codes/Doxyfile)、[离线 HTML API 压缩包](API参考.zip)；解压后打开 html/index.html |
| 需求分析、架构、模块与开发说明 | [技术说明](../../codes/docs/week8-technical.md)，包括 DDI 0489B 章节、S7 公开资料与工业需求适用性表，补齐第一周的文档待办 |
| 构建、启动、升级、签名拒绝与故障恢复演示 | [逐步指南](../../codes/docs/week8-demo.md)、[复现脚本](../../codes/scripts/demo-week8.sh)、[四场 UART 原始日志](evidence/demo/) |
| 进一步压缩代码、每次优化后全量回归 | [最终测试报告 PDF](最终测试报告.pdf) · [Word](最终测试报告.docx) · [逐项证据链接](最终测试报告.md)、[成对测量和原始证据](evidence/)，回归入口 make verify-release |
| 八周项目总结 | [PDF](结项报告.pdf)、[Word](结项报告.docx)、[可编辑文本与图](结项报告.md) |
| 本周周报 | [PDF](第八周实习周报.pdf)、[Word](第八周实习周报.docx)、[Markdown](week8-report.md) |
| 干净的工程与历史 | [项目 README](../../README.md)、独立 week8-delivery 分支；提交历史可用 git log 查看 |

## 外部验收条件

本轮采用 QEMU gdbstub 完成断点与状态核查。实物 OpenOCD/SWD、Flash 擦写和断电时序、生产信任根及带教签收仍需要相应设备或责任人。公开发布本项目自有代码的许可由权利人确定；第三方版权和许可按原件保留。


## 本地审核版本

按交付要求保留在本机 week8-delivery 分支，远端发布待审核。全新目录的 14 套回归、四场演示、从空构建目录生成 API、结项报告的字数及页数列在[交付复验](evidence/clean-check.json)。全部归档文件的校验值见 [SHA256SUMS](SHA256SUMS)。
