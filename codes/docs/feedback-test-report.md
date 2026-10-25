# 反馈补充与运行脚本测试

更新日期：2026-10-24。本轮为老师反馈修订，待老师审核。历史记录保留；下列新证据为本轮判定依据。测试中的预期拒绝不等于套件失败。

入口：make verify-feedback。构建与运行入口在临时工程副本内直接调用，使用真实工具链和QEMU，不覆盖日常镜像。

TX测试编译真实第二阶段驱动，以volatile数组替代MMIO；证明轮询有界、错误返回和不写DATA，不证明物理串口阻塞时序。QEMU的禁用UART用例验证上层boot_status=3。PRIMASK用真实ARM指令，含uart_init和空队列uart_getc。

Week2演示会进入WFI，脚本必须先收到PASS才接受宿主主动停止；另以-S暂停CPU测试无输出时不能判成功。

| 用例编号与方式 | 实际输入与步骤 | 预期输出 | 实际输出与验证证据 |
|---|---|---|---|
| FB-TX-01 compile real driver with fake volatile MMIO | UART_NATIVE_TEST | compiler exit=0 | exit=0, timeout=False; ；logs/feedback-20261024-173104/FB-TX-01.log |
| FB-TX-02 native driver assertions | CTRL=1 STATE=1 DATA=0x55 then STATE=0 | bounded -1, no DATA write; recovery writes 0x41 | exit=0, timeout=False; TX_FULL=1: flush/putc/write=-1; DATA=0x55 unchanged; bounded return PASS；TX_FULL=0 after timeout: write=0 DATA=0x41 PASS；disabled TX and NULL: -1 PASS；；logs/feedback-20261024-173104/FB-TX-02.log |
| FB-IRQ-01 cross compile production IRQ/UART | nested masks 0 and 1 | build succeeds | exit=0, timeout=False; ；logs/feedback-20261024-173104/FB-IRQ-01.log |
| FB-IRQ-02 real Cortex-M7 instructions under QEMU | initial mask=0/1; nested save/restore, uart_init, empty uart_getc | both original masks restored; exit=0 | exit=0, timeout=False; PRIMASK initial=0 nested/init/getc restored=0 PASS；PRIMASK initial=1 nested/init/getc restored=1 PASS；；logs/feedback-20261024-173104/FB-IRQ-02.log |
| CLI-W1-MISSING run before build | clean copy, ELF/image absent | nonzero error, no PASS | exit=1, timeout=False; build/demo.elf: No such file or directory；qemu-system-arm: Could not load kernel 'build/demo.elf'；；logs/feedback-20261024-173104/CLI-W1-MISSING.log |
| CLI-W1-BUILD actual build entry | clean stage output | exit=0 | exit=0, timeout=False; -- The C compiler identification is GNU 16.2.0；-- The ASM … target demo.elf；   text	   data	    bss	    dec	    hex	filename；    368	      4	      8	    380	    17c	build/demo.elf；；logs/feedback-20261024-173104/CLI-W1-BUILD.log |
| CLI-W1-RUN actual run entry | default demo | expected firmware marker; W2 host stops intentional idle | exit=0, timeout=False; PASS: Cortex-M7; .data initialized; .bss zero; bare-metal C99；；logs/feedback-20261024-173104/CLI-W1-RUN.log |
| CLI-W2-MISSING run before build | clean copy, ELF/image absent | nonzero error, no PASS | exit=1, timeout=False; build/bootloader.elf: No such file or directory；qemu-system-arm: Could not load kernel 'build/bootloader.elf'；；logs/feedback-20261024-173104/CLI-W2-MISSING.log |
| CLI-W2-BUILD actual build entry | clean stage output | exit=0 | exit=0, timeout=False; -- Configuring done (0.0s)；-- Generating done (0.0s)；CMak … oader.elf；   text	   data	    bss	    dec	    hex	filename；   1488	     20	   4132	   5640	   1608	build/bootloader.elf；；logs/feedback-20261024-173104/CLI-W2-BUILD.log |
| CLI-W2-RUN actual run entry | default demo | expected firmware marker; W2 host stops intentional idle | exit=0, timeout=True; BOOT: week2 mps2-an500 Cortex-M7；CLOCK: 25000000 Hz (QEMU … e (WFI)；PASS: C runtime; UART boot ready；qemu-system-arm: terminating on signal 15 from pid 81108 (<unknown process>)；；logs/feedback-20261024-173104/CLI-W2-RUN.log |
| CLI-W3-MISSING run before build | clean copy, ELF/image absent | nonzero error, no PASS | exit=1, timeout=False; ERROR: ELF not found: /private/var/folders/7d/fk0m_4z93zj …  w write W3OK; s stats; h help；Host: save = checkpoint; quit / EOF = checkpoint and exit. Ctrl-C = exit without saving.；；logs/feedback-20261024-173104/CLI-W3-MISSING.log |
| CLI-W3-BUILD actual build entry | clean stage output | exit=0 | exit=0, timeout=False; -- The C compiler identification is GNU 16.2.0；-- The ASM …  text	   data	    bss	    dec	    hex	filename；   2172	      0	   4256	   6428	   191c	build/week3/bootloader-week3.elf；；logs/feedback-20261024-173104/CLI-W3-BUILD.log |
| CLI-W3-RUN actual run entry | p, save, quit | expected firmware marker; W2 host stops intentional idle | exit=0, timeout=False; Commands: p ping; r read; e erase first parameter sector; … ash.bin；> Saved: /private/var/folders/7d/fk0m_4z93zjdx1fc0f71fbzc0000gn/T/feedback-zp_c3lyi/codes/build/week3-flash.bin；；logs/feedback-20261024-173104/CLI-W3-RUN.log |
| CLI-W4-MISSING run before build | clean copy, ELF/image absent | nonzero error, no PASS | exit=1, timeout=False; ERROR: image missing; run bash scripts/build-week4.sh first；；logs/feedback-20261024-173104/CLI-W4-MISSING.log |
| CLI-W4-BUILD actual build entry | clean stage output | exit=0 | exit=0, timeout=False; opt/homebrew/bin/arm-none-eabi-gcc；-- Detecting C compile … 76	   6596	   19c4	build/week4/bootloader-week4.elf；    960	     12	   4132	   5104	   13f0	build/week4/application.elf；；logs/feedback-20261024-173104/CLI-W4-BUILD.log |
| CLI-W4-RUN actual run entry | default demo | expected firmware marker; W2 host stops intentional idle | exit=0, timeout=False; BOOT: stage4 image validation；IMAGE: CRC32 PASS；LOAD: verified at 0x00010000；JUMP: application；APP: CRT PASS；APP: HANDOFF PASS；APP: SVC PASS；APP: RUNNING；> ；logs/feedback-20261024-173104/CLI-W4-RUN.log |
| CLI-W4-MISSING-IMAGE actual runner missing image | missing explicit image path | exit=1 and image missing | exit=1, timeout=False; ERROR: image missing; run bash scripts/build-week4.sh first；；logs/feedback-20261024-173104/CLI-W4-MISSING-IMAGE.log |
| CLI-W4-BLANK actual runner rejected image | 1 MiB all ff | exit=1 and REJECT: 0x00000002 | exit=1, timeout=False; BOOT: stage4 image validation；REJECT: 0x00000002；> ；logs/feedback-20261024-173104/CLI-W4-BLANK.log |
| CLI-W2-PAUSED paused emulator deadline | -S; CPU never starts | no PASS; watchdog terminates process | exit=0, timeout=True; qemu-system-arm: terminating on signal 15 from pid 81108 (<unknown process>)；；logs/feedback-20261024-173104/CLI-W2-PAUSED.log |
| CLI-BUILD-FAIL controlled tool failure | cmake returns 42 | build script returns 42; no stale success | exit=42, timeout=False; ；logs/feedback-20261024-173104/CLI-BUILD-FAIL.log |
| MANIFEST-valid schema and file verification | valid v1 manifest and file bytes test | accepted | accepted；logs/feedback-20261024-173104/manifest-example.json |
| MANIFEST-missing schema and file verification | 对有效manifest构造：missing；完整输入见manifest-cases.json | ValueError | $: missing field；logs/feedback-20261024-173104/manifest-cases.json |
| MANIFEST-unknown schema and file verification | 对有效manifest构造：unknown；完整输入见manifest-cases.json | ValueError | $: unknown field；logs/feedback-20261024-173104/manifest-cases.json |
| MANIFEST-version schema and file verification | 对有效manifest构造：version；完整输入见manifest-cases.json | ValueError | $.schema_version: const；logs/feedback-20261024-173104/manifest-cases.json |
| MANIFEST-hash schema and file verification | 对有效manifest构造：hash；完整输入见manifest-cases.json | ValueError | $.files[0].sha256: pattern；logs/feedback-20261024-173104/manifest-cases.json |
| MANIFEST-traversal schema and file verification | 对有效manifest构造：traversal；完整输入见manifest-cases.json | ValueError | $.files[0].path: pattern；logs/feedback-20261024-173104/manifest-cases.json |
| MANIFEST-size schema and file verification | 对有效manifest构造：size；完整输入见manifest-cases.json | ValueError | $.files[0].size: range；logs/feedback-20261024-173104/manifest-cases.json |
| MANIFEST-boolean-size schema and file verification | 对有效manifest构造：boolean-size；完整输入见manifest-cases.json | ValueError | $.files[0].size: type；logs/feedback-20261024-173104/manifest-cases.json |
| MANIFEST-duplicate schema and file verification | 对有效manifest构造：duplicate；完整输入见manifest-cases.json | ValueError | duplicate file；logs/feedback-20261024-173104/manifest-cases.json |
| MANIFEST-tampered-file hash verification | file bytes test -> bad | file mismatch | file mismatch: firmware.bin；logs/feedback-20261024-173104/manifest-cases.json |

本轮调试记录：首轮IRQ测试因头文件搜索顺序选到Week2 UART头而失败，已改正；随后脚本错误地要求QEMU被SIGTERM后返回负值，但本机返回0，现以明确固件输出和主动停止标志判断。失败记录保留在logs/feedback-20261024-172954、173014及173050。最终证据以feedback-latest.txt对应PASS目录为准。
