# P2-FIX：Encoder 消费者迁移、PID 按值化与测量口径收口计划

计划所有者：Codex（本文件即验收契约，REASONIX 不得修改验收条目）  
制定日期：2026-07-16  
状态：done（2026-07-16 Codex 验收 P2F.T1 通过；P2F.T2 随硬件验收取消而删除）  
背景：REASONIX 于 2026-07-16 提交 `SELF_CHECK_BLOCKED`，证据为 `plan2_encoder_rewrite.md:3` 仍为 in_progress（P2.1 硬件确认与 P2.3–P2.5 待执行），且代码中 `g_tMotors` 兼容写入、deprecated Encoder API 与 PID 读全局仍然存在。本计划承接 plan2 的 P2.4/P2.5 软件残余与 P2.1/§7 硬件验收，是 P3 Motor 重写的直接前置。原 plan3 P3.T2 中涉及 `encoder.c`、`pid.c/.h` 的改动所有权移交本计划（plan3 已同步修订）。  
流程（2026-07-16 简化）：Codex 计划 → REASONIX 施工并提交行级完成报告 → Codex 精简验收（自检阶段已撤销）。

REASONIX 施工时必须携带以下指令：

```text
Execute only this task. Reports are claims, not proof. Do not mark complete until every command and observable postcondition has been reproduced. Never ignore command errors. Do not close topology items without direct evidence.
```

状态标签与证据行规则同 plan3/4/5：`CONSTRUCTION_*` / `CODEX_*`；证据行 E01–E09 由本计划固定分配。

## 1. 三个设计问题

1. **抽象是什么？** Encoder 只提供一致快照（plan2 §4 契约，已实现）；PID 电机环变为纯按值算法：输入目标与反馈（m/s），输出控制量（-1000..1000），不感知任何 Driver。
2. **必须隐藏什么？** Encoder 私有常量表（PPR、轮周长、方向、校准）不再公开；PID 内部积分/上次误差状态维持现有全局实例（V13 存量，另案）。
3. **代码属于哪里？** 采样节拍与真实 elapsed 计算在调用任务；速度反馈来源唯一为 `Encoder_GetSnapshot()`；输出施加仍经现有 `Motor_SetPwm()`（Motor API 更换归 P3）。

## 2. 当前问题证据（Codex 于 2026-07-16 逐条核实）

- `encoder.c:169-176`、`encoder.c:228-236`：兼容分支写 `g_tMotors[]`（V05 残余）。
- `pid.c:214-222`：`pid_closeloop_motor(float,float)` 读 `g_tMotors[i].p_pid`、`g_tMotors[i].speed` 并回写（V04 的 Middleware 侧）。
- 消费者仍走旧链路：`task_groups.c:226` `Encoder_UpdateSample()`、`:234` `pid_closeloop_motor(...)`、`:236` `Motor_SetPwm(..., p_pid->out)`；`speed_loop.c:63-64`（读 `g_tMotors[].speed` 做遥测）、`:120,130,134`；`task1.c:235,248,353-354,411`。
- deprecated Encoder API 仍公开：`encoder.h:99` `g_fEncoderParam[]` 公开可写参数表、`encoder.h:103-131` `Encoder_UpdateSample/GetTotals/CalcSpeed/SetMiu/SetSamplePeriodMs/CalcWheelTargets/UpdateTravelDistance/GetTravelDistance/ResetTravelDistance`。全仓库调用者仅 `Encoder_UpdateSample` ×3（上列任务），其余 9 项零调用者。
- `encoder.c:300` 旧 `Encoder_UpdateSample()` 用可写参数表里的固定 10 ms 充当 elapsed（plan2 §6 明令禁止的模式）。
- 双口径残留：`encoder.c:63` `MAX_TARGET_SPEED = 1200.0f`（mm/s 口径）与实际 m/s 反馈并存于同一公开表。
- P2.1 硬件事实（方向符号、PPR 口径、轮径/miu 依据）尚无测量记录；`encoder_measurement_baseline.md` 待补。

## 3. 唯一数据处理链（P2-FIX 之后）

```text
采样任务用 Clock_NowMs() 差值计算真实 elapsed_ms
 -> Encoder_Update(elapsed_ms) -> Encoder_GetSnapshot(&snap)
 -> pid_closeloop_motor(lt, rt, snap.speed_mps[L], snap.speed_mps[R], &out_l, &out_r)
 -> Motor_SetPwm(&g_tMotors[i], out_i)   /* Motor API 更换归 P3 */
 -> 遥测读 snap.speed_mps，不读 g_tMotors
```

- 速度反馈唯一来源为快照；`g_tMotors` 的 `speed/encoder_*/p_pid` 字段在本计划后成为无读写的死字段，由 P3.T2 随 `Motor_T` 一并删除。
- PID 输出限幅保持在 PID（算法职责）；Motor 硬件限幅归 P3。

## 4. 最小目标接口

```c
/* pid.h —— 仅此函数变更，其余 PID API 不动 */
void pid_closeloop_motor(float left_target_mps, float right_target_mps,
                         float left_feedback_mps, float right_feedback_mps,
                         float *p_left_out, float *p_right_out);
```

`encoder.h` 删除：`g_fEncoderParam` 与 `ENCODER_PARAM_*` 枚举（常量转为 `encoder.c` 私有 `static const`）、上列 9 个 deprecated API。保留：`Encoder_Init/Encoder_Update/Encoder_GetSnapshot` 与 `Encoder_Snapshot` 类型，签名不变。

## 5. 施工任务

### P2F.T1 消费者迁移与耦合删除（软件）

Status: done（2026-07-16 CODEX_ACCEPTED：E01–E06 通过，提交 455a968）
Goal: 全仓库不存在 Encoder→Motor 写入、PID→Driver 读取和 deprecated Encoder API；速度环行为不变。

Evidence:
- §2 全部行号。

Architecture:
- Abstraction/Hidden/Owner: 见 §1。
- Allowed dependency direction: `Task -> Encoder/PID/Motor`（V07 存量方向，不新增类别）；禁止 `PID -> Driver`、`Encoder -> Motor`。

Scope:
- allowed_files: `hc-team/driver/encoder/encoder.c`、`hc-team/driver/encoder/encoder.h`、`hc-team/middleware/pid/pid.c`、`hc-team/middleware/pid/pid.h`、`hc-team/app/tasks/task_groups.c`、`hc-team/app/tasks/task1/task1.c`、`hc-team/app/tasks/speed_loop/speed_loop.c`、`tests/host/test_pid.c`（新建）、`tests/host/test_encoder.c`、`tests/host/stub_motor.c`（预期删除或清空）、`tests/host/Makefile`
- forbidden_files: `hc-team/driver/motor/motor.c`、`hc-team/driver/motor/motor.h`、`hc-team/app/scheduler/vofa_register.c`、`hc-team/app/scheduler/vofa_register.h`、`hc-team/app/tasks/platform_2d/**`
- preserved_behavior: PID 参数值、输出限幅值、任务周期、VOFA 遥测字段含义不变；`pid_closeloop_angle/track` 等其他 PID 函数不动；采样任务名义周期仍 10 ms，仅 elapsed 改为真实测量。

Preconditions:
- 基线 `rtk make -C tests/host all` 通过（2026-07-16 已记录 13 项）。

Steps:
1. 先写 `test_pid.c` 并运行（RED：`pid.c` 因包含 `motor.h` 无法进入主机构建，或按值断言失败）。用例最少覆盖：正/负误差的输出方向、输出限幅命中、零误差稳态、连续两次调用的增量式状态推进、左右通道互不串扰。
2. `pid_closeloop_motor` 按值化；三个任务改走 §3 数据链；删除 encoder 兼容写入块与 deprecated API/参数表；`stub_motor.c` 相应清理。
3. 只重构本任务引入的代码；不顺手动 `g_tLeft/RightMotorPID` 全局实例（V13 另案）。
4. 验证通过后更新拓扑（见 §7）。

Verification:
- E01 command: `make -C tests/host run_pid`
- E01 expected_exit: 0
- E01 postcondition: 上列 PID 用例全部通过；链接对象不含任何 motor 符号（fake/stub 列表即证据）。
- E01 negative_check: `rg 'driver/|motor\.h|g_tMotor|Motor_T' tests/host/test_pid.c` 零命中。原正则 `motor|g_tMotor` 会误匹配被测公共 API 名 `pid_closeloop_motor`，已于施工 RED 审计中更正，不以拆分标识符规避。
- E02 command: `rtk make -C tests/host all`
- E02 expected_exit: 0
- E02 postcondition: encoder 既有用例全部保留并通过；总用例数 ≥ 基线 13 项 + 新增 PID 项。
- E02 negative_check: 不得删除既有断言凑绿。
- E03 command: `rg 'driver/|motor\.h|g_tMotor|Motor_T|p_pid' hc-team/middleware/pid`
- E03 expected_exit: 1
- E03 postcondition: 零命中；`pid.h:59` 签名为 §4 形式。
- E03 negative_check: 不得以前置声明或 `extern` 绕过。
- E04 command: `rg 'g_tMotor|driver/motor' hc-team/driver/encoder`
- E04 expected_exit: 1
- E04 postcondition: 零命中（V05 全关闭）。
- E04 negative_check: 无。
- E05 command: `rg 'Encoder_UpdateSample|Encoder_GetTotals|Encoder_CalcSpeed|Encoder_SetMiu|Encoder_SetSamplePeriodMs|Encoder_CalcWheelTargets|TravelDistance|g_fEncoderParam|ENCODER_PARAM_' hc-team tests`
- E05 expected_exit: 1
- E05 postcondition: 全仓库零命中。
- E05 negative_check: 常量私有化后不得新增任何公共 Set/Get 参数接口。
- E06 command: `rtk make -C Debug clean` 后 `rtk make -C Debug all`，随后 `git diff --stat`
- E06 expected_exit: 0
- E06 postcondition: 新鲜产物、无新增 warning；改动只落在 allowed_files。
- E06 negative_check: forbidden_files 零改动；不使用旧目标缓存。

Hardware gate:
- 本任务不执行硬件操作；速度环行为等价性的最终确认在 E07–E09。

Stop conditions:
- 按值化后发现任务实际依赖 `p_pid->out` 之外的 PID 内部状态；真实 elapsed 与固定 10 ms 假设差异导致速度数值明显变化（先报告，禁止加补偿系数掩盖）；需要动 vofa_register 才能编译。

### P2F.T2 测量口径硬件基线（P2.1 + plan2 §7 硬件验收）

Status: removed（2026-07-16 用户裁定取消硬件验收，测量基线由用户自测，E07–E09 作废）
Goal: 用测量证据确认方向符号、PPR 口径、轮周长/校准依据和采样间隔无关性，补齐 `encoder_measurement_baseline.md`。

Evidence:
- plan2 §5 P2.1 条目；`encoder.c:60` 轮径 68.6 mm 与 `miu=1.0` 无来源记录。

Scope:
- allowed_files: `agent/phase2_driver_rewrite/encoder_measurement_baseline.md`、`hc-team/driver/encoder/encoder.c`（仅当测量证明私有常量错误时修正数值，且必须附测量记录）
- forbidden_files: 其余全部源码
- preserved_behavior: 快照 API 契约不变。

Preconditions:
- P2F.T1 通过；轮子离地；Motor P0 有效（只允许手动转轮或安全低功率）。

Steps:
1. OLED/串口同屏显示 raw total、corrected total、delta、speed_mps。
2. 按 E07–E09 逐项测量并记录到 baseline 文档。

Verification:
- E07 postcondition: 左轮正转/反转、右轮正转/反转四组独立测量：raw 符号、corrected 符号、speed 符号记录成表；“车辆前进为正”修正恰好发生一次（Encoder 通道表），Runtime 判向公式未叠加二次修正。
- E07 negative_check: 不得只测整车前进一组。
- E08 postcondition: 手转固定 N 圈（N≥10），累计脉冲 ≈ N × PPR，误差 ≤2%；明确记录 PPR 是电机轴/输出轴/四倍频口径；轮径实测值与 `encoder.c` 私有常量一致或已按测量修正；`miu` 的取值依据写明（无依据则固定 1.0 并记录）。
- E08 negative_check: 不得沿用无来源的旧经验系数。
- E09 postcondition: 同一手转速度下采样间隔 5/10/20 ms 三组 speed_mps 差异 ≤5% 且无首拍跳变；逻辑分析仪确认正常转速下 AB 边沿计数无漏计（对比仪器计数与固件累计值）。
- E09 negative_check: 不得借调试器观察；只允许串口/OLED/仪器证据。

Hardware gate:
- （作废：硬件验收已取消。）

Stop conditions:
- 测量与私有常量矛盾且修正会改变速度环行为超出 preserved_behavior（报告 Codex 决定是否连同 PID 参数一起重标定）。

## 6. 全任务禁止事项

- 禁止保留任何 `g_tMotors` 镜像写入或第二套速度反馈来源。
- 禁止把删除的参数表变成新的公共配置 API。
- 禁止在 PID 内新增滤波、死区或第二层限幅。
- 禁止修改 `driver/motor/*`（P3 所有权）。

## 7. 拓扑更新契约（验证通过后才允许改）

- 类图：`Encoder_API` 删除 deprecated 项与 `g_EncoderParams`；`PID_API` 的 `pid_closeloop_motor` 更新签名；删除 `Encoder_API --> Motor_API : VIOLATION` 边与 `PID_API --> Motor_API : VIOLATION reads g_tMotors` 边（`Motor_API --> PID_API` 头文件包含边保留，归 P3 关闭）。
- 5.1 数据流图：删除 `Encoder -.-> MotorGlobal` 兼容边与 `MotorGlobal --> PID` 边；PID 节点改按值输入输出。
- 登记表：V05 改 `closed + 日期 + E04/E05 证据`；V04 改 `partially closed（Middleware 侧关闭，motor.h 包含 pid.h 归 P3）`；V06 补记“方向唯一所有者已由 E07 硬件证据确认”。
- 日志新增一行，引用 E01–E09 结果。
