# 西门子实习工作路径

本交付分支以当前 Git 检出目录为项目根目录。

- `codes/` 是唯一主代码工程，Git 仓库位于项目根目录；构建、运行和验证均从此目录进行。
- 周报只在 `reports/week1-2/`、`reports/week3/`、`reports/week4/` 等对应周次维护，Word 与 Markdown 应保持一致，不在 codes/docs 中放置周报副本。
- `codes/docs/` 保留架构说明、操作指南和测试报告；个人学习笔记放在 `notes/`。
- 实习任务与规划放在 `planning/`。
- `archive/` 保存旧版本与生成中间文件，不作为活跃代码或周报编辑入口；其中历史脚本不保证可直接运行。
- 干净交付分支只保留活跃工程与正式材料。历史归档在原开发目录和既有 Git 历史中查阅。
- 项目根目录的 Git 统一管理 codes、reports、planning、notes；archive、构建产物、下载包及缓存不上传。远程仓库为 https://github.com/Charttt0n/Siemens_Intern_Charton.git。

- 现有周报按规划书归档：`reports/week1-2/`、`reports/week3/` 和 `reports/week4/`；后续逐周归档。后续严格按规划书逐周推进：先完成当前阶段反馈修改与验证，提交更新报告，待老师确认后再进入下一阶段。

- 2026-10-25按用户要求将Week3、Week4严格分周：Week3归UART/Flash/分区与持久化；Week4归CRC/镜像/应用加载与交接。旧reports/week3-4已归档，不恢复为活跃入口；每周交付物与证据见planning/week3-week4-delivery-index.md。
