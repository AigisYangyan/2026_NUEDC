# 云台快速位置 + 0xAA 双轴协同改造（契约，冻结于代码之前）

> 决策已定死（2026-07-20，用户裁定）：二维云台双 X42S 共 UART7 总线（256000），运动流走
> **快速位置模式 FC（有符号 int32 绝对目标）+ 0xAA 多电机命令一帧封装双轴、同起**；
> 管理命令（enable/preset/clear）保持单帧异步。方案论证见会话记录与
> `docs/X42S云台步进上位机配置指南.md`、记忆 `x42s-manual-key-facts`。
>
> 为什么绝对而非相对：绝对模式 + 位置命令打断（手册 §5.1.3 平滑过渡）→ 丢帧/忙帧被下一帧
> 自愈，消灭增量漂移这一整类误差。为什么 FC 而非 0xFD-绝对：FC 子命令 7B（vs 13B）、符号自带
> 于 int32 → **消掉 gimbal_stepbus 的「脉冲→dir+magnitude 拆分」所有者**（数据链少一级变换）。
> 为什么 0x0A 而非 0x93：绝对模式的目标相对「坐标零点」，须用 0x0A(将当前位置清零) 建立该零点，
> 0x93 是单圈回零零点（不同语义）。
>
> 速度上限：F1 预设速度 = gimbal cfg.step_speed_rpm(=30)，emm42 唯一限幅所有者夹 ≤100 后 ×10
> （S_Vel_IS 使能前提）→ 300=0x012C。256 细分下 30RPM=25600 微步/s，为 300kHz 上限的 8.5%，
> 主频余量充足，满足用户「高细分必须限速」要求。max_step 钳位仍归 vision_aim。

---

## 本轮不做（显式排除，避免 scope 蔓延）

**步进 RX 反馈轮询（0x36 实时位置 / 0x3A 状态标志）不在本轮。** 现役架构的既定设计是
「步进应答有意不用，视觉是唯一反馈路径」（拓扑 §6 V22 语境、gimbal_stepbus.h 注释）。加入步进
RX 解析会**反转这一既定架构决策**，须走独立契约与拓扑风险评审。而且它是安全/诊断增强，非指向
精度所需——命令过期安全已由 gimbal 的 coord_timeout_ms/ack_timeout_ms（视觉路径）满足（§8.1）。
故本轮 `GimbalStepbus_Service` 保持 drain+discard 不变；0x36/0x3A 轮询留作后续独立任务。

---

## T-GQ1 emm42 快速位置/多机/清零 纯组包（Driver，纯新增）

Status: ACCEPTED (2026-07-20，提交 bff7516 + 拓扑 1bb2a95)
Accept: E01 test_emm42 8/8、E02 套件 36pass 0fail、E03 固件 exit 0 + .out 重链、E04 emm42.c 层洁、E05 四新声明皆有定义。
Goal: emm42.c 新增 F1/FC/0xAA/0x0A 四个纯组包函数并有独立主机测试逐字节验证；固件与全量主机测试不回归。

Evidence:
- `hc-team/driver/step_motor/emm42.c:119` 现有 `Emm42_BuildPositionFrame`（0xFD 相对/绝对，13B，speed×10 限幅唯一所有者）——冻结 `stepmotor_bus.c` 与 gimbal_stepbus 均用它，**不删**。
- `hc-team/driver/step_motor/emm42.h:29-54` 现有命令码/尺度宏；`EMM42_POSITION_MODE_ABSOLUTE=1`、`EMM42_SPEED_SCALE_X10=10`、`EMM42_SPEED_MAX_RPM=100` 已在，复用。
- `tests/host/` 当前无独立 `test_emm42.c`；emm42 经 test_gimbal_stepbus 间接覆盖。

Architecture:
- Abstraction: 把「快速位置预设/快速位置目标/多机封装/清零原点」编码为 EMM42 协议帧（纯组包，零副作用，无收发）。
- Hidden state: 无（纯函数）。
- Owner layer: Driver。
- Allowed dependency direction: emm42.c → emm42.h + std 仅。

Scope:
- allowed_files: `hc-team/driver/step_motor/emm42.c`, `hc-team/driver/step_motor/emm42.h`, `tests/host/test_emm42.c`, `tests/host/Makefile`
- forbidden_files: `hc-team/app/service/gimbal/gimbal.c`, `hc-team/app/service/gimbal/gimbal_stepbus.c`, `hc-team/app/service/gimbal/gimbal_stepbus.h`, `board.syscfg`
- preserved_behavior: 现有 7 个 Build* 函数签名与字节布局逐字不变（冻结 stepmotor_bus 依赖）；speed×10 限幅仍唯一在 emm42。

Preconditions:
- 波特率 256000 已落地（提交 12388e2）；S_Vel_IS 使能属用户上位机自理（不影响组包，仅影响电机端解释）。

Steps:
1. 先加 test_emm42.c（对 F1/FC/0xAA/0x0A 的期望字节断言），编译即失败（函数未实现）。
2. 在 emm42.c 实现四个 builder，最小改动；emm42.h 加对应声明与文档。
3. Makefile 注册 test_emm42 + run_emm42 并挂 all/clean/.PHONY。
4. 验证后更新拓扑 driver.md §2 Emm42_API 方法清单。

新增函数（精确签名）：
```
bool Emm42_BuildQPosPresetFrame(uint8_t axis_id, uint16_t speed_rpm, uint8_t acceleration,
                                uint8_t mode, uint8_t *out, uint8_t *out_len);  /* 0xF1, 8B, speed×10 限幅 */
bool Emm42_BuildQPosFrame(uint8_t axis_id, int32_t pulses, uint8_t *out, uint8_t *out_len); /* 0xFC, 7B, int32 大端 */
bool Emm42_BuildMultiCmdFrame(const uint8_t *sub_cmds, uint8_t sub_cmds_len,
                              uint8_t *out, uint8_t *out_len); /* 0xAA, 广播00, len=sub+5, 上限 sub≤26 */
bool Emm42_BuildClearPositionFrame(uint8_t axis_id, uint8_t *out, uint8_t *out_len); /* 0x0A 6D, 4B */
```
字节布局锚点（官方 Emm_V5.c 交叉核对）：F1=[addr,F1,vel_hi,vel_lo,acc,mode,sync=0,6B]；
FC=[addr,FC,p31,p23,p15,p7,6B]（有符号大端）；0xAA=[00,AA,len_hi,len_lo,<subs>,6B] 其中 len=sub_len+5；
0x0A=[addr,0A,6D,6B]。

Verification:
- E01 command: `powershell -Command "make -C tests/host test_emm42 run_emm42"`（经 PowerShell 工具）
- E01 expected_exit: 0
- E01 postcondition: 打印 "All emm42 tests passed."，F1/FC/0xAA/0x0A 逐字节断言全 PASS
- E01 negative_check: 无 FAIL 行
- E02 command: `powershell -Command "make -C tests/host all"`
- E02 expected_exit: 0
- E02 postcondition: 每个 test_* 打印 "All ... passed."，套件用例数 ≥ 基线 + test_emm42 新增数
- E02 negative_check: 无 FAIL / 无 "Error"
- E03 command: `rtk make -C Debug all`
- E03 expected_exit: 0
- E03 postcondition: 链接产物更新，退出 0（冻结 stepmotor_bus 仍引用旧 builder，不应断链）
- E03 negative_check: 无 "undefined"/"error:"
- E04 command: Grep `#include` in emm42.c
- E04 expected_exit: n/a
- E04 postcondition: 仅 emm42.h + <stdbool/stddef/stdint.h>，无 app/driver/ti_ 头
- E04 negative_check: 无跨层 include
- E05 command: Grep 新四函数名 in emm42.h 与 emm42.c
- E05 postcondition: 每个新声明在 emm42.c 有定义（头只声明自身实现符号，承 V18）
- E05 negative_check: emm42.h 无任何 stepmotor_bus 实现的动作函数回潮

Stop conditions:
- 若 0xAA 子命令拼装需要 emm42 感知「两个轴」→ 停（应由 gimbal_stepbus 拼装、emm42 只封装裸串）。
- 若为让新函数编过需动 forbidden_files → 停。

---

## T-GQ2 gimbal 双轴绝对协同 + F1 预设/0x0A 清零编排（Service，两文件原子改）

Status: ACCEPTED (2026-07-20，提交 e6be2d2 + 拓扑 4cc17fe)
Accept: E01 stepbus 10/10、E02 gimbal 16/16、E03 套件 0fail、E04 固件 exit 0 + .out 重链、E05 旧 API(TrySendRelative/TrySendSetZero/s_dispatch_y_first) 全仓零残留、E06 层向不变。arch-auditor 五项通过，仅存量 §8.3 建议（emm42_clamp_accel_grade 不可达分支，非本轮引入，留后续触碰时清理）。
Goal: gimbal_stepbus 用 FC+0xAA 一帧发双轴绝对目标、preset/clear 替换旧 setzero；gimbal ARMING 加
preset/clear、AIMING 每新坐标累加绝对目标并发一帧双轴、删除单轴相对/拆分/交替。固件+全量主机测试绿。

Evidence:
- `gimbal_stepbus.c:48` `TrySendRelative`（拆 dir+magnitude→0xFD 相对）——唯一调用者 gimbal.c:252；本任务删除并替换。
- `gimbal_stepbus.c:103` `TrySendSetZero`（0x93 单圈回零）——绝对方案改用 0x0A 清零；调用者 gimbal.c:178,181。
- `gimbal.c:198-256` `gimbal_aim_dispatch`：每 delta≠0 轴单发相对、成功才累加 cur_pulse、`s_dispatch_y_first` 交替——绝对单帧方案下交替失去意义（双轴同帧无竞争）。
- `gimbal.c:170-185` ARMING 四步（enable X/Y + setzero X/Y）——改六步（enable X/Y + preset X/Y + clear X/Y）。

Architecture:
- Abstraction: 云台把「双轴当前绝对脉冲目标」翻译成一帧 0xAA(FC_Y∥FC_X) 发上总线，同起、位置命令打断自愈；管理期逐帧建立使能/预设/绝对零点。
- Hidden state: gimbal 轴累计绝对目标 cur_pulse（唯一所有者，本任务由「成功才累加」改为「无条件累加」——绝对重发幂等，忙帧自愈；轴程限幅仍由 vision_aim 依 cur_pulse 施加）。gimbal_stepbus 无私有运行态。
- Owner layer: Service。
- Allowed dependency direction: gimbal.c → gimbal_stepbus/uart_vision/vision_aim/clock；gimbal_stepbus.c → emm42/stepmotor_uart。

Scope:
- allowed_files: `hc-team/app/service/gimbal/gimbal_stepbus.c`, `hc-team/app/service/gimbal/gimbal_stepbus.h`, `hc-team/app/service/gimbal/gimbal.c`, `hc-team/app/service/gimbal/gimbal.h`, `tests/host/test_gimbal_stepbus.c`, `tests/host/test_gimbal.c`
- forbidden_files: `hc-team/driver/step_motor/emm42.c`, `hc-team/driver/step_motor/emm42.h`, `hc-team/middleware/vision_aim/vision_aim.c`, `hc-team/middleware/vision_aim/vision_aim.h`, `hc-team/driver/uart_vision/uart_vision.c`
- preserved_behavior: 状态机骨架（IDLE/HANDSHAKING/ARMING/AIMING/STOPPED）与超时安全停语义不变；vision_aim 映射（死区/kp/kd/step-clamp/极性/轴程）逐拍透传不复算；速度限幅仍唯一在 emm42；enable 帧（F3）不变；drain+discard RX 不变（反馈轮询本轮排除）。

Preconditions:
- T-GQ1 ACCEPTED（F1/FC/0xAA/0x0A builder 可用）。

Steps:
1. 先改 test_gimbal_stepbus/test_gimbal 断言新行为，编译即失败（旧 API 已改签名）。
2. gimbal_stepbus：删 TrySendRelative，加 TrySendDualAbsolute(x_pulse,y_pulse)（拼两 FC→0xAA）、TrySendPreset(axis,speed,accel,mode)、TrySendClearZero(axis)（0x0A）；删 TrySendSetZero。
3. gimbal：ARMING 六步；AIMING dispatch 改无条件累加 cur_pulse + 总线空发一帧双轴绝对；删 s_dispatch_y_first。
4. 头文件单一所有者注释更新（拆分所有者移除；相对→绝对；0x93→0x0A）。
5. 验证后更新拓扑 §5.2 数据流（相对→绝对、拆分级移除）、app.md 边、§10 日志。

Verification:
- E01 command: `powershell -Command "make -C tests/host test_gimbal_stepbus run_gimbal_stepbus"`
- E01 expected_exit: 0
- E01 postcondition: 双轴绝对帧 == Emm42_BuildMultiCmdFrame(FC_Y(y)∥FC_X(x)) 逐字节相等；preset==F1、clear==0x0A 帧断言 PASS；TX 忙拒发、完成重开保留
- E01 negative_check: 无 FAIL 行；测试内无 TrySendRelative/TrySendSetZero 引用
- E02 command: `powershell -Command "make -C tests/host test_gimbal run_gimbal"`
- E02 expected_exit: 0
- E02 postcondition: ARMING 依次发 enable X/Y→preset X/Y(F1,mode=ABS)→clear X/Y(0x0A)；AIMING 每新坐标发一帧 0xAA 含两轴 cur_pulse 绝对目标；坐标/握手超时→STOPPED
- E02 negative_check: 无 FAIL；AIMING 不再出现单轴 0xFD 相对帧
- E03 command: `powershell -Command "make -C tests/host all"`
- E03 expected_exit: 0
- E03 postcondition: 全套件绿，用例数 ≥ 基线（含 T-GQ1 后基线）
- E03 negative_check: 无 FAIL / "Error"
- E04 command: `rtk make -C Debug all`
- E04 expected_exit: 0
- E04 postcondition: 退出 0（gimbal 链更新，冻结 platform_2d 不受影响）
- E04 negative_check: 无 "undefined"/"error:"
- E05 command: Grep `TrySendRelative|TrySendSetZero|s_dispatch_y_first` in hc-team
- E05 expected_exit: n/a
- E05 postcondition: 全仓零命中（旧 API 与交替状态彻底移除，无双轨）
- E05 negative_check: 上述任一符号残留即 REJECT
- E06 command: Grep `#include` in gimbal.c 与 gimbal_stepbus.c
- E06 postcondition: gimbal.c 仅含 gimbal_stepbus/uart_vision/vision_aim/clock 头；gimbal_stepbus.c 仅含 emm42/stepmotor_uart 头（层向不变）
- E06 negative_check: 无 DL HAL / ti_ 头 / 反向上层 include

Stop conditions:
- 若绝对累加需要在 gimbal_stepbus 或 vision_aim 复做第二次限幅/缩放/符号 → 停（单一所有者）。
- 若移除 TrySendRelative 导致 firmware 断链（除 gimbal.c 外有他处调用）→ 停并复核。
- 若为过测须动 forbidden_files → 停。
