# Week 4 测试用例与验收报告

更新日期：2026-10-25。本轮为老师反馈修订，待老师审核。历史记录保留；下列新证据为本轮判定依据。测试中的预期拒绝不等于套件失败。

入口：在 codes/ 执行 make verify-week4。证据目录：`logs/week4-20261024-173015-915034`。完整步骤及输入保存在对应 .gdb／命令日志／验证脚本；摘要记录本轮判定。

验证路径：干净构建 → 指定新 ELF → 输入或故障注入 → 输出与状态断言 → 原始日志 → summary.json。未出现预期输出、断言失败或意外超时均判失败。以下按固定编号连接执行表和结果表。基础镜像表沿用2026-10-24及2026-10-25已执行证据；新增向量和状态专项实测见后文。文档编辑与实际测试分开记录。

## 执行表

日志根目录：`W4LOG = logs/week4-20261024-173015-915034`（相对 codes/）。表内路径由该根目录和文件名组成。

| 固定编号 | 测试方式 | 操作步骤 | 具体输入 |
|---|---|---|---|
| W4-BUILD | 构建/地址检查 | 1. 新目录构建Bootloader和独立应用；2. objcopy生成application.bin；3. 检查两ELF反汇编、符号及栈/向量地址。 | 独立Boot/App ELF；固定栈、VTOR和链接段 |
| W4-CRC | 独立zlib oracle及sanitizer | 1. 编译真实crc32.c和test_crc32.c，启用sanitizer；2. 用固定随机种子生成8种长度输入；3. 逐次运行；4. 将结果与独立zlib.crc32比较。 | 长度0,1,9,255,256,257,4096,458720，种子20261022；标准串123456789 |
| W4-HANDOFF | GDB真实CPSID后注入 | 1. 在boot_handoff停下并实际执行CPSID；2. 读回PRIMASK=1后注入挂起状态；3. 在应用Reset入口检查寄存器；4. 污染应用data/bss和设置哨兵；5. 运行到app_idle，检查CRT、交接、SVC状态。 | NVIC IRQ7、SysTick、PendSV挂起；应用data/bss污染 |
| W4-COPY | GDB复制后翻转 | 1. 正常镜像启动；2. 在boot_verify_loaded入口断点暂停；3. 翻转运行地址0x100c8一位；4. 继续到boot_idle；5. 检查boot_status和完整串口字节。 | 运行地址0x100c8一个字节异或1 |
| W4-DIAG | 模型日志检查 | 1. 遍历镜像场景及GDB场景的qemu.log；2. 排除主动终止消息；3. 断言没有其他guest-error。 | 所有正常/拒绝场景qemu.log |

## 结果与证据表

| 固定编号 | 预期输出 | 实际输出 | 判定 | 日志路径（根目录见上） |
|---|---|---|---|---|
| W4-BUILD | 无BKPT，地址符合 | 断言通过 | PASS | W4LOG/build.txt；W4LOG/summary.json |
| W4-CRC | CRC与zlib逐一相等；标准串cbf43926 | 全部相等，无sanitizer错误 | PASS | W4LOG/summary.json；W4LOG/summary.json |
| W4-HANDOFF | 入口MSP=20200000、VTOR=10000、PRIMASK=1；清中断；应用初始化恢复且SVC=1 | APP MSP VTOR masks NVIC SysTick CRT sentinel SVC: PASS | PASS | W4LOG/dirty-state-handoff-and-app-crt.txt；W4LOG/summary.json |
| W4-COPY | 状态8，REJECT 00000008，无应用日志 | boot_status=8；输出符合 | PASS | W4LOG/reject-corrupted-loaded-copy.txt；W4LOG/summary.json |
| W4-DIAG | 无guest error | 检查通过 | PASS | W4LOG/summary.json；W4LOG/summary.json |


头起点为镜像偏移0x10000，payload起点为0x10020。22种镜像输入的SHA256、完整头和48项向量十六进制值在summary.json/image_cases中。向量负向输入修复CRC后再测试，避免CRC错误遮蔽向量检测。CRC检查不认证来源；备份槽尚不用于启动。

## 镜像输入逐项对照

本次按老师要求将原有18种输入拆为18行主表，已增加的4种边界输入列为补充表，保留IMG编号。表格依据已通过的 `logs/week4-20261025-015917-068252`，沿用实际执行结果，不将文档编辑算作重新测试。构造输入经重新还原，22份镜像SHA256均与原始summary.json一致；具体数值已与头、向量、宿主输出及QEMU UART逐项核对。

偏移均以整个1 MiB镜像文件起点为0；H = 0x00010000为镜像头起点，P = H + 0x20 = 0x00010020为payload/向量表起点。除注明“一个字节”及全文件/填充区外，修改均为小端uint32；区间均左闭右开。每行从同一有效基准独立构造，不将前一行修改带到下一行。基准payload为972字节，固件版本1。

“头重算”指对[H,H+0x1C)计算CRC32并写至0x0001001C；“内容CRC”对声明的payload计算并写至0x00010018。向量修改先重算内容CRC再重算头CRC，以免较早的CRC错误遮蔽目标向量错误。头字段负向输入只重算头CRC；头CRC错误及内容损坏用例保留旧CRC。

IMGLOG = `logs/week4-20261025-015917-068252`（相对codes/）。表内N为宿主image_validate的IMAGE_RESULT，不是宿主进程退出码；预期拒绝时测试程序断言匹配后退出0。Q为QEMU UART实际结果，“拒绝0xNN”对应完整REJECT十六进制行且没有APP: RUNNING；成功须出现APP: RUNNING。每行日志均在IMGLOG根目录下，前者为宿主输出，后者为UART。详细CRC数值、实际SIZE、输入SHA256和完整UART另存codes/docs/week4-image-inputs.json。

错误码对应：0=接受；2=格式；3=头CRC；4=长度；5=加载地址；6=内容CRC；7=向量。复制后运行区损坏对应8，属于W4-COPY的另一类GDB故障注入，不计入这18+4种输入。CRC只检测损坏，不证明固件来源可信。

<!-- landscape -->

### 原有18种镜像输入

| 编号与场景 | 修改偏移 | 具体数值 | CRC处理 | 预期错误码 | 实际结果与判定 | 日志（相对IMGLOG） |
|---|---|---|---|---|---|---|
| IMG-01 valid | 无修改 | 有效基准；version=1；payload_size=972 | 打包时计算两CRC | 0 (IMAGE_OK) | N=0；Q=APP: RUNNING；PASS | valid-native.txt；valid/uart.log |
| IMG-02 blank | 全文件[0x00000000,0x00100000) | 每字节=0xFF；1 MiB | 均不计算 | 0x02 | N=2；Q=拒绝0x02；PASS | blank-native.txt；blank/uart.log |
| IMG-03 bad-magic | H+0x00 = 0x00010000 | magic：0x4D49374D → 0x00000000 | 头重算；内容保留 | 0x02 | N=2；Q=拒绝0x02；PASS | bad-magic-native.txt；bad-magic/uart.log |
| IMG-04 unsupported-format | H+0x04 = 0x00010004 | format：1 → 2 | 头重算；内容保留 | 0x02 | N=2；Q=拒绝0x02；PASS | unsupported-format-native.txt；unsupported-format/uart.log |
| IMG-05 bad-header-size | H+0x08 = 0x00010008 | header_size：32 → 64 | 头重算；内容保留 | 0x02 | N=2；Q=拒绝0x02；PASS | bad-header-size-native.txt；bad-header-size/uart.log |
| IMG-06 bad-header-crc | H+0x14 = 0x00010014 | firmware_version：1 → 8；保留旧头CRC | 均不重算 | 0x03 | N=3；Q=拒绝0x03；PASS | bad-header-crc-native.txt；bad-header-crc/uart.log |
| IMG-07 empty-payload | H+0x0C = 0x0001000C | payload_size：972 → 0 | 头重算；内容保留 | 0x04 | N=4；Q=拒绝0x04；PASS | empty-payload-native.txt；empty-payload/uart.log |
| IMG-08 oversize-payload | H+0x0C = 0x0001000C | payload_size：972 → 0xFFFFFFFF | 头重算；内容保留 | 0x04 | N=4；Q=拒绝0x04；PASS | oversize-payload-native.txt；oversize-payload/uart.log |
| IMG-09 wrong-load-address | H+0x10 = 0x00010010 | load_address：0x00010000 → 0x20000000 | 头重算；内容保留 | 0x05 | N=5；Q=拒绝0x05；PASS | wrong-load-address-native.txt；wrong-load-address/uart.log |
| IMG-10 payload-corruption | P+0xC8 = 0x000100E8 | 一个字节：0x91 → 0x90（异或1） | 均不重算 | 0x06 | N=6；Q=拒绝0x06；PASS | payload-corruption-native.txt；payload-corruption/uart.log |
| IMG-11 unaligned-msp | P+0x00 = 0x00010020 | vector[0]：0x20200000 → 0x20200004 | 先内容CRC，再头CRC | 0x07 | N=7；Q=拒绝0x07；PASS | unaligned-msp-native.txt；unaligned-msp/uart.log |
| IMG-12 boot-ram-msp | P+0x00 = 0x00010020 | vector[0]：0x20200000 → 0x20010000 | 先内容CRC，再头CRC | 0x07 | N=7；Q=拒绝0x07；PASS | boot-ram-msp-native.txt；boot-ram-msp/uart.log |
| IMG-13 even-reset | P+0x04 = 0x00010024 | vector[1]：0x000100C1 → 0x000100C0 | 先内容CRC，再头CRC | 0x07 | N=7；Q=拒绝0x07；PASS | even-reset-native.txt；even-reset/uart.log |
| IMG-14 outside-reset | P+0x04 = 0x00010024 | vector[1]：0x000100C1 → 0x00080001 | 先内容CRC，再头CRC | 0x07 | N=7；Q=拒绝0x07；PASS | outside-reset-native.txt；outside-reset/uart.log |
| IMG-15 vector-table-reset | P+0x04 = 0x00010024 | vector[1]：0x000100C1 → 0x00010001 | 先内容CRC，再头CRC | 0x07 | N=7；Q=拒绝0x07；PASS | vector-table-reset-native.txt；vector-table-reset/uart.log |
| IMG-16 bad-irq-vector | P+0x40 = 0x00010060 | vector[16]：0x00010105 → 0x00000000 | 先内容CRC，再头CRC | 0x07 | N=7；Q=拒绝0x07；PASS | bad-irq-vector-native.txt；bad-irq-vector/uart.log |
| IMG-17 reserved-vector | P+0x1C = 0x0001003C | vector[7]：0x00000000 → 0x000100C1 | 先内容CRC，再头CRC | 0x07 | N=7；Q=拒绝0x07；PASS | reserved-vector-native.txt；reserved-vector/uart.log |
| IMG-22 maximum-payload | H+0x0C；文件[0x000103EC,0x00080000) | 长度972 → 458720 (0x6FFE0)；扩展部分填0xFF | 重新打包计算两CRC | 0 (IMAGE_OK) | N=0；Q=APP: RUNNING；PASS | maximum-payload-native.txt；maximum-payload/uart.log |

### 新增4种边界输入

| 编号与场景 | 修改偏移 | 具体数值 | CRC处理 | 预期错误码 | 实际结果与判定 | 日志（相对IMGLOG） |
|---|---|---|---|---|---|---|
| IMG-18 msp-above-top | P+0x00 = 0x00010020 | vector[0]：0x20200000 → 0x20200008 | 先内容CRC，再头CRC | 0x07 | N=7；Q=拒绝0x07；PASS | msp-above-top-native.txt；msp-above-top/uart.log |
| IMG-19 msp-below-base | P+0x00 = 0x00010020 | vector[0]：0x20200000 → 0x2000FFF8 | 先内容CRC，再头CRC | 0x07 | N=7；Q=拒绝0x07；PASS | msp-below-base-native.txt；msp-below-base/uart.log |
| IMG-20 reset-at-end | P+0x04 = 0x00010024 | vector[1]：0x000100C1 → 0x000103CD；(0x10000+972)按位或1 | 先内容CRC，再头CRC | 0x07 | N=7；Q=拒绝0x07；PASS | reset-at-end-native.txt；reset-at-end/uart.log |
| IMG-21 irq-before-code | P+0x40 = 0x00010060 | vector[16]：0x00010105 → 0x0000FFFF | 先内容CRC，再头CRC | 0x07 | N=7；Q=拒绝0x07；PASS | irq-before-code-native.txt；irq-before-code/uart.log |

<!-- portrait -->

## 向量合法范围与边界覆盖

本工程固定AN500、48项向量和192字节向量表。范围规则来自src/week4/image.c的vectors_ok，image_validate与boot_verify_loaded共同调用；宿主打包器scripts/image_format.py实施相同的输入规则。下表是本工程镜像契约，不将其扩展为所有Cortex-M芯片的统一地址规则。

| 检查对象 | 精确规则 | 边界含义 | 验证与证据 |
|---|---|---|---|
| 向量表位置 | payload复制到0x00010000；向量表[0x10000,0x100C0)，共48项 | load必须为固定地址；表本身按256字节对齐 | W4-BUILD、W4-HANDOFF；链接与GDB证据 |
| 初始MSP vector[0] | 0x20010000 < MSP <= 0x20200000；MSP模8=0 | 下界不接受；上界接受；保留Bootloader RAM | VEC-001..015；MSP-AT-BASE |
| Reset vector[1] | entry位0必须1；addr=entry按位与0xFFFFFFFE | Thumb位与地址分别检查，不按4字节对齐处理 | Reset索引7个边界；RESET-BELOW |
| 所有已实现异常及IRQ入口 | 0x100C0 <= addr < 0x10000+payload_size，且entry位0=1 | 代码下界接受；上界不接受；零入口拒绝 | 42个必填索引各7项；VEC-MATRIX |
| 核心保留项 | 索引7、8、9、10、13必须为0 | 每一项单独验证0可接受、非0拒绝 | 5项各2个断言 |
| 外部IRQ | 索引16..47对应IRQ0..31，每项必须为非零合法Thumb入口 | 未使用IRQ也要配置合法默认处理函数 | 全部32项都测，不仅测IRQ0 |
| 复制后的复核 | 先CRC，再用同一规则检查MSP及全部入口 | 先修正测试CRC才能直接触达向量拒绝分支 | LOAD-VECTOR-MSP、LOAD-VECTOR-IRQ31 |

新证据VLOG = `logs/vector-bounds-20261025-021521`（相对codes/）。`python3 scripts/verify-vector-bounds.py`在干净构建中执行321个宿主断言（ASan/UBSan），再做4个CPU镜像拒绝、2个复制后向量复核及2个状态路径验证。完整321项的输入、预期、实际、文件行号见codes/docs/vector-boundary-cases.json；原始输出为VLOG/matrix.txt。

| 覆盖类别 | 输入及预期 | 本轮结果 |
|---|---|---|
| MSP上下界 | 0、base-8、base、base+8、top-8、top、top+8、0xFFFFFFF8 | 只有base+8、top-8、top接受；8项PASS |
| MSP对齐 | base+8再分别加1..7 | 7种余数全部返回7；PASS |
| 每个必填入口 | 最低合法Thumb地址、低于代码区、最高内部地址、等于末端、超过末端、清Thumb位、零 | 42×7=294项PASS |
| 每个保留项 | 零与0x100C1 | 5×2=10项PASS |
| 表长边界 | payload=191、192 | 191返回长度错误4；192无代码可容纳合法入口，返回向量错误7；2项PASS |
| CPU拒绝 | MSP恰为RAM下界、保留项13非零、IRQ31恰到末端、Reset低于代码区 | 各输出REJECT: 0x00000007；4项PASS |
| 加载后复核 | 运行区MSP改为下界、IRQ31入口改零；更新boot_image.crc以越过CRC检查 | 两项均在boot_verify_loaded返回7；PASS |

对于基准972字节payload，代码起始地址范围为[0x100C0,0x103CC)；最低合法entry为0x100C1，最高合法entry为0x103CB，entry=0x103CD解码为末端，拒绝。接受边界值的宿主用例只验证地址判定，没有把修改成任意内部地址的镜像启动执行。当前规则只检查入口起始地址、Thumb位及范围，不证明该地址上的完整指令或函数语义有效；实际应用运行证据仍来自未修改的合法应用。最低可执行镜像还须包含代码，不能把192字节长度字段检查通过解释为整个镜像通过。

<!-- landscape -->

## 完整状态转移图

![完整启动状态及失败去向](images/boot-state-complete.png)

<!-- portrait -->

图中所有Bootloader失败最终都进入boot_idle循环，应用自己的成功或失败最终进入app_idle。当前没有备份槽切换、重试下载或自动恢复分支。完整可编辑图源为codes/docs/images/boot-state-complete.mmd，渲染脚本为codes/scripts/render-boot-state.py。图中“通过”表示继续下一步，不表示最终应用运行成功。

boot_status不是进度枚举：它在BSS初始化时为0，成功交接前也会被写为0；中间校验阶段没有逐状态写入。判断应用成功须结合APP: RUNNING、app_status=1及交接/CRT/SVC检查。应用出错时boot_status可能仍为0，应读取app_status和app_exception。

## REJECT 输出与状态字典

src/week4/main.c的reject(result)先把result写入boot_status，再依次输出前缀、十六进制码、CRLF和提示符，最后进入boot_idle。精确格式如下，其中h为小写十六进制数字；引号表示字符串边界，不是发送字节：

```text
"REJECT: 0xhhhhhhhh\r\n> "
```

前缀中的空格、0x、恰好8位十六进制和末尾提示符后的空格都是输出的一部分。`\r\n`表示CR/LF两个字节，不是发送反斜线字符；提示符之后没有额外换行。唯一字段是无符号32位错误码，来源是image_result，不含地址、时间、序号，也不是宿主进程退出码。

| boot_status值 | 名称与触发条件 | REJECT行为 | 最终停留及证据 |
|---|---|---|---|
| 0 | BSS初值，或完成最终flush后准备交接 | 不输出REJECT；单值0不证明应用成功 | 初期位置不定；交接后应用app_idle；W4-HANDOFF |
| 1 | IMAGE_IO：设备未绑定、读取失败等 | REJECT: 0x00000001 | boot_idle；VLOG/STATUS-IO.txt及-uart.log |
| 2 | IMAGE_FORMAT_ERROR：magic、format、header_size不符 | REJECT: 0x00000002 | boot_idle；IMG-02..05 |
| 3 | IMAGE_HEADER_CRC：前28字节CRC不匹配 | REJECT: 0x00000003 | boot_idle；IMG-06 |
| 4 | IMAGE_LENGTH：长度小于表、超过槽/执行区，或load/verify重复边界无效 | REJECT: 0x00000004 | boot_idle；IMG-07/08；load参数分支按源码映射 |
| 5 | IMAGE_ADDRESS：load不等于0x10000 | REJECT: 0x00000005 | boot_idle；IMG-09 |
| 6 | IMAGE_PAYLOAD_CRC：存储payload CRC不匹配 | REJECT: 0x00000006 | boot_idle；IMG-10 |
| 7 | IMAGE_VECTOR：存储或运行区向量非法 | REJECT: 0x00000007 | boot_idle；向量矩阵及LOAD-VECTOR两项 |
| 8 | IMAGE_COPY_CRC：运行区CRC与已校验info.crc不符 | REJECT: 0x00000008 | boot_idle；W4-COPY |
| 0x100 | CPU异常：记录exception_number=IPSR，再写状态 | 异常路径不调用reject，无REJECT保证 | boot_idle；VLOG/STATUS-HARDFAULT.txt读回exception_number=3 |
| 0x101 | UART初始化、write或最终flush失败 | UART不可用，不保证错误文本或完整REJECT | boot_idle；TX-W4-PRINT、TX-W4-FLUSH |

VLOG = `logs/vector-bounds-20261025-021521`（相对codes/）；IMG编号对应第四阶段输入表的宿主和UART日志，TX编号对应TX专项报告。状态1由GDB在image_validate入口将已绑定设备的memory指针置零触发实际flash_read失败；0x100由无效取指触发真实HardFault验证。它们分别是数据读取错误注入和CPU异常测试，不是篡改boot_status伪造结果。各load参数失败的所有子条件尚未逐一做GDB注入，应以表中具体验证范围为准。

应用使用独立状态：app_status=0为初值，1为CRT/交接/SVC通过后的运行状态，2为CRT、交接或SVC检查失败，3为应用异常（记录app_exception），4为UART失败。最终均在app_idle循环。app_status=4已由TX-W4-APP验证；不能将应用状态值套用为Bootloader的image_result。

格式错误示例（只展示拒绝行及提示符）：

```text
"REJECT: 0x00000002\r\n> "
```

复制损坏示例的完整UART为：

```text
BOOT: stage4 image validation\r\n
IMAGE: CRC32 PASS\r\n
"REJECT: 0x00000008\r\n> "
```

UART在reject输出途中失败时，可能留下不完整前缀，boot_status最终被print改写为0x101；这时以GDB状态取证，不要求再靠同一UART输出错误行。IRQ唤醒后idle函数仍循环，不自动恢复启动流程。
