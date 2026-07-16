# P3：Motor Driver 安全重写与耦合清除计划

计划所有者：Codex（本文件即验收契约，REASONIX 不得修改验收条目）  
制定日期：2026-07-16  
状态：done（2026-07-16 T1/T2/T3 全部 CODEX_ACCEPTED，V04/V06/V11/V12 关闭）  
修订：2026-07-16 响应 REASONIX `SELF_CHECK_BLOCKED`——前置条件由“P1/P2 已验收”改为下列可验证条件；`encoder.c`、`pid.c/.h` 的改动所有权移交 `plan2_fix_encoder_closeout.md`（P2F.T1），本计划对应扫描降级为回归确认。  
修订 2：2026-07-16 消除与根 `AGENTS.md` §8.1 的冲突——删除“不引入斜坡”禁令，改为 Motor 私有对称 slew limit（§3、§4、E01 已同步；T3 硬件测量前 slew 数值为 P0 保守占位）。  
前置条件：`plan2_fix_encoder_closeout.md` P2F.T1（E01–E06）已获 Codex 验收；Motor P0 安全闸门有效（见总计划 §3），全程禁止带负载运行电机。（2026-07-16 起硬件验收取消：软件行全过即 `CODEX_ACCEPTED` 终局。）  
流程（2026-07-16 简化）：Codex 计划 → REASONIX 施工并提交行级完成报告 → Codex 精简验收（自检阶段已撤销）。

REASONIX 施工时必须携带以下指令：

```text
Execute only this task. Reports are claims, not proof. Do not mark complete until every command and observable postcondition has been reproduced. Never ignore command errors. Do not close topology items without direct evidence.
```

施工报告状态只允许：`CONSTRUCTION_DONE`、`CONSTRUCTION_BLOCKED`。  
Codex 验收结论只允许：`CODEX_ACCEPTED`、`CODEX_REJECTED`（2026-07-16 起硬件验收取消，SOFTWARE 分级作废）。  
证据行 ID（E01…E14）由本计划固定分配，施工与验收双方必须引用同一 ID，不得改号或合并。

## 0. 基线（Codex 于 2026-07-16 记录）

- 工作区存在大量未提交修改（`git status` 全量 M/D 列表已知）；施工不得回滚、格式化或覆盖与本计划无关的文件。
- 主机测试框架位于 `tests/host/`（`Makefile`、`fake_board_gpio.c`、`stub_motor.c`、`test_encoder.c`），编码器 13 项测试为绿色基线。
- 固件构建入口：`rtk make -C Debug all`（CCS 生成的 `Debug/makefile`）。
- 唯一硬件配置源：`project/mspm0/board.syscfg`；禁止改 Debug 生成物代替它。

## 1. 三个设计问题

1. **抽象是什么？** 按 `Motor_Id` 提供直流电机执行能力：初始化即安全态、设置带符号归一化输出、周期推进（换向状态机与命令超时）、刹车。不含速度闭环、编码器数据和 PID。
2. **必须隐藏什么？** 方向引脚映射、PWM compare 口径与定时器实例、换向状态机状态、上次命令时间戳、TB6612 真值表。公共头只允许标准 C 类型和 Motor 自有枚举。
3. **代码属于哪里？** 方向 + PWM + 硬件安全态属于 Motor Driver；速度环、目标生成属于 Service/Middleware；DL HAL 写操作只允许出现在 Motor 的硬件写文件中。

## 2. 当前问题证据（Codex 已逐条核实）

- `hc-team/driver/motor/motor.h:36` 包含 `middleware/pid/pid.h`；`motor.h:81` `PID_T* p_pid`；对应拓扑 V04（Driver↔Middleware 循环）。
- `motor.h:86` 公开可写全局 `extern Motor_T g_tMotors[MOTOR_COUNT]`，`motor.h:73-78` 混入 `encoder_sign/encoder_delta/encoder_total/speed` 字段；对应 V05（残余兼容写入）与 V06（`encoder_sign` 已确认未使用）。
- `hc-team/middleware/pid/pid.c:219-222` 直接读写 `g_tMotors[i].p_pid` 和 `.speed`（V04 另一半）。
- `hc-team/driver/encoder/encoder.c:169-176`、`encoder.c:228-236` 仍有“旧 API 兼容：同步写入 g_tMotors”分支（plan2 §4 已判定随 P3 一并清除）。
- 消费者：`app/tasks/task_groups.c:236`、`app/tasks/task1/task1.c:131,237,250,353-354,386-387`、`app/tasks/speed_loop/speed_loop.c:63-64,99-100,112,134`、`app/system/sys_init.c:72-73` 直接使用 `g_tMotors`/`p_pid->out`/`.speed`。
- `motor.c:150-169` `Motor_SetPwm()` 先 `motor_apply_direction()` 再写 duty，无过零、无死区、无斜率、无命令超时（V12）。
- `motor.c:176-184` `Motor_Brake()` 双引脚置高 + duty 0，TB6612 真值表和 PWM 有效极性无硬件记录（V12/P0）。
- 左 PWM 3.2 MHz/period 63999（约 50 Hz），右 PWM 80 MHz/period 1000（约 80 kHz），而 `motor_write_duty()` 把同一 `0..1000` 写入两侧 compare，左轮实际占空比上限约 1.6%（V11，总计划 §3）。
- `motor.c:30-33` 直接包含 `ti_msp_dl_config.h`/`dl_gpio.h`/`dl_timera.h`（Driver 允许，但阻断主机测试，需要逻辑/硬件写分离）。

## 3. 唯一数据处理链（P3 之后）

```text
Service 计算带符号输出（-1000..1000，算法限幅由 PID 层负责）
 -> Motor_SetOutput(id, output)：硬件限幅到 MOTOR_OUTPUT_MAX，刷新命令时间戳
 -> Motor_Update(elapsed_ms)：换向状态机（先归零 -> 死区驻留 -> 反向）+ 命令超时归零
 -> motor_hw 写方向引脚与按各自 period 换算的 compare
```

- 硬件限幅只在 Motor 做一次；PID 输出限幅是算法职责，两者不共享常量（plan2 §5 P2.5 结论）。
- `0..1000` 到 compare 的换算按每通道真实 period 完成，消除 V11 的软件口径错误；PWM 频率统一是硬件决策，留在 T3。
- 输出变化必须经 Motor 私有的对称 slew limit（根 `AGENTS.md` §8.1 强制；本条替代早先“不引入斜坡”的错误禁令），由 `Motor_Update(elapsed_ms)` 按真实 elapsed 推进——同向 0→1000 与换向同样受限，禁止单拍跃变。slew 速率与换向死区时长为私有常量，T3 硬件测量前取 P0 保守占位值并在注释注明来源；不引入电流估计或多级限幅。

## 4. 最小目标接口

```c
typedef enum {
    MOTOR_LEFT = 0,
    MOTOR_RIGHT,
    MOTOR_COUNT
} Motor_Id;

#define MOTOR_OUTPUT_MAX 1000

void Motor_Init(void);                       /* 上电即安全态：方向清零、compare 清零、计数器启动 */
bool Motor_SetOutput(Motor_Id id, int16_t output); /* 越界拒绝返回 false；合法则登记目标并刷新命令时间 */
void Motor_Update(uint32_t elapsed_ms);      /* 周期调用：slew 限速推进、换向过零/死区、命令超时 -> 输出归零 */
void Motor_Brake(Motor_Id id);
void Motor_BrakeAll(void);
```

内部拆分（不改变公共 API）：

- `motor.c`：状态机、限幅、超时，全部私有 `static` 状态，不包含任何 TI 头。
- `motor_hw.c` + 私有 `motor_hw.h`：唯一包含 `ti_msp_dl_config.h`/`ti/driverlib` 的文件，提供 `motor_hw_write_dir()`、`motor_hw_write_duty_permille()`（内部按本通道 period 换算）、`motor_hw_brake_pins()`、`motor_hw_start_pwm()`。`motor_hw.h` 不进入公共包含路径说明。

删除项：`Motor_T`、`g_tMotors`、`g_tMotorL/R` 宏、`p_pid`、`encoder_sign/encoder_delta/encoder_total/speed` 字段、`Motor_GetSpeed()`、`Motor_SetPwm(Motor_T*, float)`、`Motor_StartPwm()` 公共暴露（并入 `Motor_Init()`；`mspm0_runtime.c` 的 deprecated 包装同步删除或改内部调用）。

## 5. 施工任务

### P3.T1 主机可测的 Motor 状态机（新文件，不动现有构建）

Status: done（2026-07-16 CODEX_ACCEPTED：E01 7 项 + E02 回归 26 项全绿；Codex 复跑扫描与测试通过）
Goal: 在不修改现有 `motor.c/.h` 与消费者的前提下，新实现（新文件）在主机测试下证明限幅、换向过零死区、命令超时和安全初始化行为。

Evidence:
- `motor.c:150-169` 现实现无过零/死区/超时。
- `tests/host/Makefile` 现有 encoder 测试模式可复制。

Architecture:
- Abstraction: 带安全状态机的方向+PWM 执行能力。
- Hidden state: 每电机目标输出、施加输出、换向阶段、死区剩余时间、命令时间戳。
- Owner layer: Driver。
- Allowed dependency direction: `motor.c -> motor_hw.h`（同模块内部）；测试 -> fake motor_hw。

Scope:
- allowed_files: `hc-team/driver/motor/motor_new.c`（临时名，T2 收口时更名）、`hc-team/driver/motor/motor_new.h`、`hc-team/driver/motor/motor_hw.h`、`tests/host/test_motor.c`、`tests/host/fake_motor_hw.c`、`tests/host/Makefile`
- forbidden_files: `hc-team/driver/motor/motor.c`、`hc-team/driver/motor/motor.h`、`hc-team/middleware/pid/*`、全部 `app/**`、`project/mspm0/board.syscfg`
- preserved_behavior: 现有固件构建与全部现有测试保持绿色；本任务不进入固件构建。

Preconditions:
- 基线 `make -C tests/host all` 通过（encoder 13 项）。

Steps:
1. 先写 `test_motor.c` 并运行确认失败（RED：目标符号尚不存在或断言失败）。
2. 实现 `motor_new.c` 最小状态机使测试通过（GREEN）。
3. 只重构本任务引入的代码。
4. 在执行日志记录死区常量与超时常量的依据（机构/驱动芯片手册或待 T3 测量的占位说明）。

Verification:
- E01 command: `make -C tests/host run_motor`
- E01 expected_exit: 0
- E01 postcondition: 输出逐项列出并通过至少以下用例——越界拒绝且状态不变；同向 `0 -> 1000` 不允许单拍直达，施加输出按 slew 速率 × 真实 elapsed 逐拍逼近（elapsed 翻倍则单拍步进翻倍）；`+800 -> -800` 不允许单周期直达，必须先按 slew 降到零、驻留 ≥ 死区时长再反向爬升；死区期间新同向命令不缩短死区；超时无新命令后 fake 记录的 duty 归零且方向引脚清零；`Motor_Init()` 后 fake 记录 duty=0、方向清零；brake 调用 fake 的 brake 序列且 duty=0（brake 为安全动作，不受 slew 延迟）。
- E01 negative_check: `rg 'ti_msp_dl_config|ti/driverlib' hc-team/driver/motor/motor_new.c tests/host/test_motor.c tests/host/fake_motor_hw.c` 零命中。
- E02 command: `make -C tests/host all`
- E02 expected_exit: 0
- E02 postcondition: encoder 既有 13 项测试全部仍通过（回归保护）。
- E02 negative_check: `git status --short tests/host` 之外不出现新增/修改文件。

Hardware gate:
- 本任务不执行硬件操作；Motor P0 保持有效。

Stop conditions:
- 需要修改现有 `motor.c/.h` 才能编译测试；死区/超时常量找不到可陈述依据；fake 无法表达真实硬件写顺序。

### P3.T2 切换公共 API 并删除 Motor_T/g_tMotors

Status: done（2026-07-16 CODEX_ACCEPTED：E03–E08 通过；E07 构建阻塞源于用户 syscfg 改名，Codex 基线修复 5131f6e 后完成）
Goal: 固件全部改用新 Motor API；`Motor_T`、`g_tMotors`、`motor.h` 对 `pid.h` 的包含全部消失，clean build 通过。（PID 按值化与 encoder 兼容写入删除已由 P2F.T1 完成，本任务 E04/E05 仅作回归确认。）

Evidence:
- `motor.h:36,81,86` 及 §2 列出的全部消费者行号（P2F.T1 后消费者已改读快照与 PID 按值输出，仅剩 `Motor_SetPwm(&g_tMotors[i], out)` 形式的执行调用待切换）。

Architecture:
- Abstraction: 同 T1。
- Hidden state: 同 T1；PID 改为按值输入 target/feedback、返回输出，不再感知 Motor。
- Owner layer: Motor=Driver；pid_closeloop_motor=Middleware；采样/输出编排留在现有调用点（V07 存量不在本任务扩大）。
- Allowed dependency direction: `Task/Service -> Motor_API`、`Task/Service -> PID_API`；禁止 `PID -> Motor`、`Encoder -> Motor`。

Scope:
- allowed_files: `hc-team/driver/motor/motor.c`、`hc-team/driver/motor/motor.h`、`hc-team/driver/motor/motor_hw.c`、`hc-team/driver/motor/motor_hw.h`、`hc-team/driver/motor/motor_new.c`（并入后删除）、`hc-team/driver/motor/motor_new.h`（并入后删除）、`hc-team/app/system/sys_init.c`、`hc-team/app/tasks/task_groups.c`、`hc-team/app/tasks/task1/task1.c`、`hc-team/app/tasks/speed_loop/speed_loop.c`、`hc-team/driver/mspm0_runtime/mspm0_runtime.c`、`hc-team/driver/mspm0_runtime/mspm0_runtime.h`、`tests/host/test_motor.c`、`tests/host/Makefile`、构建元数据中登记新增源文件所需的最小修改
- forbidden_files: `project/mspm0/board.syscfg`、`hc-team/driver/encoder/*`、`hc-team/middleware/pid/*`、`hc-team/app/tasks/platform_2d/*`、`hc-team/driver/step_motor/*`、`hc-team/app/ui/*`
- preserved_behavior: PID 参数值、任务周期、遥测字段语义不变；P2F.T1 建立的数据链（快照反馈、PID 按值）不动，仅把输出施加调用从 `Motor_SetPwm(&g_tMotors[i], out)` 换为 `Motor_SetOutput(id, out)` 并接入 `Motor_Update()` 周期。

Preconditions:
- P3.T1 状态为通过（E01/E02）；P2F.T1（E01–E06）已获 Codex 验收。

Steps:
1. 先运行 E04/E05/E06 的依赖扫描，记录基线（E04/E05 预期已为零，作回归锚点；E06 在旧代码上非零命中，作 RED 基线）。
2. `motor_new` 并入 `motor.c/.h` + `motor_hw.c`；逐个迁移 §2 消费者的执行调用；删除 `Motor_T`/`g_tMotors` 与 `mspm0_runtime` 中的 `StartMotorPwm` 关联残留。
3. 只重构本任务引入的代码；不顺手改任务业务逻辑。
4. 验证通过后更新拓扑（见 §7）。

Verification:
- E03 command: `make -C tests/host all`
- E03 expected_exit: 0
- E03 postcondition: encoder + motor 全部主机测试通过；`tests/host` 不再引用 `g_tMotors`。
- E03 negative_check: 测试中不得为让编译通过而保留旧 `Motor_T` 定义副本。
- E04 command: `rg 'pid\.h|PID_T|p_pid|g_tMotor|Motor_T|Motor_GetSpeed|encoder_sign' hc-team/driver/motor hc-team/driver/encoder`
- E04 expected_exit: 1
- E04 postcondition: 零命中（rg 无匹配时退出码为 1）。
- E04 negative_check: 不得通过重命名同义符号（如 `s_motors_public`）绕过扫描；Codex 将读 diff 复核。
- E05 command: `rg 'driver/motor|g_tMotor|Motor_' hc-team/middleware/pid`
- E05 expected_exit: 1
- E05 postcondition: 零命中；`pid_closeloop_motor` 签名为按值输入输出。
- E05 negative_check: `pid.h` 不新增任何 Driver 类型前置声明。
- E06 command: `rg 'g_tMotors|g_tMotorL|g_tMotorR|Motor_SetPwm|Motor_GetSpeed' hc-team`
- E06 expected_exit: 1
- E06 postcondition: 全仓库零命中。
- E06 negative_check: 不得把旧调用注释保留为死代码。
- E07 command: `rtk make -C Debug clean` 后 `rtk make -C Debug all`
- E07 expected_exit: 0
- E07 postcondition: 生成新的 `Debug/*.out` 时间戳晚于本次构建开始；链接 map 含 `Motor_Update`、`motor_hw_write_duty_permille`；无新增 warning（与基线 warning 列表对比）。
- E07 negative_check: 不得使用旧的目标文件缓存充当构建证据。
- E08 command: `git diff --stat`
- E08 expected_exit: 0
- E08 postcondition: 改动只落在 allowed_files；无秘密、无生成物、无无关格式化。
- E08 negative_check: forbidden_files 零改动。

Hardware gate:
- 明确不执行。V11/V12 保持 open；`Motor_Update` 的换向/超时行为仅主机证据，硬件行为归 T3。

Stop conditions:
- 消费者迁移需要改 platform_2d 或 UI 才能编译；PID 按值化需要改任务周期语义；构建元数据无法以最小方式登记新文件；工作区无关文件被波及。

### P3.T3 PWM 频率统一（syscfg 单源，软件任务）

修订 3：2026-07-16 用户裁定取消硬件验收——原示波器/真值表/时序实测行（旧 E09–E12）作废，硬件调试由用户自理。本任务改为纯软件任务：统一左右 PWM 定时器配置并以生成配置值为证据。

Status: done（2026-07-16 CODEX_ACCEPTED：E09 复核通过——双通道 80 MHz/period 7999 = 10 kHz 统一，构建 0 警告）
Goal: `board.syscfg` 左右电机 PWM 使用同一目标频率与同一 period 口径；`motor_hw.c` 换算常量与生成配置一致；clean build 通过。

Evidence:
- `project/mspm0/board.syscfg` 左 PWM 3.2 MHz/period 63999（约 50 Hz）、右 PWM 80 MHz/period 1000（约 80 kHz），同一 `0..1000` 写入两侧 compare（V11）。

Scope:
- allowed_files: `project/mspm0/board.syscfg`、`hc-team/driver/motor/motor_hw.c`（period 常量随 syscfg 同步）
- forbidden_files: `hc-team/app/**`、`hc-team/middleware/**`
- preserved_behavior: 公共 API 不变；统一频率取 TB6612 数据手册允许范围内的常用值（如 10 kHz），选值依据写入 syscfg 注释或提交说明。

Preconditions:
- P3.T2 全部软件行通过。

Steps:
1. syscfg 中把左右 PWM 改为同一时钟分频与 period（单源修改，禁止改 Debug 生成物）。
2. 重新生成并构建；同步 `motor_hw.c` 换算常量。
3. 验证通过后更新拓扑（V11 软件侧关闭）。

Verification:
- E09 command: `rtk make -C Debug all` 后 `rg -n 'PWM_MOTOR' Debug/ti_msp_dl_config.h`
- E09 expected_exit: 0
- E09 postcondition: 生成配置中左右 PWM 定时器 period/分频一致；`Motor_SetOutput(id, 500)` 两通道换算 compare 占各自 period 的比例相同（主机测试或生成值推算证明）。
- E09 negative_check: 不得只改一侧或在 `motor_hw.c` 用第二套系数掩盖 syscfg 不一致。

Stop conditions:
- TB6612 允许频率范围存疑（报告用户选值）；syscfg 单源无法表达两通道同频。

## 6. 全任务禁止事项

- 禁止保留 `g_tMotors` 或任何“过渡期兼容”双轨输出路径。
- 禁止在 Motor 内加入速度环、编码器读取或遥测。
- 禁止多层重复限幅：PID 限幅（算法）与 Motor 限幅（硬件）各一次，常量独立。
- `board.syscfg` 只允许在 T3 修改。

## 7. 拓扑更新契约（验证通过后才允许改）

- 类图：`Motor_API` 更新为新 API 集合；删除 `Motor_API --> PID_API` VIOLATION 边（`PID_API --> Motor_API` 与 `Encoder_API --> Motor_API` 两条边由 P2F.T1 先行删除）；删除 `+g_tMotors`。
- 5.1 数据流图：删除 `MotorGlobal` 违规节点，改为 `Service -> Motor_SetOutput/Motor_Update`。
- 登记表：V04 由 `partially closed`（P2F.T1）改 `closed + 日期 + E04/E06 证据`；V06 改 `closed + 日期 + E04 证据`（`encoder_sign` 字段随 `Motor_T` 删除）；V11 软件侧由 T2+T3 关闭；V12 由 T1/T2 状态机主机测试关闭（硬件实测由用户自理）。
- 源文件覆盖清单：Motor 行增加 `motor_hw.c/.h`；日志新增一行。
