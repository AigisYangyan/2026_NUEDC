# 2026 电赛 · MSPM0G3519 智能车/二维云台固件

全国大学生电子设计竞赛（NUEDC）2026 参赛固件。由 `NUEDC`/G3507 移植到 **MSPM0G3519**，
严格分层架构 + 拓扑/机械闸门强制。**Driver、Middleware 两层已全部实现并验收，App 上层重置的
Service/调度/UI/调参/诊断条目全部完成且已点亮为现役启动路径；唯一未写的是赛题公布后才做的
最终编排 Task（T01）。** 主要未决集中在硬件侧：一批占位常量/几何标定/极性位序等待上板实测替换。

---

## 工程事实

- **MCU**：MSPM0G3519（LQFP-100）；**SDK** 2.11.00.07；**工具链** ticlang 4.0.4。
- **唯一硬件配置源**：仓库根 `board.syscfg`（SysConfig，勿在别处硬编码实例/引脚）。
- **外设映射**：编码器 = TIMG8/TIMG9 硬件 QEI；步进总线 = UART7（两台 ZDT X42S，256000）；
  VOFA 上位机 = UART5 / PA0·PA1 / 230400 / DMA；视觉 = UART1 / 230400 / DMA；
  灰度 = 12 路数字量；BSL 入口 = UART0 / 9600。
- **移植配方**：`agent/MIGRATION_G3507_TO_G3519.md`。（旧 MPU6050/I2C_IMU 已移除，新陀螺仪待接。）

## 构建 & 测试

```powershell
# 固件（必须经 PowerShell；Bash 里调 make.bat 会静默空转、假报成功）
rtk make -C Debug all          # 产出 Debug/2026_Diansai.out / .hex

# 主机单元测试（真实 Driver/Middleware/Service + fake 端口层）
rtk make -C tests/host all      # MS02 时点 479 PASS / 0 FAIL（之后随 T-GQ/T-VTXQ 继续增长）
```

## 目录结构

```
hc-team/
  driver/       # 硬件驱动——唯一允许碰 DL HAL 的层
  middleware/   # 无硬件依赖的算法（PID / 里程计 / 循迹 / 速度规划 / 视觉映射…）
  app/
    service/    # 控制环触发 + 逻辑编排（现役）
    scheduler/ system/ ui/   # 装配/调度/菜单（现役 World-2；另含 World-1 冻结件）
    tasks/      # 旧赛题 Task —— World-1 冻结，待 T01 整体替换删除
agent/          # 拓扑、分阶段计划、迁移记录、风险登记（架构权威 AGENTS.md 在仓库根）
tests/host/     # 主机侧单元测试 + fake 端口
docs/           # 硬件配置指南与通信协议文档
board.syscfg    # 唯一硬件配置源
```

## 架构分层（硬约束）

调用只能自上而下：`App Task/Scheduler/UI → Service → Middleware → Driver → DL HAL`。
反向依赖、跨模块读写另一模块全局、Driver 调上层回调、ISR 做协议解析——一律禁止；
由 `.claude/hooks` 机械闸门 + 拓扑评审子 agent 强制。
架构唯一权威 = 根 `AGENTS.md`；当前真实状态 = `agent/api_architecture_topology.md`。

## 当前进度

| 层 | 状态 | 内容 |
|---|---|---|
| DL HAL (phase1) | ✅ 完成 | mspm0_runtime 运行时边界 |
| Driver (phase2) | ✅ 完成并验收 | clock / gpio / uart×N / motor(+hw) / encoder(QEI) / gray / oled / emm42 / bsl_entry / param_store / uart_vofa / uart_vision |
| Middleware (phase3) | ✅ 完成 | pid / track_error / odometry / heading / track_elements / speed_plan / vision_aim / move_profile |
| App Service·Sched·UI (phase4) | ✅ DONE，World-2 现役启动 | 见下 |
| **赛题 Task (T01)** | ⏳ **未写**（赛题公布后） | 薄编排；届时一次性替换删除全部旧 `tasks/**` |

**App Service（均已 DONE / ACCEPTED）**

- **S01 chassis** — 底盘速度内环（左右轮 PID 触发）
- **S02 line_follow + lost_line** — 循迹外环 + 丢线记忆/回退/有界超时
- **S03 tuning** — VOFA 遥测与在线调参
- **S04 hmi** — Key / OLED 人机输入显示
- **S05 视觉三闭环** — `uart_vision`(0xAA55 坐标帧+0xFF 选题握手) ｜ `vision_aim`(PD 像素误差→X/Y 脉冲) ｜ `gimbal`+`gimbal_stepbus`(握手+瞄准收敛+步进总线)
- **S06 motion** — 直行 N mm / 定角转 / 定点停 / 航向保持 / 圆弧 / 定长梯形直行
- **S07 route** — 段表驱动分段路线（FOLLOW / STRAIGHT / TURN / ARC）
- **SCH01 scheduler · UI01 menu** — 调度器（时间参数注入、零 Driver 依赖）+ 两级菜单/参数表
- **param_store + param_tune** — 片内 flash 持久化 + 现场按钮调参
- **调试诊断条目** — encoder_test（编码器脉冲）/ motor_check（"MotorDir" 方向自检）/ gray_check（"GrayTest" 12 路灰度）

**最近里程碑（2026-07-20）**：MS01 定长梯形直行原语 → MS02 上板封装 + 防跑飞/堵转双看门狗 →
syscfg 步进波特率 256000 → T-GQ1 emm42 快速位置组包 → T-GQ2 双轴绝对协同一帧发 →
T-VTXQ1 VOFA 软件 TX 队列（忙时入队而非丢帧）。

## 硬件待测（上板实测，用户自理；硬件约 2026-07-23 到货）

- [ ] **电机安全常量**：reverse_deadtime=5ms / timeout=100ms / slew=100‰·ms⁻¹ 为占位，待 T3 实测（`driver/motor/motor.c`）
- [ ] **VOFA TX DMA 真机续传**：主机 fake 只证队列/kick 逻辑，`IsrTxDone` 内 DMA 续传段待上板确认（T-VTXQ1）
- [ ] **mm_per_pulse 标定**：占位；未测则定长直行过冲（`app_compose.c`）
- [ ] **整车几何标定**：pitch_mm / track_width_mm / mm_per_pulse / 压角点 L（`agent/GEOMETRY_CALIBRATION.md`）
- [ ] **灰度左右位序 bit0_is_left**：用 GrayTest 实测撤销猜测占位（灰度硬件方案 = 5V + 串阻）
- [ ] **编码器 AB 极性**：用 encoder_test 确认「正转→脉冲正增」；唯一修正点 `encoder.c s_direction_sign[]`
- [ ] **二维云台**：步进使能 + 机械零点对齐 + 带载低速短行程先验方向/停止
- [ ] **param_store flash**：擦/写真值、扇区不与 .out/BSL 段重叠
- [ ] **增益整定**：底盘速度环默认增益 0（先 SpeedTune 才动）；vision_aim kd=0 待整定；LineFollow 为 UNCALIBRATED 占位
- [ ] **X42S 波特率 256000** 真机验证

> 已排除的疑似阻塞：UART5 PA0/PA1 已确认连接；BSL 入口已实现（D14，收 0x22→跳 ROM BSL）。

## 待填入（占位值 / 预留点，需人工补）

- **调参初值**：`TUNE_DEFAULT_KP/KI/KD` = 0、`TUNE_STEP_*` 步进占位「现场再定」（`param_tune.h/.c`）
- **循迹/直行占位**：LineFollow 阈值组、mm_per_pulse、heading_sign、巡航/转向/圆弧速度、track_width_mm=100（`app_compose.c`）
- **新陀螺仪接入**：`app/tasks/task1/task1.c` 的 `TODO(新陀螺仪)`（该文件属 World-1 冻结，最终随 T01 处理）
- **故意预留未接**：gimbal 里程计前馈钩子（`gimbal.h`）、uart_vofa 的 ESP32 平台分支

## 还没编写

- **T01 赛题薄编排 Task**（赛题公布后）：把现役 Service 编排成赛题流程；届时一次性删除
  全部 World-1 冻结件（`app/tasks/**`、旧 scheduler/system/ui、`vision_coord`），关闭债务 V03/V07/V13/V14/V15。
- **零调用者待接线**（设计预期，非缺陷）：`route` 服务、`gimbal` 服务级、`motion` 的
  STRAIGHT/TURN/ARC 非-profiled 原语——都等 T01。
- **余力项（硬件未定案，不预支）**：双机协同 / 测距避障、声光提示（buzzer / LED）。

## 文档索引

- `AGENTS.md` — 架构唯一权威（§1–§15，含依赖矩阵、数据流、风险登记、检查清单）
- `agent/api_architecture_topology.md` — 当前拓扑 · 风险登记 · 更新日志
- `agent/phase4_app_rewrite/plan_app_first_order.md` — 当前阶段严格计划表（逐项状态）
- `docs/` — 硬件与协议指南：`通信协议.md`、`VOFA_PROTOCOL.md`、`X42S云台步进上位机配置指南.md`、
  `12路灰度传感器配置指南.md`、`IMU陀螺仪配置指南.md`、`视觉通信协议_主控确认版_v1.md`
