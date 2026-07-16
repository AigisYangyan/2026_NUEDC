# agent/ 目录索引与项目脉络

更新：2026-07-16。本文件是 `agent/` 的导航图：先看这里，再进具体计划。架构现状的唯一权威是 `api_architecture_topology.md`（AGENTS.md §14 强制维护）。

## 1. 已完成的脉络（时间线）

| 阶段 | 内容 | 结果 |
|---|---|---|
| Phase 1（2026-07-13，旧 NUEDC 仓库） | 移除自制 sdk/hal，确立 DL HAL 边界：runtime/system/GPIO/PWM/UART-DMA/I2C 迁移、common type 清理、CCS 元数据脱钩 | done，见 `phase1_DL_HAL/`（纯历史归档，含 G3507 时期的硬件冒烟清单，不再执行） |
| Phase 2 P1：`mspm0_runtime`（+P1F 收口） | 删除 Runtime 死接口与时间包装，时间统一走 `Clock_NowMs()`；UART 角色迁移移交 P5、按键 IRQ 移交 P4 | `CODEX_ACCEPTED`，V01 closed |
| Phase 2 P2：`encoder`（+P2F 收口） | 拉取式快照 API（`Encoder_Init/Update/GetSnapshot`），删除对 Motor 全局的写入与 10 个 deprecated API；PID 入口改按值目标/反馈 | `CODEX_ACCEPTED`，V05 closed、V04/V06 收口 |
| Phase 2 P3：`motor` | `Motor_SetOutput/Update/Brake` 状态机：slew 限速、换向过零 +5ms 死区、100ms 命令超时归零；PWM 统一 80MHz/period 7999（10kHz） | `CODEX_ACCEPTED`，V04/V06/V11/V12 closed |
| Phase 2 P4：`key` | Key 改经 `BoardGpio` 拉取边沿/电平位图，GROUP1 ISR 只置私有位图，`Key_NotifyIrq` 删除 | `CODEX_ACCEPTED` |
| 流程裁定（2026-07-16） | 撤销 REASONIX 自检阶段（三阶段流程）；取消硬件验收（软件验收唯一制，板上实测用户自理）；E 行预算 ≤6/任务 | 现行流程 |
| G3507→G3519 迁移（2026-07-16，本仓库） | 工程移入 `2026_Diansai`：MSPM0G3519/LQFP-100、SDK 2.11、步进总线 UART2→UART7、编码器升级 TIMG8/TIMG9 硬件 QEI（公共 API 零变化，GROUP1 只剩按键）、MPU6050/I2C_IMU 移除、灰度 8→12 路 | 提交 `ccf3fee`…`fc86063`；配方见 `docs/MIGRATION_G3507_TO_G3519.md` |
| Agent/skills 本地适配（2026-07-16） | AGENTS.md 入库并标注 G3519 工程事实；拓扑同步（删 MPU6050、QEI 数据流、V16 登记）；三个 REASONIX skills 注入本地事实；plan5 修订 5 | 提交 `a7446c6` |
| 计划目录整理（2026-07-16） | 完成计划移入 `done/`，作废文档移入 `obsolete/`，新建 HT 派工计划与本索引 | 本次提交 |

## 2. 目录地图

```text
agent/
├── README.md                          ← 本文件（导航 + 脉络）
├── api_architecture_topology.md       ← 架构现状唯一权威，编码前必读、编码后必更
├── phase1_DL_HAL/                     ← 纯历史归档（G3507 时期），只读
└── phase2_driver_rewrite/
    ├── phase2_driver_rewrite_plan.md  ← Phase 2 索引（模块顺序、裁定记录）
    ├── plan_host_tests_restore.md     ← ★ 下一个派工：HT.T1 迁入 tests/host 恢复基线
    ├── plan5_uart_role_drivers.md     ← P5 待派工（前置：HT.T1 验收）
    ├── done/                          ← 已验收计划（7 份），只读历史契约
    └── obsolete/                      ← 被 G3519 迁移作废（GROUP1 软件判向基线），可整目录删除
```

## 3. 接下来的执行顺序

1. ~~**HT.T1**~~ done（`CODEX_ACCEPTED` 2026-07-16）：`tests/host` 已迁入，32 项全绿，V16 closed。主机测试入口：`make.bat -C tests/host all`（或直接 CCS gmake）。
2. **P5.T1–T3**（`plan5_uart_role_drivers.md`，按修订 5 解读）——**当前下一个派工，前置已满足**：Vision / VOFA / StepMotor 三个 UART 角色驱动化，消灭 Runtime 上层回调（V02/V09），IMU 迁 `imu_uart` TX 角色。
3. **P6**（待编写）：EEPROM、OLED I2C 器件驱动收口。
4. **P7**（待编写）：其余 Driver 拆分。
5. **Service 层承接**（待规划）：关闭 V07/V10/V13/V14/V15（Task 直接编排 Driver/PID、Service 空缺、可写全局、UI 直调 Driver、VOFA 跨层注册）。

## 4. 违规登记现状速览（细节见拓扑 §6）

- **closed**：V01、V04、V05、V06、V11、V12、V16（2026-07-16 HT.T1）。
- **open**：V02（Runtime 上层回调，归 P5）、V03（App 直调 DL HAL 残余）、V07、V08（Emm42 extern App）、V09（VOFA ISR 解析，归 P5）、V10、V13、V14、V15。
