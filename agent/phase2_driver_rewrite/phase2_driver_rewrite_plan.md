# Phase 2：Driver 分层重写总计划

日期：2026-07-13  
状态：planning  
范围：从 Driver 层开始，按依赖顺序逐个模块修复；本阶段文档不修改业务代码。

## 1. 阶段目标

按照根目录 `AGENTS.md` 将旧 Driver 重写为职责单一、资源所有权明确、上层主动拉取数据的模块。每次只迁移一个 Driver 及其不可避免的调用点，不做全仓库同时重构。

本阶段必须做到：

- Driver 是唯一接触 DL HAL、SysConfig 生成符号和寄存器的项目层。
- Driver 不包含 App 或 Middleware，不调用上层函数，不接收上层注册回调。
- ISR 只采集、入队、计数或置位；解析和业务处理留在 Service/Task 上下文。
- 公共头文件不暴露寄存器、PID、可写全局实例和私有状态。
- 数据方向、单位、采样周期、校准、滤波和限幅必须只有一个所有者。
- 每个迁移步骤都从可构建状态开始，也必须回到可构建状态。

## 2. 当前基线

- `rtk make -C Debug all` 于 2026-07-13 通过。
- 当前工作区已有大量未提交修改；执行计划时不得覆盖、回滚或格式化无关文件。
- `mspm0_runtime` 当前仍是未跟踪目录，`agent/` 也是未跟踪目录；执行前必须再次确认工作区状态。
- Phase 1 已移除自制 `sdk/hal`，Phase 2 不得重新创建通用 HAL 包装层。
- 目前没有完成 Driver 重写所需的主机测试框架和硬件冒烟证据，不能把“能编译”当作完成。

## 3. P0 电机安全闸门

在 Motor 正式重写和示波器验收前，禁止带负载运行直流电机：

- 左 PWM 当前生成配置为 3.2 MHz 时钟、period `63999`，约 50 Hz。
- 右 PWM 当前生成配置为 80 MHz 时钟、period `1000`，约 80 kHz。
- `Motor_SetPwm()` 却把同一个 `0..1000` 数值直接写入左右 compare，因此左右占空比口径并不一致。
- 当前换向顺序是先翻转方向 GPIO、后更新 PWM compare，没有先归零、换向死区、斜率限制或命令超时。
- 当前 `Motor_Brake()` 的 TB6612 真值表、PWM 有效极性和实际制动效果尚无示波器/硬件记录。

在确认 TB6612 接线、左右方向、PWM 有效极性、统一 PWM 频率、compare 口径、刹车/滑行真值表之前，任何阶段只能做断电静态检查或 PWM 引脚无负载测量。

## 4. 模块顺序

| ID | 状态 | 模块 | 选择原因 | 详细计划 |
|---|---|---|---|---|
| P1 | done | `mspm0_runtime` | 当前所有 Driver 的板级基础，同时存在 Driver→App、ISR 回调和多资源混管；必须先建立干净底座 | `done/plan1_mspm0_runtime_rewrite.md` + 收口契约 `done/plan1_fix_runtime_closeout.md` |
| P2 | done | `encoder` | 高度依赖 runtime，且把方向、速度和状态写进 Motor 全局；先拆它才能独立重写 Motor | `done/plan2_encoder_rewrite.md` + 收口契约 `done/plan2_fix_encoder_closeout.md`（G3519 起计数源已换硬件 QEI，公共 API 不变） |
| P3 | done | `motor` | 硬件风险最高；待 Encoder 不再依赖 Motor 状态后单独重写，并执行 P0 参数确认 | `done/plan3_motor_rewrite.md` |
| P4 | done | `key` | 从共享 GPIO IRQ/Runtime 通知中拆出，形成主动读取事件接口 | `done/plan4_key_rewrite.md` |
| HT | **pending（下一个派工）** | `tests/host` 主机测试基线恢复 | G3519 迁移未带入主机测试套件（拓扑 V16），是 P5 及一切后续施工的前置 | `plan_host_tests_restore.md` |
| P5 | pending（等 HT） | UART 角色驱动 | 按 Vision、VOFA、Stepmotor 的实际角色逐个迁移，禁止上层 ISR 回调 | `plan5_uart_role_drivers.md` |
| P6 | queued | I2C 器件驱动 | EEPROM、OLED 分别明确总线、超时和错误所有权（MPU6050 已移除，范围收窄） | 后续编写 |
| P7 | queued | 其他 Driver | IMU（`imu_uart` TX 角色随 P5.T3）、Step Motor 等按依赖图继续拆分 | 后续编写 |

2026-07-16 目录整理：已完成并验收的计划移入 `done/`（P1–P4 及 FIX-BAUD 共 7 份，只读历史契约，不再修订）；`obsolete/` 存放被 G3519 迁移作废的文档（`encoder_measurement_baseline.md` 记录的是已删除的 GROUP1 软件判向机制，该目录可整体删除）。本目录顶层只保留：本索引、待派工计划（`plan_host_tests_restore.md`、`plan5_uart_role_drivers.md`）。

2026-07-16 备注（第二次修订）：REASONIX 提交 `SELF_CHECK_BLOCKED`，证据确认 P1（P1.3–P1.5）与 P2（P2.1 硬件、P2.4/P2.5）存在未完成项，“P1/P2 已验收”前置条件不成立。Codex 处置：建立收口契约 `plan1_fix_runtime_closeout.md`（含 P1.3/P1.4/P1.5.2 向 P5/P4 的正式移交与 P1 完成定义收窄）与 `plan2_fix_encoder_closeout.md`（承接 P2.4/P2.5 软件残余与 P2.1/§7 硬件基线）；plan3/plan4/plan5 前置条件同步改为引用具体 E 行验收结果。强制施工顺序：**P1F.T1 → P2F.T1 →（P3.T1/T2 与 P4 可并行）→ P5.T1/T2 → P5.T3**；（2026-07-16 更新：硬件验收整体取消，见下方裁定；原硬件行全部作废。）

2026-07-16 流程简化（用户裁定，权威版本）：撤销 REASONIX 独立自检阶段，`reasonix-embedded-self-check` skill 已删除。现行流程为 **Codex 计划 → REASONIX 施工并提交行级完成报告（`CONSTRUCTION_DONE`/`CONSTRUCTION_BLOCKED`，每条 E 行一行）→ Codex 精简验收**（协议见 `.agents/skills/reasonix-embedded-accept/SKILL.md`：diff 审读 + 复跑扫描 + 主机测试一遍 + 复用施工构建证据 + 聚焦读核心 hunk）。历史文档中的 `SELF_CHECK_*` 记录为当时事实，不再回改。

2026-07-16 硬件验收取消（用户裁定，权威版本，覆盖本文件及五份计划中所有与之矛盾的条款）：**一切验收只做软件验收**——依赖扫描、主机测试、构建、diff 审读；软件行全过即标 `done`，验收结论只有 `CODEX_ACCEPTED`/`CODEX_REJECTED`。所有硬件验收行（原 P1F.T2、P2F.T2、P3 旧 E09–E12、P4.T3、P5 E14–E16）作废，板上调试与实测由用户自行负责。保留的底线：电机等功率器件的**软件侧安全设计**（上电安全态、单一所有者限幅、slew、换向过零死区、命令超时停止）仍是验收内容，用主机测试证明。§3 P0、§5 第 9 步、§7 硬件证据条、§8 硬件完成条按本裁定解读为“用户自理的操作性提醒”，不再是验收阻塞项。

进度（2026-07-16）：P1F.T1、P2F.T1 已获 `CODEX_ACCEPTED`（软件验收唯一制下为终局；提交 `455a968`），P1、P2 标 `done`。plan3 斜坡冲突已消除（修订 2：Motor 私有对称 slew limit）；plan3 T3 已改为 syscfg PWM 频率统一的纯软件任务（修订 3）。下一步：P3.T1 与 P4.T1 可并行开工。

2026-07-16 G3519 迁移本地适配（覆盖本文件及各计划中与之矛盾的环境事实，历史记录不回改）：工程已整体移植到 `2026_Diansai`（MSPM0G3519/LQFP-100，SDK 2.11.00.07，见 `docs/MIGRATION_G3507_TO_G3519.md`）。生效事实：① 唯一硬件配置源为仓库根 `board.syscfg`（各计划中 `project/mspm0/board.syscfg` 为旧仓库路径）；② 编码器改 TIMG8/TIMG9 硬件 QEI，GROUP1 中断仅服务按键（P2 系列文档中"GROUP1 软件判向"描述仅为历史事实）；③ 步进总线物理实例 UART2→UART7（引脚 PB15/PB16、波特率 230400 不变）；④ MPU6050/I2C_IMU 已移除——P6 范围相应收窄为 EEPROM、OLED；⑤ 灰度 8 路升级 12 路；⑥ **`tests/host/` 主机测试套件未随迁（拓扑 V16）**——P5 或任何后续施工开工前，必须先从 `../NUEDC/tests/host/` 迁入套件并复跑恢复全绿基线，此前所有 `make -C tests/host` E 行不可执行。

P2 选择 Encoder 而非 Motor 的原因：Encoder 当前直接写 `g_tMotors[]`，Motor 又持有 PID 指针，形成 `Encoder -> Motor <-> PID` 的交叉所有权。先移除 Encoder 对 Motor 的依赖，Motor 才能在 P3 只保留“方向 + PWM + 安全状态”职责。

## 5. 每个模块的固定执行协议

1. **冻结基线**：记录 `git status`、当前构建结果、模块 API、全部调用者和硬件配置。
2. **画完整链路**：从 DL HAL/ISR 到 Driver、Service、Middleware、Task，再到最终硬件输出；记录每一步单位和数据处理。
3. **先写失败验证**：依赖扫描、主机单元测试或确定性硬件复现至少有一项在旧实现上失败。
4. **先定最小接口**：调用者只看到能力、不可变输入和快照；不得保留“以后可能用”的通用 API。
5. **迁移内部实现**：资源私有化，删除反向调用、共享全局和无界等待。
6. **迁移直接调用者**：只改为适配新接口所必需的代码，不夹带业务重写。
7. **删除旧接口**：同一提交/步骤内删除兼容回调和废弃状态，禁止长期双轨运行。
8. **软件验收**：依赖扫描、主机测试、覆盖率、clean build、警告与 `git diff` 审查。
9. **硬件验收**：不用调试器；使用串口/OLED、示波器、逻辑分析仪和分阶段硬件现象。
10. **更新计划**：记录通过项、未执行硬件项、参数结论和下一模块阻塞条件。

## 6. 全阶段禁止事项

- 禁止为保持旧调用方式而新增 Driver→App 回调、弱符号或 `extern` 上层变量。
- 禁止创建新的大一统 HAL、Bus Manager 或泛型设备框架。
- 禁止在多个层重复方向修正、滤波、单位换算和限幅。
- 禁止用大量状态码和无实际处理动作的 `if` 掩盖边界不清。
- 禁止在 UART2 实际连接设备未确认前改写 Stepmotor/IMU 共用发送链。
- 禁止在 Motor P0 闸门未关闭前带负载测试。
- 禁止用 Debug 目录生成文件代替修改唯一配置源 `project/mspm0/board.syscfg`。

## 7. 阶段完成标准

- 每个 Driver 有单一职责和唯一硬件资源所有者。
- `rg` 搜索不到 Driver 包含 App/Middleware，也搜索不到上层注册回调。
- App 不再直接包含 `ti_msp_dl_config.h` 或 `ti/driverlib/*`。
- Middleware 不包含 Driver，不读取 Driver 全局变量。
- Service 主动读取 Driver 快照、按值调用 Middleware、再向 Driver 下发命令。
- 主机可测代码覆盖率达到 80% 以上；ISR/HAL 路径有逻辑分析仪或硬件冒烟证据。
- 全工程 clean build 通过且没有新增警告。

## 8. 计划维护规则

- 状态只允许：`pending`、`in_progress`、`blocked`、`done`。
- 开始执行模块前把对应状态改为 `in_progress`。
- 同一阻塞连续三次无法解决时才标为 `blocked`，并记录证据和所需输入。
- 只有软件验收与要求的硬件验收都完成后才能标为 `done`；硬件未测必须保持 `in_progress`。
- 发现新依赖或安全风险时先更新本总表和对应模块计划，再继续编码。
