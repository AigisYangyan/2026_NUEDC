# 严格计划表：Middleware 层改写（上层仍冻结）

状态：**生效中**
建立：2026-07-17
依据：用户指示 2026-07-17 —— **开始对 Middleware 层进行改写；App 层 Task 任务编排暂不考虑，
Middleware 先管好自己当前层的接口。** §15 上层重置裁定不变、不放宽。

> **本表是 Middleware 层的唯一进度权威。** 开工前必读，完工后必更（与拓扑 §14 同级强制）。
> 架构现状权威仍是 `agent/api_architecture_topology.md`；Driver 层进度权威仍是
> `agent/phase2_driver_rewrite/plan_driver_first_order.md`（已收官）。

---

## 1. 裁定对本阶段施工的硬性影响（§15 的 Middleware 类比）

1. **Middleware 零调用者 = 预期状态。** 现有调用者全部是待重置的 App 存量债（V07/V13），
   不为「让它有人调」保留业务包装；也不因存量债而推迟本层接口收敛。
2. **禁止为假想上层预建接口。** 判据（§15.3 类比）：每个公共接口必须能用
   「**算法本身能做什么**」解释，且当前确有消费者（存量债的机械适配算消费者）；
   只能用「上层可能要什么」解释的接口，删掉。
3. **App 存量调用点只做机械适配**（同符号改名/同语义换 API），目的只有一个：保持构建绿。
   不新增 Task→Middleware 边（现有 `#include` 边已冻结在 arch-baseline，V07 照旧 open，
   关闭时机仍是上层重置）。
4. Middleware 验收 = 主机测试 + 固件构建 + 依赖扫描（§3.3：不得包含 Driver/DL HAL/App，
   不得出现引脚/外设/寄存器/IRQ/SysConfig 宏，不得跨层全局变量交换数据）。
5. 单一所有者铁律（§8.2）：方向修正唯一所有者是 `encoder.c` 的 `s_direction_sign`；
   IMU Yaw 已含模组内 Kalman；PID 内不得新增任何方向反转、二次滤波、单位换算。

## 2. Middleware 层逐项状态

图例：`DONE` 已验收 / `WORK` 施工中 / `GAP` 缺口未做 / `HOLD` 暂缓

| # | 模块 | 目录 | 状态 | 派工 | 说明 |
|---|---|---|---|---|---|
| M01 | pid | `middleware/pid/` | `WORK` | 本契约 | 改为调用者持有上下文；删 5 个可写全局与业务包装（V13 PID 部分的关闭条件） |
| M02 | 循迹误差估计器 | `middleware/`（未建） | `GAP` | 未立 | 12 路灰度位图→横向误差；**位序左右修正的唯一落点=本模块权重表**（承接 phase2 §3/§4.4，禁止在 Driver 加第二反转开关）。派工前需用户确认赛题对误差形态的要求 |

> M02 之外暂无第三个可辩护的 Middleware 模块：IMU Yaw 不允许二次处理（§8.2），
> 坐标/滤波类无现实消费者。发现新缺口先登记本表，不得直接开写。

## 3. M01 契约 —— PID 改为纯算法上下文模块

Status: pending
Goal: `middleware/pid` 成为符合 §3.3 的纯算法模块——无模块级可写全局、无业务编排入口、
调用者显式持有 `Pid_T` 上下文、内部状态只经 API 读写；全部存量调用点机械适配后
主机测试与固件构建双绿。

### 现状证据（2026-07-17 实测）

- `pid.h:49-53`：5 个 `extern PID_T` 可写全局（V13 的 PID 部分；拓扑登记名 `g_PID_instances`
  是命名漂移，实符号为 `g_tAnglePID/g_tLeftMotorPID/g_tRightMotorPID/g_tTrackPID/g_tPositionPID`）。
- `pid.h:57-75`：`pid_Init/pid_closeloop_angle/pid_closeloop_motor/pid_closeloop_track` ——
  angle/track 是空占位（`pid.c:202-207,238-242`），motor 是左右轮业务配对（属未来 Service 职责）。
- 字段级越权：`task1.c:118-135`、`speed_loop.c:33-51`、`2DPlatform_LaserStrike.c:208-226`
  三处本地 `*_reset_pid_runtime()` 直接置零 PID 内部字段；`task1.c:357-362`、
  `speed_loop.c:69-70,77-82`、`vofa_register.c:61-66,72-77`、
  `2DPlatform_LaserStrike.c:655-660,742-752` 直读直写 `.kp/.ki/.kd/.out/.target/...`。
- 调用点全集（Grep 实测，无其他）：`sys_init.c:79`、`vofa_register.c`、`task_groups.c:250`、
  `task1.c`、`speed_loop.c`、`2DPlatform_LaserStrike.c`、`tests/host/test_pid.c`、
  `tests/host/test_encoder.c:44`（链接桩 `void pid_closeloop_motor(void){}`）。
- 主机测试基线：109 PASS / 0 FAIL（其中 test_pid 5 例，全走旧全局接口，须随契约重写）。

### Architecture

- Abstraction：单实例 PID 求解器——配置(增益/限幅/微分滤波)、复位、增量式/位置式两种更新、
  内部分量遥测。每个接口对应现存消费者：
  `Pid_Init/Pid_Reset`（task Enter/Exit 清史）、`Pid_SetGains`（VOFA 在线调参×3 处）、
  `Pid_SetLimits`（2DPlatform 从 VOFA 设输出/积分限幅）、
  `Pid_UpdateIncremental`（speed_loop/task1/task_groups 速度环）、
  `Pid_UpdatePositional`（2DPlatform 像素误差→脉冲）、
  `Pid_GetTelemetry`（speed_loop 读 out、2DPlatform 读 p/i/d/out）。**不再多一个。**
- Hidden state：误差历史、积分累计、微分滤波历史、上次输出——头文件中标注「私有，
  调用者不得直接读写」，唯一读出口是 `Pid_GetTelemetry`。
- Owner layer：Middleware（纯算法，无任何下游依赖，主机可测）。
- Allowed dependency direction：App(存量债)→Middleware、tests→Middleware；
  Middleware→（仅 libc math）。

### 接口冻结

```c
typedef struct {
    float kp, ki, kd;
    float out_limit;       /* 对称输出限幅，<=0 不限 */
    float integral_limit;  /* <=0 时按 out_limit*3.5 推导（沿用现行为） */
    float d_filter_alpha;  /* (0,1] 一阶低通；1.0 不过滤（沿用现行为） */
} Pid_Config_T;

typedef struct { float out, p_out, i_out, d_out; } Pid_Telemetry_T;

typedef struct Pid_T Pid_T;   /* 定义在头文件（调用者需静态分配），字段私有约定 */

void  Pid_Init(Pid_T *pid, const Pid_Config_T *config);
void  Pid_Reset(Pid_T *pid);                       /* 清运行史，保留配置 */
void  Pid_SetGains(Pid_T *pid, float kp, float ki, float kd);
void  Pid_SetLimits(Pid_T *pid, float out_limit, float integral_limit);
float Pid_UpdateIncremental(Pid_T *pid, float target, float feedback); /* 返回限幅后输出 */
float Pid_UpdatePositional(Pid_T *pid, float target, float feedback);
void  Pid_GetTelemetry(const Pid_T *pid, Pid_Telemetry_T *out_telemetry);
```

### Scope

allowed_files（除此之外任何文件被触碰即拒收；拓扑三文件由收工阶段 topo-updater 单独处理）：

- hc-team/middleware/pid/pid.h
- hc-team/middleware/pid/pid.c
- hc-team/app/system/sys_init.c
- hc-team/app/scheduler/vofa_register.c
- hc-team/app/tasks/task_groups.c
- hc-team/app/tasks/task1/task1.c
- hc-team/app/tasks/task1/task1.h
- hc-team/app/tasks/speed_loop/speed_loop.c
- hc-team/app/tasks/speed_loop/speed_loop.h
- hc-team/app/tasks/platform_2d/2DPlatform_LaserStrike.c
- tests/host/test_pid.c
- tests/host/test_encoder.c
- tests/host/Makefile
- agent/phase3_middleware_rewrite/plan_middleware_first_order.md

forbidden_files（重点声明，非全集）：

- board.syscfg
- hc-team/driver/encoder/encoder.c
- hc-team/driver/encoder/encoder.h
- hc-team/driver/motor/motor.c
- hc-team/driver/motor/motor.h
- .claude/hooks/arch-baseline.txt

preserved_behavior：

1. 两种公式的数学行为不变：同参数同输入序列输出相同（重写用例含与旧实现等值的序列断言）。
2. 对称输出限幅、积分限幅 `limit*3.5` 推导、微分一阶低通、NaN/Inf 回退到上次输出——语义均不变。
3. 速度环数据链不变：输入 m/s 按值、输出 ±1000 PWM 尺度、PID 内零单位换算零方向修正（§8.2）。
4. VOFA 调参持久化语义不变：profile 重进不丢当前 cmd 增益（vofa_register 由「经 PID 全局回读」
   改为「memset 前本地暂存/恢复」，观测行为一致）。
5. App 存量调用点行为等价适配：每 task 持有自己的 `static Pid_T` 实例
   （原共享全局本就被各 task Enter 时整体重置，互斥使用，无共享语义损失）。

### App 机械适配落点（逐文件）

| 文件 | 适配 |
|---|---|
| sys_init.c | 删 `pid_Init()` 调用与 `#include`（全局实例消失，无可初始化） |
| vofa_register.c | 删 `#include`；reset 函数 memset 前暂存 cmd 增益、之后恢复（保住持久化语义） |
| speed_loop.c | 2 个 `static Pid_T`；Enter/Exit→`Pid_Reset`；sync→`Pid_SetGains`；Control→`Pid_UpdateIncremental`；遥测→`Pid_GetTelemetry` |
| task1.c / task1.h | 同 speed_loop 模式；`task1_reset_pid_runtime` 删除；头文件注释同步 |
| task_groups.c | 2 个 `static Pid_T` 惰性初始化（limit=1000）；`Task_MotorPidControl` 改双次 `Pid_UpdateIncremental` |
| 2DPlatform_LaserStrike.c | `PID_T pid`→`Pid_T`；本地 reset→`Pid_Reset`；sync→`Pid_SetGains`+`Pid_SetLimits`；update→`Pid_UpdatePositional` 返回值+`Pid_GetTelemetry` |
| test_encoder.c / Makefile | 删 `pid_closeloop_motor` 链接桩，`test_encoder` 目标改为直接链接 `$(PID_SRC)` |
| test_pid.c | 全量重写为新 API：保留 5 个行为等值用例（符号方向/饱和/稳态零误差/增量累积/双实例隔离）+ 新增位置式积分限幅、Reset 清史保配置、NaN 回退、微分低通、SetLimits 生效等，总数 ≥10 |

### Steps

1. 重写 test_pid.c（新 API，先红：新符号未实现，编译失败即旧实现上的失败复现）。
2. 重写 pid.h/pid.c 最小实现；同步删除旧接口（无双轨兼容）。
3. 逐文件机械适配 App 存量调用点与 test_encoder 链接。
4. 复现 E01–E05；派 arch-auditor；topo-updater 收工（V13 的 PID 部分改注«已由 M01 关闭，
   残余 g_eSysFlagManage/TrackN 随上层重置»，并修正 g_PID_instances 命名漂移）。

### Verification（证据行，冻结）

- E01 command: Grep `\bPID_T\b|g_tAnglePID|g_tLeftMotorPID|g_tRightMotorPID|g_tTrackPID|g_tPositionPID|pid_closeloop|pid_formula_|pid_out_limit|\bpid_Init\b|reset_pid_runtime` in `hc-team/` 与 `tests/`
- E01 expected: 0 命中（基线：hc-team 115 处/10 文件 + tests 16 处/2 文件，已逐条核对全部应消失）
- E01 negative_check: 新符号 `Pid_Init` 等不得被该模式误伤（大小写敏感已核）

- E02 command: Grep `#include\s+"(driver/|app/|ti_msp)|DL_[A-Z]|NVIC_|__disable_irq|GPIO_|IRQ` in `hc-team/middleware/`
- E02 expected: 0 命中（§3.3 纯净性：无 Driver/DL HAL/App/外设符号）
- E02 postcondition: middleware 仅含 `pid.h`/`<math.h>` 级依赖

- E03 command: Grep `pid\.\w+\s*=|pid->\w+\s*=|=\s*\w*pid\.(out|p_out|i_out|d_out|kp|ki|kd)|=\s*\w*pid->(out|p_out|i_out|d_out|kp|ki|kd)` in `hc-team/app/`
- E03 expected: 0 命中（基线 44 处/3 文件；字段级越权全部改经 API）
- E03 negative_check: `&s_xxx_pid` 取址传参、`Pid_Config_T` 局部配置体不落入该模式（已预演核对）

- E04 command: PowerShell `rtk make -C tests/host all`（clean 后全量）
- E04 expected_exit: 0
- E04 postcondition: 0 FAIL；test_pid 用例 ≥10 全 PASS；全套 PASS 总数 ≥114（基线 109 − 旧 pid 5 + 新增 ≥10）

- E05 command: PowerShell `rtk make -C Debug all`
- E05 expected_exit: 0
- E05 postcondition: 0 error / 0 warning，正常产出并链接 .out（唯一固件构建行）

### Stop conditions

- 适配中发现除已列 12 个调用文件之外的 PID 消费者 → 停，契约修订单独提交。
- 任何适配需要新增 Task→Driver/Middleware `#include` 边（arch-guard 报基线外新增）→ 停。
- 主机测试暴露新旧公式数值不等且原因不明 → 停，禁止改断言凑绿。

## 4. 历史调参常数存档（随旧全局删除，防丢失）

| 环 | kp | ki | kd | 输出限幅 | 出处 |
|---|---|---|---|---|---|
| 角度环 g_tAnglePID | 0.004 | 0.0001 | 0.0015 | 0.4 | pid.c:95（旧板实测值，仅作未来 Service 调参起点参考） |
| 循迹环 g_tTrackPID | 0.00872 | 0 | 0.0030 | 0.6 | pid.c:107（同上） |
| 速度环左右 / 位置环 | 0 | 0 | 0 | 1000 | pid.c:99-111（无调参史） |

## 5. 维护规则

1. 每完成一个 Middleware 派工，必须更新 §2 表格状态与派工号。
2. 新发现的 Middleware 缺口/债，必须先登记 §2 再谈施工。
3. 契约证据行冻结后如需修改，单独 commit 说明改了什么、为什么（embedded-closed-loop 铁律）。
4. 本表与代码冲突时以代码为准：先改表成真实状态，再继续设计。
