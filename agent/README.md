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
| G3507→G3519 迁移（2026-07-16，本仓库） | 工程移入 `2026_Diansai`：MSPM0G3519/LQFP-100、SDK 2.11、步进总线 UART2→UART7、编码器升级 TIMG8/TIMG9 硬件 QEI（公共 API 零变化，GROUP1 只剩按键）、MPU6050/I2C_IMU 移除、灰度 8→12 路 | 提交 `ccf3fee`…`fc86063`；配方见 `agent/MIGRATION_G3507_TO_G3519.md` |
| Agent/skills 本地适配（2026-07-16） | AGENTS.md 入库并标注 G3519 工程事实；拓扑同步（删 MPU6050、QEI 数据流、V16 登记）；三个 REASONIX skills 注入本地事实；plan5 修订 5 | 提交 `a7446c6` |
| 计划目录整理（2026-07-16） | 完成计划移入 `done/`，作废文档移入 `obsolete/`，新建 HT 派工计划与本索引 | 提交 `36d4d65` |
| Phase 2 P8：新单轴 IMU 驱动重写（2026-07-17） | **器件已更换**（用户解除 P7 的 IMU 暂缓裁定）。新器件与旧 IMU 协议无交集：5 字节定长帧、内置 Kalman 解算、只出 Yaw 与 GyroZ。删除旧 `IMU.c/.h`(616 行/0x7E 九轴协议)，新增 `imu.c/.h`（解析+快照+新鲜度+诊断）；`imu_uart` 补 RX FIFO 并接线 UART3 IRQ；`board.syscfg` 的 `UART_IMU` 230400→**115200** 并开启 RX 中断 | `ACCEPTED`，`plan_p8_imu_rewrite.md`，E01–E06 全过，主机 **98 项**（+22），违规边 `IMU_API --> DL_HAL` **closed** |
| Phase 2 P7：emm42 残渣清理（2026-07-17） | 巡查推翻原范围：**Step Motor 已于 P5 拆完**（`emm42.c` 已是纯组包），**IMU 无外部调用者且 RX 未接线**（`mspm0_runtime.c` 中 IMU 字样 0 处）。**IMU 部分经用户 2026-07-17 裁定推迟**（IMU 与 12 路灰度后续要改），已施工的 IMU 改动整体回退。实际交付：清除 emm42 未引用全局 `g_emm42_default_*` 与空壳 `Emm42_RunCommandTask` | `ACCEPTED`（范围收窄），`plan_p7_imu_stepmotor.md`，E01/E03/E05/E06 过、E02/E04 标 N/A（IMU 未施工），主机测试 76 项 |
| Phase 2 HT.T1：`tests/host` 恢复 | 主机测试套件从旧仓库迁入 `2026_Diansai`，32 项基线全绿 | `CODEX_ACCEPTED`，V16 closed，提交 `d57b728` |
| Phase 2 P6：I2C 屏幕收口 + EEPROM 删除 | `driver/eeprom/at24cxx.*` 死代码整体删除（含构建登记）；OLED 公共头由 10+ 符号收敛为 6 个显示能力接口，页寻址/字模/pow/总线恢复全部私有；`50000u` 魔数超时改为 `2 byte×9 bit/400 kHz ×2 = 90 µs → 1800 loops` 算式推导；新增主机 OLED 测试 15 项 | `CODEX_ACCEPTED`，V17 登记并同批次 closed，主机测试 76 项 |
| Phase 2 P5：`board_uart` 四角色（T1+T2+T3 统一施工） | 新增 vision/vofa/stepmotor/imu 角色 Driver 与私有 FIFO；Runtime 回调与 9 个 Send/Busy 接口删除；VOFA 解析迁任务态；emm42 纯组包；IMU 迁专用 `UART_IMU`；uart_stress 改有界等待独占 | `CODEX_ACCEPTED`，V02/V08/V09 closed、V03 partially closed，主机测试 61 项，提交 `b24a456` |
| QEI/灰度引脚重映射（2026-07-16） | 编码器 4 脚中 3 个是核心板晶振脚（PA3=LFXIN、PA6=HFXOUT、PA2=ROSC）——核心板为现成模块，晶振实焊，固件不可绕过。`QEI_LEFT`→**TIMG9/PB7/PB9**、`QEI_RIGHT`→**TIMG8/PB10/PB11**；灰度 IN4/IN5/IN10/IN11→**PB8/PB20/PB14/PB0**（IN4 用 PB8 而非表建议的 PA7，保住 12 路同端口原子采样）。释放 PA2/PA3/PA6/PA7 | `ACCEPTED`，`plan_qei_gray_pinmux.md`，E01–E06 全过，**驱动零改动**，引脚表 11 行冲突全消 |
| 流程自闭环（2026-07-16，用户裁定） | 取消第二个施工者：三个 `reasonix-embedded-*` skill 合并为 `.agents/skills/embedded-closed-loop`，删除 GPT 承包商注册与派工 Prompt；流程改为**单 agent 决策→施工→验收**，标签 `ACCEPTED`/`REJECTED`。代偿机制：**契约含 E 行须在写代码前先提交**，由 git 充当第二方 | 现行流程（权威：`phase2_driver_rewrite_plan.md` 同日「流程自闭环」条） |

## 2. 目录地图

```text
agent/
├── README.md                          ← 本文件（导航 + 脉络）
├── api_architecture_topology.md       ← 架构现状唯一权威，编码前必读、编码后必更
├── MIGRATION_G3507_TO_G3519.md        ← G3507→G3519 移植配方（2026-07-16 由 docs/ 移入；AGENTS.md 引用）
├── phase1_DL_HAL/                     ← 纯历史归档（G3507 时期），只读
└── phase2_driver_rewrite/
    ├── phase2_driver_rewrite_plan.md  ← Phase 2 索引（模块顺序、裁定记录）
    ├── plan_driver_first_order.md     ← ★ Driver 层唯一进度权威（AGENTS.md §15 引用，开工前必读）
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
5. ~~**引脚表 v2 迁移**~~ **已闭环**（2026-07-16）。事实源方向裁定（用户）：**硬件围绕固件走** —— 先定 `board.syscfg`，硬件组照配置画板；引脚表是 syscfg 的**派生产物**，不是事实源。据此四问全部落地：
   - **Q1（封装 LQFP-64 vs 100）**：**不影响任何裁定，继续挂起**。经核实本次涉及的全部引脚（PB7/PB9/PB10/PB11/PB8/PB20/PB14/PB0、PA0/PA1）在两种封装下均存在。
   - **Q2（IMU 串口）/ Q3（VOFA）**：用户 2026-07-16 裁定 —— IMU 定 PA25/PA26，VOFA 定 UART5/PA1/PA0。已施工。
   - **Q4（编码器⇄灰度对调）**：**硬件组是对的，固件是错的**。编码器原占的 PA3/PA6/PA2 分别是 `SYSCTL.LFXIN`/`HFXOUT`/`ROSC`，核心板为现成模块、晶振实焊，固件无法绕过 —— 这是引脚表比固件更接近物理事实的**唯一一处**。已按 `plan_qei_gray_pinmux.md` 施工并验收，表的 11 行冲突全消。
   - 「左右轮 timer 归属翻转导致静默对调」的风险**不存在**：syscfg `$name` 继续与物理轮子绑定，实例号是 Driver 以下私有事实（E04 证实驱动零改动）。
6. **P7**（2026-07-17 `ACCEPTED`，范围收窄，`plan_p7_imu_stepmotor.md`）：原定「其余 Driver 拆分」，巡查后**两项均不成立** —— Step Motor 已于 P5 拆完；IMU 的问题不是耦合而是一行冗余头包含。**IMU 部分经用户裁定推迟**，仅交付 emm42 残渣清理。
   - **★ IMU 的「暂不编写」裁定已于 2026-07-17 由用户解除**（用户更换了 IMU 器件并要求重写）→ 见下方 P8。**12 路灰度的裁定仍然有效，动它前须先确认。**
   - P7 记录的两项 IMU 缺陷（`IMU.h:5` 冗余头、三处 32MHz 延时假设）**随 P8 删除旧 `IMU.c/.h` 一并消失**，不再是未结项。
7. **P8**（2026-07-17 `ACCEPTED`，`plan_p8_imu_rewrite.md`）：新单轴 IMU 驱动重写，面向小车底盘。
   - **器件换了，不是改**：旧 0x7E 九轴多功能码协议 → 新 5 字节定长帧单轴模组。参考资料在 `hc-team/IMU_NEW_EXAMPLE/`（未纳入版本控制）。
   - **器件内置 Kalman 解算** → 驱动**禁止**再做积分/滤波/方向反转/单位再换算（AGENTS.md §8.2 单一所有者）。旧驱动的 `IMU_Update_Yaw_Integration()` 是对裸陀螺的积分，对本器件属重复处理，已随旧文件删除且**不得迁移回来**。
   - **航向角 unwrap 不在 Driver**：器件出 `[-180,180)`；连续多圈航向是数据处理，属 Middleware/Service。
   - **未实现 BIAS_CAL（陀螺零偏校准）**：21 秒阻塞、一次性台架动作，可用厂家上位机完成。需要时另开派工。
   - **★ 上板必测**：① 实测输出速率（`Imu_GetDiag().frame_count` 1 秒增量 ÷ 2）—— 出厂默认 10Hz 对底盘偏航闭环太慢，须用 `Imu_SetOutputRate()` 提到 100/200Hz；② 波特率裁定验证（若 `checksum_error_count` 暴涨而 `frame_count` 不涨 → 115200 判断错）；③ **航向角正方向**（车体转向 vs `yaw_deg` 增减），修正点只能有一个。
   - **`Imu_ZeroYaw()` / `Imu_SetOutputRate()` 写器件 flash**，均为一次性动作，禁止放进周期任务。
8. **★ 用户裁定（2026-07-17）：App 上层将整体重置 → 当前只做 Driver 层，不管上层调用者。**已写入 `AGENTS.md` §15，严格计划表见 `phase2_driver_rewrite/plan_driver_first_order.md`。
   - **Driver 零调用者是预期状态，不是缺陷** —— 不得因此推迟 Driver 工作，也不得在 Task 里直接调 Driver 来「制造调用者」（那是复制 V07/V03 违规）。
   - **Driver 层实质只剩 12 路灰度一块缺口**（`driver/gray/` 不存在，采样仍在 `track_follow.c` 里直接调 `DL_GPIO_readPins` = V03 残留），而它**仍在用户暂缓裁定下**。解除灰度裁定之时即 Driver 层收官之时。
9. **Service 层承接**（**裁定解除后才启动，当前不得施工**）：关闭 V03(残留)/V07/V10/V13/V14/V15。现有 `app/**` 不是范例，它就是这堆违规本身。

## 3.1 待办（有裁定但未立计划）

- **BSL ENTRY 监听器未实现**（调试串口迁移遗留，需立计划）：`UART_BSL_ENTRY`(UART0/9600) 已配好但**无消费者**——上位机 ENTRY 字节 `0x22` 的监听与软件跳 BSL 尚未落地。在此之前，**软件跳 BSL 不可用**，只能用硬件 BSL invoke 引脚 + 复位。参考实现：SDK `bsl_software_invoke_app_demo_uart/main.c:197`（判 0x22）+ `invokeBSLAsm()`（擦 SRAM + `DL_SYSCTL_RESET_BOOTLOADER_ENTRY`，含 BSL_ERR_01 勘误绕行）。
- **PA0/PA1 待硬件组引出**（调试串口迁移遗留）：用户 2026-07-16 明示板上尚无这两个脚，将要求硬件组新画。**在引出前 VOFA 在实物上不可用**（固件已就绪）。
- **BSL 波特率提升到 230400（可选，高风险）**：需自定义 UART BSL flash 插件（`BSL_UART_DEFAULT_BAUD` 改 230400）。代价：动 NONMAIN + linker + BCR 写保护，插件默认落 0x2000 与 app 撞车（建议挪 flash 顶部），配错要 SWD 工厂复位。不做则 BSL 恒为 9600（ROM 固定）。
- ~~旧引脚表删除待确认~~ **已闭环**（2026-07-16 用户裁定）：`docs/` 现只保留给硬件组的最新文件（`主控板引脚表(2).xlsx` 一份）；旧表 `_G3519.xlsx` 与可读导出 `pin_table_v2/` 均已删除；`MIGRATION_G3507_TO_G3519.md` 移入 `agent/`（它是固件侧文档，被 `AGENTS.md` 等 11 处引用）。历史版本经 git 追溯。
- **★ Encoder 极性须在新板上重新实测**（2026-07-16 QEI 迁移后升级为必办项）：`encoder.c:41` `s_direction_sign[] = {-1, 1}` 是对**旧板** AB 接线极性的补偿（AB 接反是预期内硬件事故）。编码器已迁至 PB7/PB9（左）、PB10/PB11（右），新板重画后 AB 极性可能不同：若硬件组按 A相→PB7/PB10、B相→PB9/PB11 接正，该常量须改 `{1, 1}`；若仍接反则保持。**验证方法**：手推左轮前进，`Encoder_GetSnapshot()` 左轮速度应为正。**禁止新增第二个反转开关**（两处反转互相抵消）。无实测依据不得改动此常量（AGENTS.md §8.1）。附带待办：使其板级可配置 + 主机测试覆盖两种极性。
- **核心板晶振实焊情况待硬件组书面确认**（2026-07-16 QEI 迁移的前提）：本次迁移假设核心板 PA3/PA5/PA6 上确有实焊晶振（依据：引脚表 r44 硬件组备注 + TI 复用表）。若实物未焊晶振，则迁移非必需（但无害，且释放了 3 个脚）。固件本身用内部 SYSOSC，不使用晶振。

## 4. 违规登记现状速览（细节见拓扑 §6）

- **closed**：V01、V04、V05、V06、V11、V12、V16（HT.T1）、V02/V08/V09（2026-07-16 P5）、V17（2026-07-16 P6，登记与关闭同批次）。
- **partially closed**：V03（vision_bus/stepmotor_bus/uart_stress 已清零；`tasks/track_follow/track_follow.c` 仍直调 DL HAL，归后续计划）。
- **open**：V07、V10、V13、V14、V15——全部指向同一根因（Service 层空缺），由「Service 层承接」一次性规划。
