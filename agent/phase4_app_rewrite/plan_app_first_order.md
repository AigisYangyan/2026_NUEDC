# Phase 4 —— App 上层整体重置：严格计划表

> 本文件是 App 层重写的逐项状态与阻塞源记录：开工前必读，完工后必更。
> 架构权威仍是根 `AGENTS.md`；现状权威仍是 `agent/api_architecture_topology.md`。
> 本阶段沿用 phase2/phase3 的闭环铁律：**契约（含全部证据行）先提交冻结，再写第一行生产代码**；
> 契约行有错在**单独显式提交**中修订，绝不与满足它的代码同一提交。

## 1. 裁定与范围（2026-07-17）

- **§15 解除**：用户宣布「现在开始处理app层」——上层重置开始，AGENTS.md §15 第 1、2、4 条
  自动失效，§3.4 Service 层规则对新代码即时生效。
- **补充裁定（同日，用户原话要点）**：Task 层任务只做**部分保留**，作为「电赛一个小题的实际运行」
  的薄编排，**最后**编写；现有 Task 里的大量内容应当作为框架下沉——**算法归 Middleware**
  （已有 pid / track_error），**控制环的触发（循迹环触发、速度环触发）与多环触发+逻辑编排归 Service**。
- **旧 app/\*\* 存量代码处置**：scheduler / system / tasks / ui 现存文件继续**冻结**——
  保持构建绿、不逐文件缝补、不作为新代码参照（AGENTS.md §15.6 仍然有效）；
  在 T01 阶段被新 Task 整体替换时删除，届时一并关闭 V03 / V07 / V13 残余 / V15。
- **新 Service 零调用者是预期状态**（Task 未写前），与 Driver 先行阶段同一逻辑：
  不得为「让它有人调」把新 Service 临时接进旧 Task。
- 每个 Service 公共接口必须能用「底盘/器件/算法能做什么」解释；只能用「未来 Task 可能要什么」
  解释的接口，删掉。

## 2. 编排顺序与理由

**顺序：A00 → S01 → S02 → S03 → S04 → SCH01 → UI01 → T01；SYS01 随各阶段增量推进。**

1. **依赖方向强制 Service 先行**：依赖矩阵里 Task/Scheduler/UI 只允许调 Service——
   先有 Service，上层才有东西可写。这也是用户裁定的重心。
2. **S01（底盘速度内环）先于 S02（循迹外环）**：多环级联的数据方向是「外环输出 → 内环目标」，
   内环必须先存在并可独立验收，外环才有落点（Service 同层受控调用）。
3. **S03 遥测调参服务在控制服务之后**：调参/遥测上下文的形状由已存在的服务决定，
   先建就是假想接口（V15 的教训）。
4. **S04 / SCH01 / UI01 靠后**：菜单项与生命周期编排的形状取决于 Service 能力面。
5. **T01 最后**：赛题 Task 是多环触发+逻辑编排的最终消费者；写它时一次性替换并删除
   旧 `tasks/**`，避免中途断链（构建始终保持绿）。

## 3. 模块状态表

| ID | 模块 | 新目录 | 吸收/替代的存量 | 关闭的债 | 状态 |
|---|---|---|---|---|---|
| A00 | 计划 + 裁定解除记录 | `agent/phase4_app_rewrite/` | — | — | `DONE`（bffdecf + baseline chore c958a3f） |
| S01 | chassis 底盘速度环服务 | `app/service/chassis/` | speed_loop.c、task1 速度部分、task_groups 采样所有权 | V07（部分）、V10（部分） | `DONE`（契约 bffdecf，修订 926bac0；代码 8a611d5；审计处置 69c29fa。E01 0 命中 / E02 无越界 / E03 140 PASS 0 FAIL＝128 基线+12 / E04 exit 0、0 诊断、chassis.o 进链接） |
| S02 | line_follow 循迹服务（外环+丢线策略） | `app/service/line_follow/` | track_follow.c、task1 循迹部分、gray_test | V03、V03-DUP、V07（部分） | `DONE`（契约 6dfdc85，修订 88010fd；代码 bb4825c；审计处置 53e9967。E01 0 命中 / E02 无越界 / E03 159 PASS 0 FAIL＝140 基线+19 / E04 exit 0、0 诊断、两 .o 进链接。Q5 关闭：丢线策略显式重建于 lost_line） |
| S03 | 遥测/调参链路服务（VOFA） | `app/service/tuning/` | vofa_register.c | V15（替代已建成，旧边待 T01）、V19（closed） | `DONE`（契约 ed4f416，修订 57b54de；代码 d0e4996；审计处置 5a4f089。E01 0 命中 / E02 无越界 / E03 173 PASS 0 FAIL＝159 基线+14 / E04 exit 0、0 诊断、两 .o 经 linkInfo.xml 确证进链 / E05 `u8` 0 命中。Q2 定案入 §5） |
| S04 | 人机输入/显示服务（Key/OLED 包装） | `app/service/hmi/` | menu 对 Key/OLED 的直调、task_groups UI 泵送 | V14 的基础 | `DONE`（契约 f8311c8；代码 2dac572；审计处置 ad5ca08。E01 0 命中 / E02 无越界 / E03 185 PASS 0 FAIL＝173 基线+12 / E04 exit 0、0 诊断、hmi.o 经 linkInfo.xml 确证进链） |
| S05 | 云台/视觉服务群（platform_2d 下沉） | `app/service/`（契约时拆分） | vision_bus/vision_coord/stepmotor_bus/2DPlatform | stepmotor_bus 违规群 | 赛题明确后 |
| SCH01 | 调度器重写 | `app/scheduler/` | task_scheduler.c、run_registry.c | V13 残余（g_eSysFlagManage） | `DONE`（Q1 定案 74d421e；契约 56ced13，修订 c6bcc4a；代码 e801caf；审计处置 6bfe3f4。E01 0 命中 / E02 无越界 / E03 200 PASS 0 FAIL＝185 基线+15 / E04 exit 0、0 诊断、scheduler.o 经 linkInfo.xml 确证进链。V13 残余本体仍待 T01 删旧文件时关闭） |
| UI01 | 菜单重写 | `app/ui/` | menu_core/menu_pages | V14 | 待 SCH01+S04 |
| SYS01 | 装配入口更新 | `app/system/` | sys_init.c 增量改造 | — | 随各阶段 |
| T01 | 赛题 Task（薄编排）+ 旧 tasks 整体删除 | `app/tasks/` | 全部旧 `tasks/**` | V03/V07/V13 残余/V15 全关，baseline 清空 | **最后** |

## 4. 通用施工规则（每模块适用）

- 每模块走 embedded-closed-loop 完整闭环：契约冻结 → TDD 红 → 施工 → 逐行复现证据 →
  arch-auditor → topo-updater → 收官提交。
- **主机测试模式**：真实 `service/*.c` + 真实 `driver/*.c` + fake 端口层
  （既有 `fake_board_gpio.c` / `fake_motor_hw.c` / `fake_uart_port.c` 等；缺什么补什么）。
  fake 只允许伪装端口/硬件边界，不许伪装被测 Service 或 Driver 本体。
- **Service 头不暴露 Driver 类型**（AGENTS.md §3.4「隐藏用了哪些 Driver」）；
  左右轮等语义用 Service 自己的枚举。
- **单一所有者复查先于新增处理**：输出限幅在 `Pid_T.cfg.out_limit`，slew/换向死区/命令超时
  在 `motor.c` 状态机（V12），步进限幅在 `emm42.c`，编码器方向在 `encoder.c s_direction_sign[]`——
  Service 一律不复做第二份。
- Debug 构建接线遵循 P9.T1：仓库只跟踪 `Debug/makefile`；`sources.mk`/`ccsObjs.opt`/
  `subdir_*.mk` 是本地生成物，改动不入库。
- 主机测试与固件构建一律经 PowerShell 跑 `rtk proxy make`（计数取证）/ `rtk make`。

## 5. 待决问题登记（各自契约时解决，不预支设计）

| # | 问题 | 归属 |
|---|---|---|
| Q1 | ~~Scheduler 的时间来源：矩阵禁止 Scheduler 调 Driver，而 Clock 是 Driver~~ **已定案（2026-07-17 用户确认，选 A）**：System 装配层供给节拍——SCH01 新调度器不含 `clock.h`，时间以参数注入（形如 `Scheduler_Run(uint32_t now_ms)`，具体签名 SCH01 契约冻结时定），由 `app/system` 主循环读 `Clock_NowMs()` 喂入。不建 systime Service：纯透传接口只能用「让 Scheduler 有人可调」辩护，违反 §1「接口须以能力解释」裁定，且会在 Clock 之外造第二个时间查询面。红利：调度器成为零依赖纯逻辑，主机测试免链 fake clock（规避 fake_i2c_port 自带 `Clock_NowMs` 的符号重定义坑）；同款先例 `LostLine_Tick(ctx, elapsed_ms, …)`。 | SCH01 契约 |
| Q2 | ~~VOFA 命令解析与分发的最终归属~~ **已定案（S03 契约 §9）**：字节流解析归 Driver `vofa_run()`（V09 任务上下文边界不动）；解析结果的**分发与应用**归 `app/service/tuning`（唯一收口，cmd→被调 Service API 单向应用）。Task 层永不直接触碰 uart_vofa。 | S03 契约 |
| Q3 | 赛题（电赛小题）具体定义与 Task 编排内容，待用户给题。 | T01 契约 |
| Q4 | ~~`arch-baseline.txt` vofa_register.c→pid.h 滞后行~~ **已关闭（S03 复核 2026-07-17）**：该行已不在 baseline 中（A00 chore 已清）；现存第 9 行 vofa_register.c→uart_vofa.h 与代码事实一致，属冻结违规如实登记。 | A00 随手 chore |
| Q5 | ~~S02 丢线策略需显式重建~~ **已关闭（S02）**：`lost_line` 子模块=方向记忆+固定回退+有界超时（超时上限是新增安全项，旧实现没有）。 | S02 契约 |

## 6. S01 契约（chassis 底盘速度环服务）——冻结

- **task_id**: S01-chassis
- **goal**: 新建 `app/service/chassis/` 底盘速度环服务：编码器采样触发 → 双轮增量 PID →
  电机输出，含确定性停止接口。成为「速度环触发」框架与编码器采样节奏的唯一所有者
  （接替 task_groups 的采样所有权地位；旧链路冻结期间无人调用新服务，不存在双采样运行态）。
- **接口辩护**（底盘能做什么）：两轮差速底盘能按目标轮速闭环行驶、能刹停、能报告实际轮速——
  仅此四类能力成为公共面。

### 6.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/chassis/chassis.h` | 新建 |
| `hc-team/app/service/chassis/chassis.c` | 新建 |
| `tests/host/test_chassis.c` | 新建 |
| `tests/host/fake_clock.c` | 新建（Clock_NowMs 可设定 fake，仅 test_chassis 链接） |
| `tests/host/Makefile` | 追加 test_chassis 目标/clean/.PHONY |
| `.gitignore` | 追加 test_chassis / test_chassis.exe |
| `Debug/makefile` | 登记 chassis.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |
| `tests/host/fake_board_gpio.c` | **修订追加（evidence 见 §6.5）**：加采样失败注入开关 |

forbidden_files：`hc-team/app/tasks/**`、`hc-team/app/scheduler/**`、`hc-team/app/system/**`、
`hc-team/app/ui/**`、`hc-team/driver/**`、`hc-team/middleware/**`、tests/host 既有 `test_*.c`
与既有 `fake_*.c`。（Debug/ 下本地生成物不入库，不列。）

### 6.2 公共接口（最小面）

```c
typedef enum { CHASSIS_SIDE_LEFT = 0, CHASSIS_SIDE_RIGHT, CHASSIS_SIDE_COUNT } Chassis_Side;
typedef struct {
    float target_mps[CHASSIS_SIDE_COUNT];
    float feedback_mps[CHASSIS_SIDE_COUNT];
    float pid_out[CHASSIS_SIDE_COUNT];
} Chassis_Telemetry_T;

void Chassis_Init(void);          /* 目标清零 + PID 初始化（增益 0）+ 周期基准复位；不发电机命令 */
void Chassis_SetSpeedGains(Chassis_Side side, float kp, float ki, float kd);
void Chassis_SetTargetMps(float left_mps, float right_mps);
void Chassis_Update(void);        /* 自取 Clock_NowMs；不足周期直接返回；到期执行 采样→PID→输出 */
void Chassis_Stop(void);          /* 目标清零 + PID 复位 + Motor_BrakeAll；确定性停止（§8.1） */
void Chassis_GetTelemetry(Chassis_Telemetry_T *out);
```

- 控制周期 `CHASSIS_CONTROL_PERIOD_MS = 10`（沿用旧速度环周期）；`Chassis_Update()` 允许被
  更快调用，内部用 `Clock_NowMs()` 无符号减法门控；到期路径：
  `Encoder_Update(elapsed) → Encoder_GetSnapshot → Pid_UpdateIncremental ×2 → Motor_SetOutput ×2 → Motor_Update(elapsed)`。
- 单位链：目标/反馈 m/s（编码器 Driver 出口口径）；PID 输出 ±1000 PWM
  （`out_limit = MOTOR_OUTPUT_MAX`，限幅唯一所有者是 Pid cfg）。
- **不复做的保护**（单一所有者）：slew、换向过零+死区、100ms 命令超时归零、刹车真值表
  全部在 `motor.c`（V12 closed）；本服务只负责「确定性停止接口 + 初始化不发命令」。
- **已知空缺（显式记录，不加无依据防御）**：目标速度不设限幅——无实测最大轮速依据，
  增量 PID + out_limit 已界定输出；实车标定后若需要，由本服务作为唯一目标限幅所有者补上。
- Enter/Exit 生命周期语义（旧 SpeedLoop_Enter/Exit 的 profile 注册、VOFA 增益同步）**不进入** S01：
  那是调度/遥测编排，归 SCH01/S03/T01。

### 6.3 preserved_behavior

- 旧 `app/**`、`driver/**`、`middleware/**` 零改动；主机既有 128 用例全过；固件行为不变
  （chassis.o 进链接但零调用者）。

### 6.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/|app/scheduler/|app/ui/|app/system/|ti_msp_dl_config|ti/driverlib`（path=`hc-team/app/service/chassis`，`#include` 行） | 0 命中 |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §6.1 | 无 allowed_files 之外的改动 |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥138 PASS / 0 FAIL（128 基线 + ≥10 新用例），新用例必含安全项：Init 后零电机命令、Stop 触发 BrakeAll 且目标/PID 清零、未到期 Update 不产生输出、elapsed 正确传递给 Encoder_Update 与 Motor_Update、增益生效、遥测一致 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、chassis.o 进入 .out 链接 |

### 6.5 契约修订记录

- **修订 1（2026-07-17，本提交）**：`tests/host/fake_board_gpio.c` 从 forbidden 移入 allowed。
  原因：arch-auditor 重要级 finding——chassis.c 采样失败安全分支（不刷新命令→Driver 100ms
  超时归零）在真实与 fake 路径上均不可触发，安全宣称无验证途径（违 §8.3 可验证条件）。
  处置采纳审计建议 (a)：给 fake 加 `FakeBoardGpio_SetSnapshotFail` 注入开关并补时序测试，
  E03 预期相应 +1 例（≥139+1）。既有测试不受影响（开关默认关闭）。

## 7. 维护规则

- 每完成一个模块：更新 §3 状态列（含契约/代码提交哈希）、追加该模块契约章节、
  在拓扑索引 §10 记一行日志。
- 契约修订必须单独提交并在本文件中注明修订原因与提交哈希。

## 8. S02 契约（line_follow 循迹外环服务）——冻结

- **task_id**: S02-line_follow
- **goal**: 新建 `app/service/line_follow/`：灰度位图采样触发 → Middleware 加权重心误差 →
  外环 PID → 差速目标喂 chassis 内环（Service 同层受控调用）。「循迹环触发 + 多环触发/级联」
  的唯一所有者：`LineFollow_Update()` 推进外环并级联推进 `Chassis_Update()`。
  丢线恢复策略按 phase3 §5.2 移交备忘**显式重建**（Q5 关闭），落在独立子模块 `lost_line`。
- **文件层级**（用户指令 2026-07-17：不要把所有代码放在一个文件里）：
  `line_follow.{h,c}` = 公共面 + 触发/编排/状态机；`lost_line.{h,c}` = 丢线恢复策略
  （服务内私有模块，调用者持有上下文，可独立主机测试）。
- **接口辩护**：循迹功能能做什么——沿线行驶、丢线后有界恢复、超时安全停车、报告状态与
  误差遥测、可调外环增益。仅此成为公共面。

### 8.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/line_follow/line_follow.h` / `.c` | 新建 |
| `hc-team/app/service/line_follow/lost_line.h` / `.c` | 新建 |
| `tests/host/test_lost_line.c`、`tests/host/test_line_follow.c` | 新建 |
| `tests/host/Makefile` | 追加两个 target/clean/.PHONY |
| `.gitignore` | 追加两个测试产物 |
| `Debug/makefile` | 登记 line_follow.o、lost_line.o |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |

forbidden_files：`hc-team/app/service/chassis/**`（只调用不修改）、`hc-team/app/{tasks,scheduler,system,ui}/**`、
`hc-team/driver/**`、`hc-team/middleware/**`、tests/host 既有 `test_*.c` 与 `fake_*.c`
（fake_gray_port 已有注入面）。

### 8.2 公共接口（最小面）

```c
typedef enum { LINE_FOLLOW_IDLE, LINE_FOLLOW_TRACKING,
               LINE_FOLLOW_RECOVERING, LINE_FOLLOW_LOST } LineFollow_State;
typedef struct {
    float    pitch_mm;          /* 探头机械间距(>0)；机械定案后实测 */
    bool     bit0_is_left;      /* 位序唯一修正点（H2 实测后定），透传 TrackError */
    float    base_speed_mps;    /* 巡线基速 */
    float    diff_limit_mps;    /* 差速修正限幅 = 外环 Pid out_limit（唯一所有者） */
    float    recovery_error_mm; /* 丢线回退误差幅值（旧±27 语义重建，建议≈2.7×pitch） */
    uint32_t lost_timeout_ms;   /* 恢复期上限，超时→LOST+停车 */
} LineFollow_Config_T;
typedef struct { uint16_t dark_bitmap; float error_mm; float diff_cmd_mps;
                 LineFollow_State state; } LineFollow_Telemetry_T;

void LineFollow_Init(const LineFollow_Config_T *config); /* 静默；不动底盘 */
void LineFollow_SetGains(float kp, float ki, float kd);  /* 外环 PID，运行时可调 */
bool LineFollow_Start(void);   /* 配置有效(pitch>0)→TRACKING；否则 false 并保持 IDLE */
void LineFollow_Update(void);  /* 自门控 10ms；每次调用末尾恒推进 Chassis_Update() */
void LineFollow_Stop(void);    /* →IDLE + Chassis_Stop */
LineFollow_State LineFollow_GetState(void);
void LineFollow_GetTelemetry(LineFollow_Telemetry_T *out);
```

- **差速符号约定**：+误差 = 线在车右（M02 口径）→ 需右转 → 左快右慢：
  `left = base + c`，`right = base − c`，c 与误差同号（外环 PID 位置式，输入=误差，
  输出=差速修正 m/s，out_limit = diff_limit_mps）。
- **状态机**（转移表随 .c 注释交付）：IDLE→(Start 且配置有效)→TRACKING；
  TRACKING→(位图=0)→RECOVERING（回退误差 = sign(最近有效误差)×recovery_error_mm，
  从未见线则 0=直行找线）；RECOVERING→(重获线)→TRACKING；
  RECOVERING→(累计≥lost_timeout_ms)→LOST（Chassis_Stop，保持静默直至 Stop/Start）；
  任意态 Stop→IDLE+Chassis_Stop。全黑（十字）重心≈0 = 正常直行通过，特征识别归 T01。
- **单一所有者声明**：误差量化只在 `middleware/track_error`（本服务是其第一个消费者，
  不复算）；位序反转只经 `bit0_is_left` 透传（gray.h 位序警告的落点，全链路仍仅一个反转点）；
  丢线策略只在 `lost_line`（旧 track_follow.c 的 ±27 回退是冻结债，T01 删除，过渡期双实现登记拓扑）；
  差速限幅唯一所有者 = 外环 Pid cfg；轮速闭环与电机保护归 chassis/S01 既有所有者。

### 8.3 preserved_behavior

- `app/service/chassis/**`、旧 `app/**`、`driver/**`、`middleware/**` 零改动；
  主机既有 140 用例全过；固件行为不变（新 .o 进链接但零调用者）。

### 8.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/|app/scheduler/|app/ui/|app/system/|ti_msp_dl_config|ti/driverlib`（path=`hc-team/app/service/line_follow`） | 0 命中 |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §8.1 | 无越界改动 |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥155 PASS / 0 FAIL（140 基线 + ≥15 新用例），必含安全项：Start 前 Update 不动底盘、丢线回退方向与幅值正确、超时→LOST 且底盘刹车、重获线回 TRACKING、差速符号（+误差→左快右慢）、bit0_is_left 反转生效、Stop 确定性 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、line_follow.o 与 lost_line.o 进入 .out 链接 |

### 8.5 契约修订记录

- **修订 1（2026-07-17，本提交，审计后处置——用户选定方案 b）**：
  1. §8.2 中「每次调用末尾恒推进 Chassis_Update()」改为：**仅 TRACKING/RECOVERING 推进内环；
     IDLE/LOST 完全静默（不采样、不发目标、不推进内环）**。理由：审计重要级 F1——LOST
     转移拍 Chassis_Stop 的机械刹车在同一调用内被内环 SetOutput(0) 覆盖（实测 brake_active
     被清），「恒推进」语义使超时停车退化为惰行。方案 b 让刹车真值表在 LOST/Stop 后保持
     （chassis.h 已文档化的驻车方法）。底盘另作他用时由使用者直接泵 Chassis_Update。
  2. E03 追加两条必含用例：LOST 后刹车真值表保持（IsBrakeActive 持续 true）；
     外环积分器跨拍存活（审计重要级 F2——积分限幅量纲失配修正的验证，
     修正 = Init 按误差口径显式给积分限幅，不再依赖 out_limit×3.5 推导）。
     E03 预期总数相应 ≥156（新用例 +1，原有 LOST 用例改写）。

## 9. S03 契约（tuning VOFA 调参链路服务）——冻结

- **task_id**: S03-tuning
- **goal**: 新建 `app/service/tuning/`：VOFA 调参链路服务。吸收旧 `vofa_register.c` 的
  「profile 注册中枢」职责（旧文件保持冻结，T01 删除；过渡期双实现登记拓扑），关闭 V15
  剩余支（Driver 直注册 + 暴露 task 状态），顺手关闭 V19（`u8` 别名经 Grep 证实全域零使用，
  仅删 typedef 与 TODO 注释）。第一个 profile：**底盘速度环脱线悬挂调参**（用户指定 demo）——
  为未来 debug Task 的调参状态机提供「进入即注册」的现成入口。
- **Q2 定案**：字节流解析归 Driver `vofa_run()`（V09 任务上下文边界不动）；解析结果的
  分发与应用归本服务唯一收口。Task 层永不直接触碰 uart_vofa。
- **变量组隔离三原则（用户裁定 2026-07-17，契约核心）**：
  1. **只做调参**：VOFA 变量组（cmd 输入 + tx 遥测）是本服务私有 static 上下文，
     不承载任何运行控制职责；实际运行变量（chassis 内部目标/PID/快照）不注册进 VOFA。
  2. **与运行变量分离、不相互赋予**：cmd 组永不从运行值回读初始化，运行值永不写入 cmd 组；
     tx 组只是 `Chassis_GetTelemetry()` 快照的单向复制（展示副本）；参数应用方向唯一：
     cmd → `Chassis_SetSpeedGains`/`Chassis_SetTargetMps`（Service 公共 API，不摸 Pid_T/内部状态）。
  3. **初值安全**：Enter/重进一律将 cmd 组重置为安全值（增益 0、目标 0）——不继承旧
     vofa_register「重进保参」回读语义；悬挂车辆上电进调参态时电机确定性不出力。
- **接口辩护**（调参链路能做什么）：能进入/退出一个调参 profile（进入即挂变量组）、
  能周期推进收发与应用、能报告当前激活 profile。仅此成为公共面。

### 9.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/tuning/tuning.h` / `.c` | 新建（会话核心：profile 生命周期 + 推进编排） |
| `hc-team/app/service/tuning/tuning_chassis.h` / `.c` | 新建（底盘速度环 profile 子模块：变量组 + 注册/重置/应用/刷新） |
| `hc-team/driver/uart_vofa/uart_vofa.h` | 修改（V19：仅删 `typedef uint8_t u8` 与 TODO(V19) 注释块，无其他改动） |
| `tests/host/test_tuning.c` | 新建 |
| `tests/host/Makefile` | 追加 test_tuning 目标/clean/.PHONY |
| `.gitignore` | 追加 test_tuning / test_tuning.exe |
| `Debug/makefile` | 登记 tuning.o、tuning_chassis.o |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |

forbidden_files：`hc-team/app/service/chassis/**`、`hc-team/app/service/line_follow/**`
（只调用不修改）、`hc-team/app/{tasks,scheduler,system,ui}/**`、`hc-team/driver/**` 其余全部
（含 `uart_vofa.c`、`board_uart/**`）、`hc-team/middleware/**`、tests/host 既有 `test_*.c`
与 `fake_*.c`（fake_uart_port 已有 Vofa 注入/抓取面，fake_clock 已有时间注入面）。

### 9.2 公共接口（最小面）

```c
typedef enum {
    TUNING_PROFILE_NONE = 0,
    TUNING_PROFILE_CHASSIS_SPEED,   /* 底盘速度环悬挂调参（demo） */
} Tuning_Profile;

void Tuning_Init(void);             /* →NONE；静默：不碰 VOFA/底盘 */
bool Tuning_EnterProfile(Tuning_Profile profile);
    /* CHASSIS_SPEED：vofa_clear_profile → cmd 组重置安全值 → 注册 tx×6/cmd×8
     * → Chassis_Stop（确定性起点）→ 立即应用安全 cmd（增益 0/目标 0 覆写底盘残留）。
     * NONE 或未知值：等效 Exit / 返回 false。 */
void Tuning_Update(void);
    /* NONE：完全静默（不发帧、不推底盘）。激活态每次调用：
     * ① 自门控 TUNING_STREAM_PERIOD_MS=10（Clock_NowMs 无符号减法）；到期执行
     *    vofa_run()（Driver 内解析 RX→cmd + 发送上一拍 tx 帧）→ cmd 无条件应用
     *    （Pid_SetGains 只写 cfg 不清史，已核实）→ 刷新 tx ← Chassis_GetTelemetry
     *    快照（遥测比现场晚一帧，接受）。
     * ② 无论到期与否，末尾恒推进 Chassis_Update()（内环自门控 10ms，S02 同款级联）。 */
void Tuning_ExitProfile(void);
    /* Chassis_Stop → vofa_clear_profile → NONE。此后 Update 静默，
     * 刹车真值表保持（S02 修订 1 同款语义）。 */
Tuning_Profile Tuning_GetActiveProfile(void);
```

- **变量组内容（CHASSIS_SPEED）**：tx×6 = 目标 L/R、反馈 L/R、PID 输出 L/R
  （全部来自 Chassis_Telemetry_T 快照副本）；cmd×8 = `LM`/`RM`（目标 m/s）、
  `LP`/`LI`/`LD`、`RP`/`RI`/`RD`（增益）——命令名沿用旧 profile，上位机工程免改。
- **单一所有者声明**：增益/目标写入只经 Chassis 公共 API（限幅、slew、换向、超时、刹车
  各归 S01 既有所有者，本服务零复做）；VOFA 协议/解析/缓冲归 uart_vofa Driver；
  串口归 vofa_uart Driver。本服务唯一拥有：调参变量组存储 + 应用节奏 + profile 生命周期。
- **前置条件**：System 装配层已完成 `vofa_init()`（含 VofaUart_Init）与 Clock/底盘链 Init
  （S01 同口径）；UART5 PA0/PA1 实物未引出是已登记硬件阻塞，不影响固件与主机验收。

### 9.3 preserved_behavior

- `app/service/{chassis,line_follow}/**`、旧 `app/**`、`driver/**`（除 uart_vofa.h 删 2 行
  死 typedef）、`middleware/**` 零行为改动；主机既有 159 用例全过；固件行为不变
  （新 .o 进链接但零调用者；`u8` 零使用故删除不改任何编译结果）。

### 9.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/|app/scheduler/|app/ui/|app/system/|ti_msp_dl_config|ti/driverlib`（path=`hc-team/app/service/tuning`，`#include` 行） | 0 命中 |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §9.1 | 无 allowed_files 之外的改动 |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥171 PASS / 0 FAIL（159 基线 + ≥12 新用例），必含安全项：Init/NONE 态完全静默（无帧无电机命令）、Enter 即安全（底盘残留增益/目标被安全 cmd 覆写 + 刹车起点）、安全初值帧全 0、RX 调参经 Chassis API 生效（LM 目标 + LP 增益悬挂主链路）、分离性（外部改运行值不回写 cmd 且下一拍被 cmd 覆写回）、tx=快照副本、10ms 门控单帧、级联推进内环、Exit 后刹车保持且无新帧、重进 cmd 回安全值 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、tuning.o 与 tuning_chassis.o 进入 .out 链接 |
| E05 | V19 关闭 | Grep `typedef\s+uint8_t\s+u8|\bu8\b`（path=`hc-team`） | 0 命中 |

### 9.5 契约修订记录

- **修订 1（2026-07-17，本提交，审计后处置）**：
  1. §9.2 Enter 序列在 `vofa_clear_profile` 之后、注册之前插入一步 **`vofa_run()` 排空积压 RX**
     （此刻绑定表与通道表已清空：解析落空、无帧发出，纯排空）。原因：审计建议级 F1——
     NONE 期间 ISR 持续入 FIFO，重进后首拍 `vofa_run` 会把会话间积压的历史命令
     （如上位机滑条残留 `LM=1`）当新命令应用，破坏三原则第 3 条「重进一律安全初值」
     的确定性（上电场景成立、重进场景不成立）。排空经既有 Driver 流程完成，不新增 API、
     不直接触碰 VofaUart。
  2. E03 追加两条必含用例（审计 F1/F2）：重进前积压的 RX 命令不生效（排空验证）；
     激活态传入无效 profile 等效 Exit（刹车 + 回 NONE + 此后静默）。
     E03 预期总数相应 ≥173（新用例 +2）。

## 10. S04 契约（hmi 人机输入/显示服务）——冻结

- **task_id**: S04-hmi
- **goal**: 新建 `app/service/hmi/`：人机输入/显示服务，包装 Key/OLED 两个 Driver，成为
  上层（未来 UI01 菜单、SCH01/T01）唯一的人机接口面：**语义输入事件**（上/下/确认/返回）
  + **行式文本显示**（4 行×16 列、16px 字模）+ 显示就绪查询 + 周期推进（按键扫描节奏
  + OLED 非阻塞初始化泵送）。K1..K4→语义动作映射从 `menu_core.c menu_key_from_id()` 下沉
  至本服务（**唯一映射点**）。本模块是 V14 的关闭基础；V14 本体在 UI01 关闭——旧
  `menu_core/menu_pages` 直调 Driver 与 UI 头暴露 `Key_Id_e` 是冻结债，UI01/T01 阶段删除，
  过渡期双实现登记拓扑（V07 同款过渡态：新 Service 零调用者，旧 `task_groups.c
  Task_UiService5ms` 泵送路径继续冻结，不强行接线）。
- **接口辩护**（器件能做什么）：人机面板能报告用户输入动作（四个语义键的单次按下事件）、
  能按行显示 ASCII 文本、能清屏、能报告显示就绪、能被周期推进。仅此成为公共面。
  （`Key_IsPressed` 电平态全 App 零消费者——不包装、不进公共面。）

### 10.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/hmi/hmi.h` / `.c` | 新建 |
| `tests/host/test_hmi.c` | 新建 |
| `tests/host/Makefile` | 追加 test_hmi 目标/clean/.PHONY |
| `.gitignore` | 追加 test_hmi / test_hmi.exe |
| `Debug/makefile` | 登记 hmi.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |

forbidden_files：`hc-team/app/service/{chassis,line_follow,tuning}/**`、
`hc-team/app/{tasks,scheduler,system,ui}/**`、`hc-team/driver/**` 全部、`hc-team/middleware/**`、
tests/host 既有 `test_*.c` 与 `fake_*.c`（fake_board_gpio 已有按键电平/边沿注入面，
fake_i2c_port 已有 I2C 抓取面 + 自带可设定 `Clock_NowMs`）。

### 10.2 公共接口（最小面）

```c
typedef enum {
    HMI_INPUT_NONE = 0,
    HMI_INPUT_UP,      /* 板载 K1 */
    HMI_INPUT_DOWN,    /* 板载 K2 */
    HMI_INPUT_ENTER,   /* 板载 K3 */
    HMI_INPUT_BACK,    /* 板载 K4 */
} Hmi_Input;

#define HMI_DISPLAY_ROWS 4u    /* 64px / 16px 字模 */
#define HMI_DISPLAY_COLS 16u   /* 128px / 8px 字宽 */

void Hmi_Init(void);            /* 门控基准/私有状态复位；不触碰 Key/OLED 硬件 */
void Hmi_Update(void);          /* 自门控 HMI_UPDATE_PERIOD_MS=5（Clock_NowMs 无符号减法，
                                   沿用旧 5ms UI 任务节奏）；到期执行：
                                   OLED 未就绪 → OLED_Process()；Key_Scan() */
Hmi_Input Hmi_PollInput(void);  /* 取出一个待处理语义输入事件；无 → HMI_INPUT_NONE
                                   （内部映射 Key_PollPressEvent，事件读清语义透传） */
bool Hmi_IsDisplayReady(void);
bool Hmi_PrintLine(uint8_t row, const char *text);
    /* row 0..3（页地址 = row×2，16px 字模）；ASCII；超长截断至 16 列；
       不足 16 列行尾空格填满（整行所有权，行级覆写无残影——旧 menu 靠整屏 Clear 防残影，
       本服务改为行级保证）；未就绪/row 越界/NULL → false 且零绘制事务。 */
bool Hmi_ClearDisplay(void);    /* 未就绪 → false */
```

- **单一所有者声明**：去抖/单次事件锁存归 `key.c`（KEY_*_DEBOUNCE_TICKS×5ms≈20ms 等效
  不变，扫描周期沿用旧值）；页寻址/字模/总线恢复/等待上限归 `oled_hardware_i2c.c`；
  边沿位图归 BoardGpio/GROUP1 ISR。本服务唯一拥有：**语义映射 + 泵送节奏 + 行式显示语义**，
  零复做下层保护。
- 头文件不暴露 `Key_Id_e`/`Oled_Status_e`（§3.4，与 chassis/line_follow/tuning 同构）。
- **前置条件**：System 装配层已完成 `Key_Init()`、`OLED_Init()`（含底层 I2C/Clock 初始化）；
  GROUP1 ISR 照常置按键边沿位图。本服务无电机/功率路径，无 §8.1 安全项。

### 10.3 preserved_behavior

- `driver/**`、`middleware/**`、其余 `app/**` 零改动；主机既有 173 用例全过；
  固件行为不变（hmi.o 进链接但零调用者）。

### 10.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/|app/scheduler/|app/ui/|app/system/|ti_msp_dl_config|ti/driverlib`（path=`hc-team/app/service/hmi`，`#include` 行） | 0 命中 |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §10.1 | 无 allowed_files 之外的改动 |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥185 PASS / 0 FAIL（173 基线 + ≥12 新用例），必含：Init 静默（零 I2C 事务、零按键电平读取）；未到期 Update 无扫描效果 + 5ms 到期才推进；泵送至显示就绪翻转 IsDisplayReady；就绪前 PrintLine/Clear 返回 false 且零绘制事务；K1..K4→UP/DOWN/ENTER/BACK 全映射（含 ≥4 拍去抖真实路径）；按住不重复出事件；空队列 Poll→NONE；PrintLine 越界/NULL→false；超长截断；行尾空格填满（整行 16 列全写）；ClearDisplay 事务发生 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、hmi.o 进入 .out 链接 |

- **主机测试链接组成**（事实登记）：test_hmi = 真实 `hmi.c` + 真实 `key.c` + 真实
  `oled_hardware_i2c.c` + `fake_board_gpio.c` + `fake_i2c_port.c`。**不链接 `fake_clock.c`**：
  `fake_i2c_port.c` 自带 `Clock_NowMs` 定义（`FakeI2cPort_SetNowMs` 注入），同链会重定义符号；
  本测试时间注入统一走 `FakeI2cPort_SetNowMs`。

### 10.5 契约修订记录

- **审计处置（2026-07-17，无契约行修订）**：审计唯一 finding（建议级）——hmi.h `Hmi_PrintLine`
  注释「false = 零绘制事务」未覆盖运行期总线错误路径（`OLED_ShowString` 逐字符事务、
  中途 I2C 超时会半行绘制后返回 false）。处置为文档级：把「零绘制事务」承诺限定于
  参数/就绪拒绝路径，补充「总线错误时行内容不确定，行级覆写幂等、重试整行恢复」。
  代码逻辑不改（主机 fake 总线不失败故 E03 无法覆盖该路径；真实恢复策略归 UI01 调用者）。

## 11. SCH01 契约（scheduler 运行条目调度器重写）——冻结

- **task_id**: SCH01-scheduler
- **goal**: 新建 `app/scheduler/scheduler.{h,c}`：运行条目（run entry）调度器——条目表登记 +
  进入/离开/重启/查询 + **单活动条目不变量** + 每拍泵送（背景钩子 + 活动条目 step）。
  时间来源按 **Q1 定案**参数注入：`Scheduler_Run(uint32_t now_ms)`，本模块不含 `clock.h`，
  `now_ms` 原值透传给全部钩子——Task/UI 层因矩阵禁调 Driver，**此参数是它们唯一合法时间来源**。
  吸收旧 `task_scheduler.c`/`run_registry.c` 的「Enter/Leave/GetActive + 条目枚举（菜单渲染）」
  职责；**时间片框架（TimCount/TimRload/任务组三态 switch）不重建**——新 Service 全部
  Clock 自门控，条目 step 每拍无条件调用即可。旧四文件继续冻结，T01 删除时关闭 V13 残余；
  本模块以「头文件零 extern 变量 + 状态全私有」建立 V13 替代前提。
  **单活动条目不变量**同时是本轮拓扑核对新发现「双泵风险」（line_follow 与 tuning 各自
  恒推 `Chassis_Update()`，源码无所有权互斥）的结构性排除手段——条目间互斥由调度器保证，
  收工时由 topo-updater 登记该新风险条目及缓解措施。
- **接口辩护**（调度器能做什么）：能登记一组命名运行条目、能进入/离开/重启条目并查询
  当前条目与名称（未来 UI01 菜单渲染所需，替代 `RunRegistry_BuildMenuItems`）、
  能被喂时驱动一拍。仅此成为公共面。
- **背景钩子辩护**（非投机）：旧系统 UI 任务组在 IDLE_PAGE 与 RUNNING 两态均运行
  （菜单在条目运行中仍须响应 BACK 键触发 Leave，§5.3 数据流既有事实）——背景钩子是
  该已证实需求的最小承载，UI01 是已知消费者。

### 11.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/scheduler/scheduler.h` / `.c` | 新建 |
| `tests/host/test_scheduler.c` | 新建 |
| `tests/host/Makefile` | 追加 test_scheduler 目标/clean/.PHONY |
| `.gitignore` | 追加 test_scheduler / test_scheduler.exe |
| `Debug/makefile` | 登记 scheduler.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |

forbidden_files：`hc-team/app/scheduler/task_scheduler.*`、`run_registry.*`、`vofa_register.*`
（同目录冻结旧文件，零触碰）、`hc-team/app/service/**`、`hc-team/app/{tasks,system,ui}/**`、
`hc-team/driver/**`、`hc-team/middleware/**`、tests/host 既有 `test_*.c` 与 `fake_*.c`
（本测试为纯逻辑，不链接任何 fake——含 fake_clock）。

### 11.2 公共接口（最小面）

```c
#define SCHEDULER_ENTRY_NONE (-1)

typedef struct {
    const char *name;                  /* ASCII 条目名，菜单渲染用；不得为 NULL */
    void (*on_enter)(void);            /* NULL 允许（跳过） */
    void (*on_step)(uint32_t now_ms);  /* NULL 允许；活动期每拍无条件调用 */
    void (*on_exit)(void);             /* NULL 允许 */
} Scheduler_Entry_T;

void Scheduler_Init(const Scheduler_Entry_T *entries, uint8_t entry_count,
                    void (*background_step)(uint32_t now_ms));
    /* 登记条目表（调用方保证表生命周期覆盖使用期）+ 可选背景钩子；活动条目复位为无。
     * entries==NULL 或 count==0 → 合法空表（Enter 恒 false）。不触碰任何硬件/Service。 */
uint8_t Scheduler_GetEntryCount(void);
const char *Scheduler_GetEntryName(uint8_t index);   /* 越界 → NULL */
bool Scheduler_EnterEntry(uint8_t index);
    /* 越界/空表 → false 零副作用。有效：先 on_exit(旧活动条目，若有)，再置活动，
     * 再 on_enter(新)。同索引重进 = 重启（同样 exit→enter 序）。 */
void Scheduler_LeaveEntry(void);       /* 有活动：on_exit + 清活动；无活动：no-op */
int16_t Scheduler_GetActiveEntry(void); /* 活动索引或 SCHEDULER_ENTRY_NONE */
void Scheduler_Run(uint32_t now_ms);
    /* ① background_step(now_ms)（非 NULL 时，无条件先行）；
     * ② 随后解析活动条目并调其 on_step(now_ms)——背景钩子内 EnterEntry 的
     *    首拍 step 同拍生效（确定性语义，非竞态）。
     * 钩子内允许调 Enter/Leave（菜单切换、条目自终止），立即生效：
     * on_step 内 LeaveEntry → 本条目 on_exit 即刻执行，本拍不再有条目 step。 */
```

- **状态全私有**：`scheduler.h` 零 `extern` 变量（V13 替代前提）；表指针/计数/活动索引
  均 `static`，仅经 getter 暴露。无三态系统状态枚举——「无活动条目」即旧 IDLE 语义。
- **零依赖**：`scheduler.c` 只含标准头（stdint/stdbool/stddef）与自身头。矩阵允许
  Scheduler→Service，但本设计不需要——条目钩子由 Task/UI 层（同层受控）在 T01/UI01 提供，
  Service 的 Init/泵送编排是钩子内容，不是调度器机制。
- **单一所有者**：run-entry 转移序（exit→enter）唯一实现点在 `Scheduler_EnterEntry`；
  钩子提供者不得自行补第二份转移逻辑。

### 11.3 preserved_behavior

- 同目录旧四文件、其余 `app/**`、`driver/**`、`middleware/**` 零改动；主机既有 185 用例
  全过；固件行为不变（scheduler.o 进链接但零调用者，Scheduler_* 符号与旧 Sys_* 无冲突）。

### 11.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/\|app/ui/\|app/system/\|app/service/\|driver/\|middleware/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/scheduler`，glob `scheduler.*`，`#include` 行） | 0 命中 |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §11.1 | 无 allowed_files 之外的改动 |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥197 PASS / 0 FAIL（185 基线 + ≥12 新用例），必含：Init 前/空表 Run 与 Enter 安全（零钩子调用、Enter false）；未激活 Run 仅背景钩子且 now_ms 原值透传；Enter 转移序 exit→enter 恰一次；同索引重启；越界 Enter false 零副作用；Leave 后仅背景、重复 Leave no-op；on_step 内自终止（on_exit 即刻、本拍无后续 step、下拍无条目 step）；背景钩子内 Enter 同拍首步；NULL 钩子容忍；无背景钩子 Init 时 Run 仅条目 step；背景先于条目的顺序记录；GetEntryName 越界 NULL |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、scheduler.o 经 linkInfo.xml 确证进入 .out 链接 |

### 11.5 契约修订记录

- **修订 1（2026-07-17，本提交，审计后处置——采纳审计建议 ②）**：§11.2 `Scheduler_EnterEntry`
  语义补充嵌套转移规则：**旧条目 on_exit 内嵌套调用 EnterEntry 时，嵌套转移胜出，
  外层进入放弃并返回 false**（守卫：LeaveEntry 返回后若活动条目已非空即放弃）。
  原因：审计建议级 finding——原实现中该路径产生「孤儿 on_enter」（嵌套进入的条目
  收到 on_enter 后被外层无条件覆盖活动索引，永远等不到配对 on_exit）；enter/exit
  配对是未来 Task 挂安全停止逻辑的锚点，配对破坏 = 停止路径被静默绕过，失败模型
  真实（Task 在 on_exit 里链式进入下一条目）且可测。E03 追加 1 条必含用例
  （on_exit 内嵌套 Enter：外层 false、嵌套条目保持活动、无孤儿 on_enter），
  预期总数相应 ≥198（185 基线 + ≥13）。
