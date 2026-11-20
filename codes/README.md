# Cortex-M7 启动器工程

本目录包含在 QEMU MPS2-AN500 上运行的裸机 C/汇编启动器、签名打包工具、串口升级器以及主机和目标机验收。第八周沿用 `bootloader-week7.elf` 正式目标。操作命令均从本目录执行。

## 环境与首次运行

使用 macOS 或提供 Bash、POSIX 套接字的类 Unix 环境。本次验证环境为 QEMU 11.1.2（含 mps2-an500）、GNU Arm GCC 16.2.0、GNU Make 4.4.1、CMake 4.4.4、Python 3.12、支持 ASan/UBSan 的宿主 C 编译器、arm-none-eabi-gdb 与 OpenSSL。生成 API 另需 Doxygen，本次为 1.18.0。Arm 工具链需同时提供 objcopy、size、nm、readelf 等工具。Unity、YMODEM 和 Mbed TLS 已随源码提供。[依赖来源与许可证](tests/third_party/README.md)

在配有 Homebrew 的 macOS 上可用 `bash scripts/install-tools.sh` 安装已有工具链脚本所列依赖，另外准备 Python 3、宿主编译器、OpenSSL 和 Doxygen；或将各工具加入 PATH。首次构建会生成当前工程自己的实验 RSA 密钥与公钥头，保存在 build/test-signing。新目录中的固件须搭配新目录签出的包。

```sh
source scripts/env.sh
bash scripts/build-week7.sh
bash scripts/demo-week8.sh
make verify-release
```

build/week7 下的 ELF 含符号，可作 QEMU 内核与 GDB 符号文件；application-*.bin 为裸应用，application-*.pkg 为可上传的签名包。模拟存储快照另为 1 MiB 文件。源输入不会被演示覆盖。操作员命令与四场演示的期望标记见[最终操作指南](docs/week8-demo.md)。

## 构建与测试入口

| 命令 | 用途 |
|---|---|
| make verify-release | 工具链冒烟、Week 1–7、反馈、交接、CLI、IRQ、TX 满、波特率及向量边界；串行、失败即停；宿主 UBSan 报告即失败 |
| make verify-week7 | 重新构建正式与故障固件，运行 Unity 和签名启动、升级、异常恢复集成 |
| make verify-all | 历史兼容入口，执行 Week 1–5 与反馈回归 |
| make benchmark-week7 | 固定 QEMU icount，用两类载荷各运行三次，保存时间、段统计和哈希 |
| make debug-week7 | 在调试目标的激活边界观察元数据与试运行顺序 |
| make api | 生成 build/api/html/index.html |

每次改变优化选项、链接脚本或编译器后运行 `make verify-release`，性能另测。保留符号与 map 文件以便复核。典型验收日志位于 logs/，以对应 latest 指针定位本轮目录，读取 summary.json 的最终状态；工具链首次冒烟使用 STATUS.txt。禁用 Python 断言的环境会被全量入口拒绝。生成目录可在结束所有 QEMU 进程后移走，保留需要复用的本地实验密钥与快照。

## 复测 LTO 对照

```sh
cmake -S . -B build/no-lto -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DBOOT_CRYPTO_LTO=OFF
bash scripts/build-week7.sh build/no-lto
bash scripts/build-week7.sh build/week7
arm-none-eabi-size build/no-lto/bootloader-week7.elf build/week7/bootloader-week7.elf
python3 scripts/benchmark-week7.py --build build/no-lto --out logs/comparison-off
python3 scripts/benchmark-week7.py --build build/week7 --out logs/comparison-on
```

使用两个新构建目录，确保 week7 使用默认 ON。text + data 衡量需装载的体积；bss 包含栈预留。时间是模型计数，适合相同输入与配置的项目内比较。

## 代码与定位

| 位置 | 内容 |
|---|---|
| src、cmake | 分周演进的正式与调试目标；最终入口 src/week7/main.c，签名与配置来自 week6，传输与元数据来自 week5 |
| scripts | 构建、运行、打包、演示、性能测量与验收 |
| tests、schemas | C 合同测试、Unity、故障模型、离线依赖和交付清单格式 |
| docs | [需求、架构与接口](docs/week8-technical.md)、[演示与故障定位](docs/week8-demo.md)，以及各周技术记录 |
| ../reports/week8 | [最终测试报告](../reports/week8/最终测试报告.md)、[结项报告](../reports/week8/结项报告.pdf)、[离线 API](../reports/week8/API参考.zip) |

历史目标按周独立保留，旧周文档记录当时的配置与路径。当前工程用仿真 NOR 显式保存数据，GDB 连接 QEMU gdbstub；真实物理 Flash、SWD/JTAG 与量产签名服务的验收条件见技术说明。
