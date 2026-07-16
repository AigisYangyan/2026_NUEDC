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
| 计划目录整理（2026-07-16） | 完成计划移入 `done/`，作废文档移入 `obsolete/`，新建 HT 派工计划与本索引 | 提交 `36d4d65` |
| Phase 2 HT.T1：`tests/host` 恢复 | 主机测试套件从旧仓库迁入 `2026_Diansai`，32 项基线全绿 | `CODEX_ACCEPTED`，V16 closed，提交 `d57b728` |
| Phase 2 P6：I2C 屏幕收口 + EEPROM 删除 | `driver/eeprom/at24cxx.*` 死代码整体删除（含构建登记）；OLED 公共头由 10+ 符号收敛为 6 个显示能力接口，页寻址/字模/pow/总线恢复全部私有；`50000u` 魔数超时改为 `2 byte×9 bit/400 kHz ×2 = 90 µs → 1800 loops` 算式推导；新增主机 OLED 测试 15 项 | `CODEX_ACCEPTED`，V17 登记并同批次 closed，主机测试 76 项 |
| Phase 2 P5：`board_uart` 四角色（T1+T2+T3 统一施工） | 新增 vision/vofa/stepmotor/imu 角色 Driver 与私有 FIFO；Runtime 回调与 9 个 Send/Busy 接口删除；VOFA 解析迁任务态；emm42 纯组包；IMU 迁专用 `UART_IMU`；uart_stress 改有界等待独占 | `CODEX_ACCEPTED`，V02/V08/V09 closed、V03 partially closed，主机测试 61 项，提交 `b24a456` |

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
2. ~~**P5**~~ done（`CODEX_ACCEPTED` 2026-07-16，提交 `b24a456`）：`board_uart` 四角色落地，V02/V08/V09 closed、V03 partially closed。E14–E16 硬件行随硬件验收取消而作废，**板上实测由用户自理**（三路 230400 实测、overflow 计数可见性、压测进出各 10 次）。
3. ~~**P6**~~ done（`CODEX_ACCEPTED` 2026-07-16）：EEPROM 器件删除、OLED 公共头收口为 6 个显示能力接口、I2C 等待上限改按 400 kHz/80 MHz 算式推导。I2C_AUX 由 `driver/oled` 独占，**未建 I2C 总线抽象层**（单器件独占不需要仲裁）；V17 登记并同批次关闭。主机测试 76 项。
4. ~~**调试串口迁移**~~ done（`CODEX_ACCEPTED` 2026-07-16，`plan_debug_uart_remap.md`，**流程例外：Codex 自施工自验收**）：`UART_HOST_LINK`(VOFA) 迁 **UART5/PA1/PA0/230400/DMA**；PA10/PA11 收归 **`UART_BSL_ENTRY`=UART0/9600/无 DMA**，专供无线 BSL。仅改 `board.syscfg`，**驱动零改动**（R05 证实）。PA0/PA1 在 LQFP-64/100 下均存在，**绕开引脚表 Q1**。⚠ 两个未闭环项见 §3.1。
5. **引脚表 v2 迁移**（`plan_pin_table_v2_migration.md`）——**`BLOCKED`**：硬件组新表与工程/表自身存在冲突，Q1–Q4 待裁定（封装 LQFP-64 vs 100、IMU 串口两 sheet 打架、VOFA 无处安放、编码器⇄灰度原子对调且 timer↔轮 归属翻转）。**与调试串口迁移同改 `board.syscfg`，须排在其后或合并施工**。Q2/Q3 已被用户 2026-07-16 裁定推翻（IMU 定在 PA25/PA26、VOFA 定在 UART5/PA1/PA0），须随 Q1/Q4 一并回告硬件组。
6. **P7**（待编写）：其余 Driver 拆分。
7. **Service 层承接**（待规划）：关闭 V07/V10/V13/V14/V15（Task 直接编排 Driver/PID、Service 空缺、可写全局、UI 直调 Driver、VOFA 跨层注册）。

## 3.1 待办（有裁定但未立计划）

- **BSL ENTRY 监听器未实现**（调试串口迁移遗留，需立计划）：`UART_BSL_ENTRY`(UART0/9600) 已配好但**无消费者**——上位机 ENTRY 字节 `0x22` 的监听与软件跳 BSL 尚未落地。在此之前，**软件跳 BSL 不可用**，只能用硬件 BSL invoke 引脚 + 复位。参考实现：SDK `bsl_software_invoke_app_demo_uart/main.c:197`（判 0x22）+ `invokeBSLAsm()`（擦 SRAM + `DL_SYSCTL_RESET_BOOTLOADER_ENTRY`，含 BSL_ERR_01 勘误绕行）。
- **PA0/PA1 待硬件组引出**（调试串口迁移遗留）：用户 2026-07-16 明示板上尚无这两个脚，将要求硬件组新画。**在引出前 VOFA 在实物上不可用**（固件已就绪）。
- **BSL 波特率提升到 230400（可选，高风险）**：需自定义 UART BSL flash 插件（`BSL_UART_DEFAULT_BAUD` 改 230400）。代价：动 NONMAIN + linker + BCR 写保护，插件默认落 0x2000 与 app 撞车（建议挪 flash 顶部），配错要 SWD 工厂复位。不做则 BSL 恒为 9600（ROM 固定）。
- **旧引脚表删除待确认**（P6 验收时发现）：新表 `docs/主控板引脚表(2).xlsx` 与可读导出 `docs/pin_table_v2/` 已入库，Q1–Q4 依据可复核。但 `docs/主控板引脚表_G3519.xlsx` 在工作树中处于已删除状态且未提交——需用户确认是否为本人操作。详见 `plan6_i2c_display.md` §10。
- **Encoder 极性/左右轮标定**：AB 相接反是预期内硬件事故（用户 2026-07-16 裁定）。全链路唯一修正点为 `encoder.c:41` `s_direction_sign[] = {-1, 1}`，**禁止新增第二个反转开关**（两处反转互相抵消）。待办是让它成为板级可配置 + 主机测试覆盖两种极性。若引脚表 v2 迁移执行（左右轮 timer 归属翻转），此常量**必须重新标定**，否则左右轮静默对调。

## 4. 违规登记现状速览（细节见拓扑 §6）

- **closed**：V01、V04、V05、V06、V11、V12、V16（HT.T1）、V02/V08/V09（2026-07-16 P5）、V17（2026-07-16 P6，登记与关闭同批次）。
- **partially closed**：V03（vision_bus/stepmotor_bus/uart_stress 已清零；`tasks/track_follow/track_follow.c` 仍直调 DL HAL，归后续计划）。
- **open**：V07、V10、V13、V14、V15——全部指向同一根因（Service 层空缺），由「Service 层承接」一次性规划。
