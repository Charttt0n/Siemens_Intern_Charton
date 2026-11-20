# Week 7 问题修复记录

日期：2026 年 11 月 13 日。Week 6 源文件保留原样，修复落在 Week 7 独立主程序、控制模块和应用确认逻辑。已有未提交修改未被覆盖。下面的“修复”表示本地测试通过，不表示已获老师确认或正式发布。

## W7 F01 试运行意图晚于活动槽提交

旧路径在 update_activate 成功后才 arm_trial。若活动槽标记已经落盘、配置意图尚未写入，此时重启会按普通启动路径运行候选应用，缺失 trial 看门狗保护。已通过旧版本故障目标实际复现：`codes/logs/week7-baseline-defects-20261113/activation-before-trial.bin` 的活动槽为 B，配置 pending=0；gap-restart/uart.log 运行 v2 而没有 TRIAL LAUNCH。

修复：`src/week7/control.c` 的 trial_activate 先提交 ARMED 意图，再调用 update_activate。若配置提交失败则不切换活动槽；若活动标记已经落盘，重启仍能看到 ARMED。新故障案例 3 至 7 和 GDB 调用栈验证提交顺序，见 `logs/week7-debug-20261113-final/gdb.txt`。

## W7 F02 旧应用确认了未激活候选槽

先写试运行意图后，如果活动提交中断，旧应用必须继续运行，且不得确认另一个候选槽。`src/week7/app_confirm.c` 在确认前读取活动元数据，仅对 trial_slot 等于 active_slot 且 trial_stage 为 LAUNCHED 的记录提交确认。目标机 Unity 覆盖成功确认、拒绝尚未 LAUNCHED、保持非活动槽候选意图；中断案例 4 至 6 验证冷启动仍运行旧应用。

## W7 F03 出厂重置缺少有效确认

旧 handle_line 的 YES 分支无条件执行重置，绕过主循环里的 factory_armed。旧版本 bare-YES/uart.log 已复现单发 YES 得到 FACTORY DONE。Week 7 删除重复的无条件分支，统一由主循环确认门控；其他完整命令、快捷键或超长输入取消确认。CLI 用例分别验证裸 YES、过期确认及有效 factory/YES。

## W7 F04 输入边界与完整命令

旧 parse_u32 仅检查 next < value，不能识别所有 uint32 溢出。原函数的原样提取测试将 5000000000 接受为 705032704；串口参数的额外范围检查会拒绝本例，因此未声称参数限制被绕过。Week 7 在乘加前检查 `(UINT32_MAX - digit) / 10`，宿主 Unity 验证最大值、越界、空串、符号及非法字符。

旧超长行会只保留前缀；Week 7 对超长整行拒绝。单字母快捷键增加 250 ms 字节空闲判定，使同一串口突发中的 version/upgrade 等完整命令可被整体解析。完整命令应整行发送，慢速逐字输入的限制在指南中明示。

## W7 F05 配置写失败与损坏配置

旧参数设置先改变内存 cfg 再提交，写失败会使运行时与持久值不一致。Week 7 通过候选副本提交，成功才替换运行时配置。单元和 QEMU 故障注入均验证 4000 ms 保存失败后仍为 2500 ms。

配置页损坏时旧 main 使用默认值继续启动，可能丢失试运行意图。Week 7 将其视为恢复状态并禁止 boot/upgrade，直到显式出厂重置。cfg-corrupt 用例验证阻止启动。出厂重置仍保留 A/B 固件，和原确认口径一致。

## W7 F06 升级结果日志

旧 upgrade_session 的 else 只保护 print，紧随其后的 hex 始终执行，使成功升级输出孤立的 0 和 4。Week 7 为成功和失败分支加完整语句块；新上传原始 UART 显示有标签的成功输出，不再出现这些孤立值。日志修改不改变协议字节和结果码。

## W7 T01 看门狗验证暂停语义

前期 fault_snapshot 会暂停 VM，不会恢复运行。若在快照后直接等复位，不能证明看门狗真实计时。Week 7 集成脚本在快照后明确 QMP cont，再要求出现 RESET 事件；随后独立验证预复位快照的冷启动回退。未改写 Week 6 历史通过口径，新报告明确分开两类证据。

## 优化与工程修订

CRC32 改为 16 项半字节表，并以独立逐位算法、镜像、元数据与签名升级回归验证。CMake 将业务优化参数显式传给公共配置函数，避免重复 -Og/-Os 被参数去重后使调试目标失去预期设置。正式、调试和旧基线的选项、size、map、ELF SHA256 均在本轮证据中保留。

完整测试结果见 [系统测试报告](week7-test-report.md)，量化结果见 [优化报告](week7-optimization-report.md)。
