# P1-FIX：`mspm0_runtime` 收口与职责移交计划

计划所有者：Codex（本文件即验收契约，REASONIX 不得修改验收条目）  
制定日期：2026-07-16  
状态：done（2026-07-16 Codex 验收 P1F.T1 通过；P1F.T2 随硬件验收取消而删除）  
背景：REASONIX 于 2026-07-16 提交 `SELF_CHECK_BLOCKED`，证据为 `plan1_mspm0_runtime_rewrite.md:3` 仍为 in_progress（P1.3–P1.5 待执行），P3/P4/P5 的“P1/P2 已验收”前置条件不成立。本计划是 Codex 对该阻塞的正式处置：把 P1 残余工作切分为“本计划收口项”与“已移交项”，给出可验收的关闭路径。  
流程（2026-07-16 简化）：Codex 计划 → REASONIX 施工并提交行级完成报告 → Codex 精简验收（自检阶段已撤销）。

REASONIX 施工时必须携带以下指令：

```text
Execute only this task. Reports are claims, not proof. Do not mark complete until every command and observable postcondition has been reproduced. Never ignore command errors. Do not close topology items without direct evidence.
```

状态标签与证据行规则同 plan3/4/5：`CONSTRUCTION_*` / `CODEX_*`；证据行 E01–E07 由本计划固定分配。

## 1. P1 残余工作的权威切分（Codex 裁定）

| plan1 条目 | 处置 | 承接契约 |
|---|---|---|
| P1.1 时基去反向依赖 | 已完成（V01 closed 2026-07-13） | 本计划 E06 硬件行补 Clock 精度证据 |
| P1.2 板级初始化归位 | 已完成（V03 partially closed） | 本计划 E01–E05 复核 |
| P1.3 Vision 与 VOFA UART | **移交** | `plan5_uart_role_drivers.md` P5.T1 / P5.T2 |
| P1.4 StepMotor UART | **移交** | `plan5_uart_role_drivers.md` P5.T3（含 UART2 归属前置） |
| P1.5.1 编码器共享 IRQ | 已完成（P2.2 落地 BoardGpio 快照） | plan2_fix E07–E09 硬件行覆盖 |
| P1.5.2 按键共享 IRQ | **移交** | `plan4_key_rewrite.md` P4.T1 / P4.T2 |
| P1.5.3 删除无调用证据接口 | **本计划 P1F.T1 执行** | 本文件 E01–E05 |
| P1.5.4 runtime 大一统接口最终瘦身 | 依赖 P4/P5 完成，登记为 P7 收尾 | 总计划 P7 |

移交后，P1 的完成定义收窄为：P1.1 + P1.2 + P1F.T1 死接口清理 + E06 时钟硬件证据。P1.3–P1.5 的验收由 P4/P5 的 E 行承担，不得在两处重复关闭同一项。

## 2. 当前问题证据（Codex 于 2026-07-16 逐条核实）

无调用者的公共接口（`rg` 全仓库仅命中声明与定义）：

- `Mspm0Runtime_InitTick()`：`mspm0_runtime.h:26`、`mspm0_runtime.c:267`。
- `Mspm0Runtime_StartMotorPwm()`：`mspm0_runtime.h:38`、`mspm0_runtime.c:309`；`motor.h:21` 仅注释提及。
- `Mspm0Runtime_DelayUs()`：`mspm0_runtime.h:47`、`mspm0_runtime.c:334`。
- `Mspm0Runtime_SendVision()`：`mspm0_runtime.h:66`、`mspm0_runtime.c:411`。
- `Mspm0Runtime_SendVofaByte()`：`mspm0_runtime.h:69`、`mspm0_runtime.c:426`。
- `Mspm0Runtime_SendVisionByte()`：`mspm0_runtime.h:70`、`mspm0_runtime.c:431`。
- `Mspm0Runtime_IsVofaTxBusy()`：`mspm0_runtime.h:55`、`mspm0_runtime.c:380`。
- `Mspm0Runtime_IsVisionTxBusy()`：`mspm0_runtime.h:56`、`mspm0_runtime.c:385`。

deprecated 包装 `Mspm0Runtime_GetTickMs()`（`mspm0_runtime.c:314-317` 直通 `Clock_NowMs()`）现有 12 个调用点：`IMU.c:76,105,110`、`oled_hardware_i2c.c:448,462`、`mpu6050.c:88`、`task1.c:107`、`stepmotor_bus.c:193,349`、`vision_bus.c:91`、`vision_coord.c:637`。

确有调用者、本计划保留的接口（P5 负责后续归属）：`Mspm0Runtime_DelayMs()`（`at24cxx.c:163,204`，EEPROM 写周期等待的真实需求）、`Mspm0Runtime_InitUartDma`、三组 RX/TX 回调注册（P5.T3 删除）、`Mspm0Runtime_SendStepmotor/SendStepmotorByte/SendVofa/IsStepmotorTxBusy`、`Mspm0Runtime_GetEncoderCounts`。

## 3. 施工任务

### P1F.T1 删除死接口并迁移 GetTickMs 调用点

Status: done（2026-07-16 CODEX_ACCEPTED：E01–E05 通过，提交 455a968）
Goal: runtime 公共头只保留有调用证据的接口；全仓库时间查询统一为 `Clock_NowMs()`。

Evidence:
- §2 全部行号。

Architecture:
- Abstraction: 不变（本任务只做减法与调用点替换）。
- Hidden state: 不变。
- Owner layer: Driver。
- Allowed dependency direction: 调用点 -> `driver/clock/clock.h`（Driver 内部与既有 App→Driver 存量边同向，不新增违规类别；`task1.c` 原本即持有 `Task1_API --> Runtime_API : time` 存量边，改为指向 Clock 后在拓扑同步改边）。

Scope:
- allowed_files: `hc-team/driver/mspm0_runtime/mspm0_runtime.c`、`hc-team/driver/mspm0_runtime/mspm0_runtime.h`、`hc-team/driver/imu/IMU.c`、`hc-team/driver/oled/oled_hardware_i2c.c`、`hc-team/driver/MPU6050/mpu6050.c`、`hc-team/driver/motor/motor.h`（仅第 21 行注释更新）、`hc-team/app/tasks/task1/task1.c`、`hc-team/app/tasks/platform_2d/stepmotor_bus.c`、`hc-team/app/tasks/platform_2d/vision_bus.c`、`hc-team/app/tasks/platform_2d/vision_coord.c`
- forbidden_files: `hc-team/app/scheduler/*`、`hc-team/middleware/**`、`project/mspm0/board.syscfg`、`hc-team/driver/key/*`、`hc-team/driver/encoder/*`
- preserved_behavior: 所有调用点的时间语义逐字节等价（GetTickMs 本就是 Clock_NowMs 直通）；`DelayMs` 行为不变；固件功能零变化。

Preconditions:
- 基线 `rtk make -C tests/host all` 通过（REASONIX 2026-07-16 已记录 13 项通过）。

Steps:
1. 先运行 E01/E02 扫描，记录旧代码上的非零命中作为 RED 基线。
2. 删除 §2 死接口的声明与实现；12 个 `GetTickMs` 调用点改 `Clock_NowMs()` 并补 `#include "driver/clock/clock.h"`；随后删除 `Mspm0Runtime_GetTickMs`/`Mspm0Runtime_InitTick`。
3. 只重构本任务引入的代码；不顺手改动任何回调或 FIFO 逻辑（P5 所有权）。
4. 验证通过后更新拓扑（见 §4）。

Verification:
- E01 command: `rg 'Mspm0Runtime_InitTick|Mspm0Runtime_StartMotorPwm|Mspm0Runtime_DelayUs|Mspm0Runtime_SendVision|Mspm0Runtime_SendVofaByte|Mspm0Runtime_IsVofaTxBusy|Mspm0Runtime_IsVisionTxBusy' hc-team tests`
- E01 expected_exit: 1
- E01 postcondition: 全仓库零命中（含 `SendVisionByte`，被 `SendVision` 前缀覆盖）。
- E01 negative_check: 不得以 `#if 0` 或注释保留尸体代码。
- E02 command: `rg 'Mspm0Runtime_GetTickMs' hc-team tests`
- E02 expected_exit: 1
- E02 postcondition: 零命中；12 个原调用点全部改为 `Clock_NowMs()`（diff 中逐一可见）。
- E02 negative_check: 不得漏改任何一个文件后靠本地 static 包装绕过扫描。
- E03 command: `rtk make -C tests/host all`
- E03 expected_exit: 0
- E03 postcondition: 既有主机测试全部通过，数量不少于基线 13 项。
- E03 negative_check: 不得删除或跳过任何既有用例。
- E04 command: `rtk make -C Debug clean` 后 `rtk make -C Debug all`
- E04 expected_exit: 0
- E04 postcondition: 新鲜 `Debug/*.out`；map 中不存在 E01/E02 列出的符号；无新增 warning（对比基线）。
- E04 negative_check: 不使用旧目标文件缓存。
- E05 command: `git diff --stat`
- E05 expected_exit: 0
- E05 postcondition: 改动只落在 allowed_files；无关未提交修改未被触碰。
- E05 negative_check: forbidden_files 零改动。

Hardware gate:
- 本任务不执行硬件操作。

Stop conditions:
- 发现 E01 所列接口存在扫描未覆盖的调用者（立即报告并停止删除该接口）；替换后编译暴露隐藏的头文件循环。

### P1F.T2 时钟精度硬件证据（P1 收口硬件门）

Status: removed（2026-07-16 用户裁定取消硬件验收，时钟精度由用户自测，E06/E07 作废）
Goal: 为收窄后的 P1 完成定义补齐 plan1 §7 的时钟硬件验收证据。

Evidence:
- `plan1_mspm0_runtime_rewrite.md:156`（10 分钟对外部基准、误差上限公式、Scheduler elapsed 一致性）。

Scope:
- allowed_files: `agent/phase2_driver_rewrite/runtime_clock_baseline.md`（新建，记录测量）；可临时使用现有 VOFA/OLED 遥测通道输出计数，但临时代码必须在记录后撤销且 `git diff` 为空
- forbidden_files: 全部生产源码的持久修改
- preserved_behavior: 全部。

Preconditions:
- P1F.T1 通过；板卡与外部计时基准（手机秒表精度不足，需 PC 时钟或仪器）可用。

Steps:
1. 按“所选时钟源数据手册最差精度 × 600 秒 + 2 ms 测量分辨率”预先计算并记录允许误差上限。
2. 连续运行 10 分钟，对比 `Clock_NowMs()` 与外部基准；同时记录 Scheduler 累计 elapsed 与 `Clock_NowMs()` 无符号差值。

Verification:
- E06 postcondition: 实测偏差 ≤ 预计算上限；Scheduler 累计 elapsed 与 `Clock_NowMs()` 差值恒为 0（不丢 tick）；测量方法、原始读数、上限算式全部写入 baseline 文档。
- E06 negative_check: 不得用示教性短时测量（<10 分钟）外推；临时遥测代码撤销后 `git diff` 为空。
- E07 postcondition: baseline 文档记录本计划 §1 移交表已同步进 `plan1_mspm0_runtime_rewrite.md` 状态头与总计划表（引用具体行）；P1 状态由 Codex 在验收时改为 `done (scope narrowed, P1.3-P1.5 transferred)`。
- E07 negative_check: REASONIX 不得自行把任何计划状态改为 `done`。

Hardware gate:
- （作废：硬件验收已取消。）

Stop conditions:
- 无可信外部计时基准；实测超出上限（回报 Codex，禁止调整上限凑数）。

## 4. 拓扑更新契约（验证通过后才允许改）

- 类图 `Runtime_API`：删除 `Mspm0Runtime_GetTickMs() @deprecated`、`Mspm0Runtime_DelayUs()`、`SendXxxByte()` 中无调用者项、`IsXxxTxBusy()` 中无调用者项；保留项按 §2 保留清单。
- 边更新：`Task1_API --> Runtime_API : time` 改为 `Task1_API --> Clock_API : time`（存量 App→Driver 边，方向不变）；`OLED_API/MPU6050_API/IMU_API --> Runtime_API : time and delay` 中 time 部分改指向 `Clock_API`。
- 登记表：V02/V03 状态不变（归 P5/P4 关闭）；新增一行记录 P1 范围收窄裁定及依据（本文件 §1）。
- 日志新增一行，引用 E01–E07 结果。
