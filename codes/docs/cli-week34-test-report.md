# Week 3–4 一键入口测试报告

## Week 3–4 一键入口专项测试

本次直接执行交付指南中的构建和运行命令，22项检查全部通过。证据根目录 CLI34LOG = `logs/cli-week34-20261024-234802`（相对 codes/）；逐项日志包含命令、完整输出、退出码、耗时、QEMU PID、临时目录及文件锁检查，汇总见该目录 summary.json。

复验：在 codes/ 执行 `python3 scripts/verify-cli-week34.py`。脚本为两个阶段分别创建排除 build、logs 的临时工程副本；不删除当前工程产物，也不修改当前实验镜像。每个阶段实际执行 `bash scripts/build-weekN.sh` 和 `python3 scripts/run-weekN.py`，其中 N 为 3 或 4。

依赖注入仅发生在测试副本：QEMU审计包装器记录PID后，以原参数exec真实QEMU；失败场景让该依赖返回42；启动超时场景使用不创建socket且忽略SIGTERM的模拟进程。UART超时场景使用真实QEMU与无UART输出的无限循环固件。CMake失败通过副本env.sh中的函数注入，分别覆盖配置和构建。没有将模拟依赖当作真实固件测试。

启动及UART内部超时为5秒，清理先等待SIGTERM最多3秒，再SIGKILL并等待回收；测试外层20秒看门狗仅用于兜底，本轮22项均未触发。每个启动子进程的用例都验证其PID不存在、QMP/UART所在临时目录删除、镜像文件锁可重新独占获取。正常运行后再做恢复运行，确认失败不阻碍下一次启动。锁文件允许留在磁盘上，检查的是锁已释放。

退出码约定：构建成功0；CMake配置/构建失败保留42；运行成功0；缺少必需产物、QEMU启动失败或内部超时返回1。Week3默认镜像不存在是合法首次运行，会新建1MiB镜像并退出0；Week4镜像缺失退出1。本次不新增对SIGKILL等不可捕获外部强制终止的自动清理保证。

### Week 3 执行表
| 固定编号 | 实际命令 | 操作步骤 | 具体输入 |
|---|---|---|---|
| CLI34-W3-MISSING | python3 scripts/run-week3.py | 复制工程时排除 build；直接运行默认命令 | ELF 和镜像均不存在 |
| CLI34-W3-BUILD | bash scripts/build-week3.sh | 确认不存在 build；执行交付构建命令 | 原始工程，无构建缓存 |
| CLI34-W3-RUN | python3 scripts/run-week3.py | 构建后执行默认运行命令；检查输出及清理 | p、save、quit |
| CLI34-W3-MISSING-ELF | python3 scripts/run-week3.py | 保留镜像；仅移走ELF；直接运行 | ELF不存在 |
| CLI34-W3-NEW-IMAGE | python3 scripts/run-week3.py | 保留ELF；移走镜像；执行并输入quit | 默认镜像不存在；quit |
| CLI34-W3-QEMU-FAIL | python3 scripts/run-week3.py | 只在副本替换QEMU启动依赖；执行原运行命令 | 子命令返回42 |
| CLI34-W3-START-TIMEOUT | python3 scripts/run-week3.py | 只在副本替换QEMU启动依赖；执行原运行命令 | 子进程不建socket且忽略SIGTERM |
| CLI34-W3-UART-TIMEOUT | python3 scripts/run-week3.py | 仅将副本ELF换成无限循环固件；运行真实QEMU | 有效ELF，无UART输出 |
| CLI34-W3-FAIL-CONFIGURE | bash scripts/build-week3.sh | 保留旧产物；注入cmake失败；运行交付构建命令 | cmake configure 返回42 |
| CLI34-W3-FAIL-BUILD | bash scripts/build-week3.sh | 保留旧产物；注入cmake失败；运行交付构建命令 | cmake build 返回42 |
| CLI34-W3-RECOVERY | python3 scripts/run-week3.py | 恢复依赖和ELF后再次运行，检验无残留锁/进程干扰 | 原有效ELF和镜像；Week3输入quit |

### Week 3 结果与证据
| 固定编号 | 预期输出及退出码 | 实际输出及清理 | 判定 | 日志路径 |
|---|---|---|---|---|
| CLI34-W3-MISSING | 退出1；不启动QEMU | exit=1；0.038秒；外层看门狗未触发。未启动QEMU。缺少ELF | PASS | CLI34LOG/CLI34-W3-MISSING.log |
| CLI34-W3-BUILD | 退出0，生成该阶段ELF及所需镜像 | exit=0；0.444秒；外层看门狗未触发。未启动QEMU。构建产物存在 | PASS | CLI34LOG/CLI34-W3-BUILD.log |
| CLI34-W3-RUN | 退出0；固件成功标记；QEMU退出、临时目录删除、文件锁释放 | exit=0；1.706秒；外层看门狗未触发。子进程已回收；临时目录已删除；文件锁已释放。PONG / Saved | PASS | CLI34LOG/CLI34-W3-RUN.log |
| CLI34-W3-MISSING-ELF | 退出1；ELF not found；不启动QEMU | exit=1；0.034秒；外层看门狗未触发。未启动QEMU。ELF not found | PASS | CLI34LOG/CLI34-W3-MISSING-ELF.log |
| CLI34-W3-NEW-IMAGE | Week3允许首次创建；退出0；保存1MiB镜像且清理QEMU | exit=0；0.093秒；外层看门狗未触发。子进程已回收；临时目录已删除；文件锁已释放。Saved；镜像1048576字节 | PASS | CLI34LOG/CLI34-W3-NEW-IMAGE.log |
| CLI34-W3-QEMU-FAIL | 入口退出1；无成功标记；清理子进程、临时目录和文件锁 | exit=1；0.055秒；外层看门狗未触发。子进程已回收；临时目录已删除；文件锁已释放。QEMU exited before opening sockets | PASS | CLI34LOG/CLI34-W3-QEMU-FAIL.log |
| CLI34-W3-START-TIMEOUT | 入口退出1；无成功标记；清理子进程、临时目录和文件锁 | exit=1；8.057秒；外层看门狗未触发。子进程已回收；临时目录已删除；文件锁已释放。QEMU socket startup timeout | PASS | CLI34LOG/CLI34-W3-START-TIMEOUT.log |
| CLI34-W3-UART-TIMEOUT | 约5秒内部UART超时；入口退出1；QEMU和资源清理完成 | exit=1；5.096秒；外层看门狗未触发。子进程已回收；临时目录已删除；文件锁已释放。ERROR: timed out | PASS | CLI34LOG/CLI34-W3-UART-TIMEOUT.log |
| CLI34-W3-FAIL-CONFIGURE | 退出42；不被旧产物掩盖；不启动QEMU | exit=42；0.026秒；外层看门狗未触发。未启动QEMU。CMake配置失败 | PASS | CLI34LOG/CLI34-W3-FAIL-CONFIGURE.log |
| CLI34-W3-FAIL-BUILD | 退出42；不被旧产物掩盖；不启动QEMU | exit=42；0.057秒；外层看门狗未触发。未启动QEMU。CMake构建失败 | PASS | CLI34LOG/CLI34-W3-FAIL-BUILD.log |
| CLI34-W3-RECOVERY | 退出0并完成清理 | exit=0；0.089秒；外层看门狗未触发。子进程已回收；临时目录已删除；文件锁已释放。Saved | PASS | CLI34LOG/CLI34-W3-RECOVERY.log |

### Week 4 执行表
| 固定编号 | 实际命令 | 操作步骤 | 具体输入 |
|---|---|---|---|
| CLI34-W4-MISSING | python3 scripts/run-week4.py | 复制工程时排除 build；直接运行默认命令 | ELF 和镜像均不存在 |
| CLI34-W4-BUILD | bash scripts/build-week4.sh | 确认不存在 build；执行交付构建命令 | 原始工程，无构建缓存 |
| CLI34-W4-RUN | python3 scripts/run-week4.py | 构建后执行默认运行命令；检查输出及清理 | 默认有效镜像 |
| CLI34-W4-MISSING-ELF | python3 scripts/run-week4.py | 保留镜像；仅移走ELF；直接运行 | ELF不存在 |
| CLI34-W4-MISSING-IMAGE | python3 scripts/run-week4.py | 保留ELF；移走镜像；直接运行 | 默认镜像不存在 |
| CLI34-W4-QEMU-FAIL | python3 scripts/run-week4.py | 只在副本替换QEMU启动依赖；执行原运行命令 | 子命令返回42 |
| CLI34-W4-START-TIMEOUT | python3 scripts/run-week4.py | 只在副本替换QEMU启动依赖；执行原运行命令 | 子进程不建socket且忽略SIGTERM |
| CLI34-W4-UART-TIMEOUT | python3 scripts/run-week4.py | 仅将副本ELF换成无限循环固件；运行真实QEMU | 有效ELF，无UART输出 |
| CLI34-W4-FAIL-CONFIGURE | bash scripts/build-week4.sh | 保留旧产物；注入cmake失败；运行交付构建命令 | cmake configure 返回42 |
| CLI34-W4-FAIL-BUILD | bash scripts/build-week4.sh | 保留旧产物；注入cmake失败；运行交付构建命令 | cmake build 返回42 |
| CLI34-W4-RECOVERY | python3 scripts/run-week4.py | 恢复依赖和ELF后再次运行，检验无残留锁/进程干扰 | 原有效ELF和镜像；Week3输入quit |

### Week 4 结果与证据
| 固定编号 | 预期输出及退出码 | 实际输出及清理 | 判定 | 日志路径 |
|---|---|---|---|---|
| CLI34-W4-MISSING | 退出1；不启动QEMU | exit=1；0.037秒；外层看门狗未触发。未启动QEMU。image missing | PASS | CLI34LOG/CLI34-W4-MISSING.log |
| CLI34-W4-BUILD | 退出0，生成该阶段ELF及所需镜像 | exit=0；0.413秒；外层看门狗未触发。未启动QEMU。构建产物存在 | PASS | CLI34LOG/CLI34-W4-BUILD.log |
| CLI34-W4-RUN | 退出0；固件成功标记；QEMU退出、临时目录删除、文件锁释放 | exit=0；0.226秒；外层看门狗未触发。子进程已回收；临时目录已删除；文件锁已释放。APP: RUNNING | PASS | CLI34LOG/CLI34-W4-RUN.log |
| CLI34-W4-MISSING-ELF | 退出1；ELF not found；不启动QEMU | exit=1；0.033秒；外层看门狗未触发。未启动QEMU。ELF not found | PASS | CLI34LOG/CLI34-W4-MISSING-ELF.log |
| CLI34-W4-MISSING-IMAGE | 退出1；image missing；不启动QEMU | exit=1；0.033秒；外层看门狗未触发。未启动QEMU。image missing | PASS | CLI34LOG/CLI34-W4-MISSING-IMAGE.log |
| CLI34-W4-QEMU-FAIL | 入口退出1；无成功标记；清理子进程、临时目录和文件锁 | exit=1；0.06秒；外层看门狗未触发。子进程已回收；临时目录已删除；文件锁已释放。QEMU exited before opening sockets | PASS | CLI34LOG/CLI34-W4-QEMU-FAIL.log |
| CLI34-W4-START-TIMEOUT | 入口退出1；无成功标记；清理子进程、临时目录和文件锁 | exit=1；8.052秒；外层看门狗未触发。子进程已回收；临时目录已删除；文件锁已释放。QEMU socket startup timeout | PASS | CLI34LOG/CLI34-W4-START-TIMEOUT.log |
| CLI34-W4-UART-TIMEOUT | 约5秒内部UART超时；入口退出1；QEMU和资源清理完成 | exit=1；5.086秒；外层看门狗未触发。子进程已回收；临时目录已删除；文件锁已释放。ERROR: timed out | PASS | CLI34LOG/CLI34-W4-UART-TIMEOUT.log |
| CLI34-W4-FAIL-CONFIGURE | 退出42；不被旧产物掩盖；不启动QEMU | exit=42；0.025秒；外层看门狗未触发。未启动QEMU。CMake配置失败 | PASS | CLI34LOG/CLI34-W4-FAIL-CONFIGURE.log |
| CLI34-W4-FAIL-BUILD | 退出42；不被旧产物掩盖；不启动QEMU | exit=42；0.048秒；外层看门狗未触发。未启动QEMU。CMake构建失败 | PASS | CLI34LOG/CLI34-W4-FAIL-BUILD.log |
| CLI34-W4-RECOVERY | 退出0并完成清理 | exit=0；0.087秒；外层看门狗未触发。子进程已回收；临时目录已删除；文件锁已释放。APP: RUNNING | PASS | CLI34LOG/CLI34-W4-RECOVERY.log |
