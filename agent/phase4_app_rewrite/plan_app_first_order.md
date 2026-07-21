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

**原顺序：A00 → S01 → S02 → S03 → S04 → SCH01 → UI01 → T01；SYS01 随各阶段增量推进。**

**修订顺序（2026-07-18，依 §12 赛前能力预制编排；A00–M02 已全部 DONE）：**
**UI01 → M01 里程计/航向 → S06 motion 语义运动 → M02 循迹元素检测 → M03 速度规划**
**→ S02b 循迹深化（元素事件面 + M03 接入）→ S07 分段路线执行 → S05 云台/视觉服务群**
**→ T01（赛题公布后）；SYS01 仍随各阶段增量推进。**
**（M03 与 S02b 拆分裁定见 §2 第 7 条，用户 2026-07-18 确认。）**

1. **依赖方向强制 Service 先行**：依赖矩阵里 Task/Scheduler/UI 只允许调 Service——
   先有 Service，上层才有东西可写。这也是用户裁定的重心。
2. **S01（底盘速度内环）先于 S02（循迹外环）**：多环级联的数据方向是「外环输出 → 内环目标」，
   内环必须先存在并可独立验收，外环才有落点（Service 同层受控调用）。
3. **S03 遥测调参服务在控制服务之后**：调参/遥测上下文的形状由已存在的服务决定，
   先建就是假想接口（V15 的教训）。
4. **S04 / SCH01 / UI01 靠后**：菜单项与生命周期编排的形状取决于 Service 能力面。
5. **T01 最后**：赛题 Task 是多环触发+逻辑编排的最终消费者；写它时一次性替换并删除
   旧 `tasks/**`，避免中途断链（构建始终保持绿）。
6. **赛前能力预制插入 UI01 与 T01 之间（2026-07-18 修订）**：T01 等赛题公布，
   等待期用于补齐赛题统计（§12）指出的能力缺口——顺序服从数据依赖：
   M01 里程计先于 S06（语义运动的位姿来源）与 S05（云台运动前馈的输入）；
   循迹深化（M02/S02b/M03）先于 S07（段切换以元素事件为触发源）；
   S05 排最后一个预制块（体量最大、且 Q7 帧解析归属需契约裁定）。
   UI01 提到最前：阻塞已清（SCH01+S04 DONE）、封箱裁定下按键+OLED 分问菜单是
   赛场生存必备、也是其后每个预制模块上板验证的入口。
7. **S02b 拆分为 M03 + S02b 两个闭环（用户 2026-07-18 确认）**：沿用 phase4 既有节奏
   「中间件纯算法先独立立契约（零消费者，如 M01/M02）→ 服务再接线（S06/S02）」——
   `middleware/speed_plan` 速度规划先作为纯算法 Middleware 独立立项（M03，契约 §17），
   TDD/审计/拓扑各自成闭环、零消费者；其后 S02b 只做 line_follow 服务接线（一次性把
   已建成的 M03 速度调制 + M02 元素事件面接入 line_follow）。每个任务有界、可独立验收。
   **速度规划减速触发信号定案（用户 2026-07-18）**：用 track_error 已算好的 `|error_mm|`
   连续曲率代理（不重复量化、不采样、不耦合外环 PID 内部）——见 §17 契约。
8. **S06b 插入 S02b 与 S07 之间（2026-07-18，用户本会话确认）**：修订顺序行把 S07 列在 S02b 之后，
   但 S07 段类型集含 `ARC`，其执行依赖 motion 圆弧原语——S06b 是 S07 `ARC` 段的前置件，
   故先做 S06b 再做 S07，S07 契约届时可一次性覆盖全部四种段类型，免二次修订。实际执行序为
   **… → S02b → S06b → S07 → S05**（S06b 契约见 §19）。
9. **S07 当前施工项，契约 §20 本提交冻结（2026-07-18，用户三问裁定）**：S06b DONE 后 S07 前置件齐备
   （四类段的执行原语 line_follow 元素事件面 + motion STRAIGHT/TURN/ARC 全部 DONE）。用户就 Q6 三问裁定：
   段表**装配层持有**（route 纯执行机制）、段类型**全四类 + 任意混合**、段间**确定性交接 + 可选段级超时**。
   契约 §20 冻结全部证据行后本会话结束；TDD/施工/审计/拓扑为后续独立闭环（契约先于代码，闭环铁律）。
   S07 与 line_follow/motion 的关键接线不变量（route 每拍至多推进一个子服务、不直接泵
   Chassis_Update/Imu_Update、不 include scheduler.h/track_elements.h）见 §20.0——由 topo-navigator
   切片核验（拓扑分层拆分后首个正式会话，导航器凭自身定义定位 `agent/topology/{driver,app}.md` 成功）。

## 3. 模块状态表

| ID | 模块 | 新目录 | 吸收/替代的存量 | 关闭的债 | 状态 |
|---|---|---|---|---|---|
| A00 | 计划 + 裁定解除记录 | `agent/phase4_app_rewrite/` | — | — | `DONE`（bffdecf + baseline chore c958a3f） |
| S01 | chassis 底盘速度环服务 | `app/service/chassis/` | speed_loop.c、task1 速度部分、task_groups 采样所有权 | V07（部分）、V10（部分） | `DONE`（契约 bffdecf，修订 926bac0；代码 8a611d5；审计处置 69c29fa。E01 0 命中 / E02 无越界 / E03 140 PASS 0 FAIL＝128 基线+12 / E04 exit 0、0 诊断、chassis.o 进链接） |
| S02 | line_follow 循迹服务（外环+丢线策略） | `app/service/line_follow/` | track_follow.c、task1 循迹部分、gray_test | V03、V03-DUP、V07（部分） | `DONE`（契约 6dfdc85，修订 88010fd；代码 bb4825c；审计处置 53e9967。E01 0 命中 / E02 无越界 / E03 159 PASS 0 FAIL＝140 基线+19 / E04 exit 0、0 诊断、两 .o 进链接。Q5 关闭：丢线策略显式重建于 lost_line） |
| S03 | 遥测/调参链路服务（VOFA） | `app/service/tuning/` | vofa_register.c | V15（替代已建成，旧边待 T01）、V19（closed） | `DONE`（契约 ed4f416，修订 57b54de；代码 d0e4996；审计处置 5a4f089。E01 0 命中 / E02 无越界 / E03 173 PASS 0 FAIL＝159 基线+14 / E04 exit 0、0 诊断、两 .o 经 linkInfo.xml 确证进链 / E05 `u8` 0 命中。Q2 定案入 §5；**W1（§22.1）tx 6→10 增益外显 DONE，418 PASS**） |
| S04 | 人机输入/显示服务（Key/OLED 包装） | `app/service/hmi/` | menu 对 Key/OLED 的直调、task_groups UI 泵送 | V14 的基础 | `DONE`（契约 f8311c8；代码 2dac572；审计处置 ad5ca08。E01 0 命中 / E02 无越界 / E03 185 PASS 0 FAIL＝173 基线+12 / E04 exit 0、0 诊断、hmi.o 经 linkInfo.xml 确证进链） |
| S05a | 视觉链路 Driver 编解码（`driver/uart_vision`：0xAA55 坐标控制帧 RX + 0xFF 选题握手 TX/RX；vision_uart 增 TX） | `driver/uart_vision/` | vision_bus/vision_coord（帧解析职责） | — | `DONE`（契约 §21.1 冻结 c1d5421，修订 1 b22ad28；代码本提交；审计处置见 §21.1.5。E01 依赖纯净 0 命中 / E02 无越界 / E03 361 PASS 0 FAIL＝334 基线+27 / E04 exit 0、0 诊断、uart_vision.o+vision_uart.o 经 linkInfo.xml 确证进链。arch-auditor 阻断 finding（vision TX 完成 ISR 未接线）已修复：mspm0_runtime.c 补 VisionUart_IsrTxDone；topo-updater 同款交叉确认。自同步分帧不引 Clock、坐标 float32 透传、无弱钩子回调。三闭环第一环，S05b/S05c 待续） |
| S05b | 视觉坐标→轴映射 Middleware（纯算法：像素误差→X/Y 轴脉冲增量，死区/步长/限幅/极性单一所有者） | `middleware/vision_aim/` | 2DPlatform 的 visionhdl_step/clamp 几何 | — | `DONE`（契约 §21.2 冻结 8b74cf0；代码本提交；审计处置见 §21.2.5。E01 依赖纯净 0 命中 / E02 无越界 / E03 377 PASS 0 FAIL＝361 基线+16 / E04 exit 0、0 诊断、vision_aim.o 经 linkInfo.xml 确证进链。arch-auditor 六轴全过、无阻断/无重要、2 建议级仅注释收敛（F1 active 单语义、F2 回拉超 max_step 有意不防御）。输出定为有符号 int32 脉冲增量/轴（非角度）；纯函数不持位置状态（cur_pulse 调用方传入）；极性唯一开关 sign[axis]、修 (int32)coord 早失精度 bug。零调用者预期态，S05c 待续。**修订1 P→PD DONE（契约 §21.4，代码本提交）**：E01 0 命中 / E02 无越界（.ccsproject 会话前既存未纳入）/ E03 416 PASS 0 FAIL＝404 基线+12（vision_aim 10 + gimbal 2）/ E04 exit 0、0 诊断、vision_aim.o+gimbal.o 经 linkInfo.xml 确证进链。arch-auditor 五项全过、无阻断/无重要、1 建议级文档处置（D 时间基=每帧无 dt、max_step 兜底、帧率漂移留调参）。kd 单一所有者=vision_aim、prev_error 状态=gimbal 持有、无 I（cur_pulse 累加即积分器）、无微分滤波（坐标已 Kalman §8.2）、kd=0 逐位退化回旧 P） |
| S05c | 云台服务（`app/service/gimbal`：选题握手编排 + 瞄准收敛环 + odometry 运动前馈 + 步进总线下沉） | `app/service/gimbal/` | stepmotor_bus/2DPlatform 控制编排 | stepmotor_bus 违规群 | `DONE`（契约 §21.3 冻结 8354b24；代码本提交；审计处置见 §21.3.5。**Service 直连最小派发** + **前馈几何推迟只留读点**——用户 2026-07-18 双裁定。E01 0 命中（无 stepmotor_bus.h/odometry.h）/ E02 无越界 / E03 401 PASS 0 FAIL＝377 基线+24（stepbus 10 + gimbal 14）/ E04 exit 0、0 诊断、gimbal.o+gimbal_stepbus.o 经 linkInfo.xml 确证进链。arch-auditor 四红线全过、无阻断/无重要、3 建议级已处置（F1 ARMING 超时采纳修复复用 ack_timeout_ms、F2 删不可达 axis 校验、F3 删未用 string.h）。cur_pulse 单一累加点、emm42 RPM 限幅未复夹、脉冲→dir/magnitude 拆分归 gimbal_stepbus。stepmotor_bus 违规群本体（baseline 14–18 行）待 T01 删文件时关闭；odometry 前馈接线亦待后续闭环/T01。视觉三闭环 S05a/b/c 全 DONE） |
| SCH01 | 调度器重写 | `app/scheduler/` | task_scheduler.c、run_registry.c | V13 残余（g_eSysFlagManage） | `DONE`（Q1 定案 74d421e；契约 56ced13，修订 c6bcc4a；代码 e801caf；审计处置 6bfe3f4。E01 0 命中 / E02 无越界 / E03 200 PASS 0 FAIL＝185 基线+15 / E04 exit 0、0 诊断、scheduler.o 经 linkInfo.xml 确证进链。V13 残余本体仍待 T01 删旧文件时关闭） |
| UI01 | 菜单重写（含分问选择/参数表——大纲 P0-D） | `app/ui/menu/` | menu_core/menu_pages（冻结不删，T01 删除） | V14（替代面 UI01 建成，本体 T01 关闭——见 §13 裁定；拓扑保持 open） | `DONE`（契约 c05de1b，修订 1 2b54b8a→Menu_Init 改名 Menu_Setup；代码 e23176a；审计处置 82e6493。E01 0 命中 / E02 无越界 / E03 214 PASS 0 FAIL＝200 基线+14 / E04 exit 0、0 诊断、menu.o+menu_param.o 经 linkInfo.xml 确证进链、旧 menu_core.o 仍共链。arch-auditor 6/7 通过，1 建议级已删。V14 待 T01 删旧关闭。**r2 两级分类外壳 DONE（2026-07-18）：契约修订 2 冻结 71a0ec1；代码 cba97cb；拓扑同步 08d0424；见 §13.5 修订 2**。**W3 §23.1 RUN_ACTIVE 统一 RUNNING 横幅 DONE（2026-07-19）：429 PASS，本提交**） |
| M01 | 里程计+航向 unwrap（Middleware 纯算法：编码器 Δ→x,y,θ；imu.h 明示 unwrap 归此层） | `middleware/odometry/`（heading+odometry 双文件） | task1 姿态/里程零散逻辑（冻结不迁移，重建） | — | `DONE`（契约 §14 冻结 b856b23；代码 85d1e31；arch-auditor 三级无发现。E01 0 命中 / E02 无越界（.ccsproject 会话前既存，未纳入）/ E03 235 PASS 0 FAIL＝218 基线+17（heading 7+odometry 10）/ E04 exit 0、0 诊断、heading.o+odometry.o 经 linkInfo.xml 确证进链。IMU unwrap 权威 + 双文件拆分（用户 2026-07-18 裁定）；heading_sign/mm_per_pulse 单一所有者落定，V22 登记） |
| S06 | motion 语义运动服务（v1：直行 N mm/定角转/定点停；IMU 航向保持可插拔） | `app/service/motion/` | task1 直行/转弯编排（冻结不迁移，重建） | — | `DONE`（契约 §15 冻结 226f8fd；代码 e30c2a0；arch-auditor 6 项通过、1 建议级文档处置见 §15.5。E01 0 命中 / E02 无越界 / E03 253 PASS 0 FAIL＝235 基线+18 / E04 exit 0、0 诊断、motion.o 经 linkInfo.xml 确证进链（3 引用）。IMU 泵所有权 motion 激活期独占落定、里程计 total 差值一次性消费、V21 双泵第三泵送者、圆弧移出 v1→S06b） |
| S06b | motion 圆弧原语（定半径+定角，双轮速度比 + 航向误差修正） | `app/service/motion/`（S06 契约修订流程扩面） | — | — | `DONE`（契约 §19 冻结 8b030a5；代码 2aa2ba9；审计处置见 §19.6；拓扑同步见 §10。E01 0 命中 / E02 3 文件在范围 / E03 310 PASS 0 FAIL＝300 基线+10 / E04 exit 0、0 诊断、motion.o 经 linkInfo.xml 确证进链。arch-auditor 六轴通过、1 建议级已随代码修正。轮距 track_width_mm 新单一所有者落定 §19.0，仅前馈几何、非第二航向权威；用户 2026-07-18 裁定先于 S07——S07 `ARC` 段前置件） |
| M02 | 循迹元素检测（几何类别检测器：断线/横线/左岔/右岔，特征+连续置信计数+上升沿事件） | `middleware/track_elements/` | — | — | `DONE`（契约 §16 冻结 b71b59b；代码 cf745f8；arch-auditor 无阻断/无重要级，1 建议级文档处置见 §16.5。E01 0 命中 / E02 无越界 / E03 269 PASS 0 FAIL＝253 基线+16 / E04 exit 0、0 诊断、track_elements.o 经 linkInfo.xml 确证进链（3 引用）。位图并列消费者不采样（非 V21 双泵）、bit0_is_left 无第二反转、V24 登记） |
| M03 | speed_plan 速度规划（Middleware 纯算法：`\|error_mm\|` → 有状态斜坡基速，直道加速/入弯减速，自持输出限幅） | `middleware/speed_plan/` | — | — | `DONE`（契约 §17 冻结 61f4149；代码 8d84657；契约修订 1/审计处置 8975b2a；代码 fix 6ace23b；拓扑同步 3b92258。E01 0 命中 / E02 无越界 / E03 285 PASS 0 FAIL＝269 基线+16 / E04 exit 0、0 诊断、speed_plan.o 经 linkInfo.xml 确证进链。arch-auditor 无阻断/无重要级，2 建议级已处置（F1 删排序夹紧、F2 白名单更正）；基速调制单一所有者落定 speed_plan，V25 登记） |
| S02b | line_follow 深化：M02 元素事件面接入 + M03 速度调制接入（base_speed 合成点仍唯一在 line_follow_apply） | `app/service/line_follow/` | — | — | `DONE`（契约 §18 冻结 b3b2d38；代码 f278894；拓扑同步见 §10。E01 0 命中 / E02 4 文件在范围 / E03 300 PASS 0 FAIL＝285 基线+15（速度调制 5 + 元素事件面 10）/ E04 exit 0、0 诊断、line_follow.o 重编并经 linkInfo.xml 确证进链（speed_plan.o/track_elements.o 已在链）。arch-auditor 三维无发现；base_speed 合成点未搬家、位图并列消费不新开采样点、V21 不新增第四推进点） |
| S07 | route 分段路线执行服务（段表驱动：FOLLOW_UNTIL(元素)/STRAIGHT/TURN/ARC——新题=换段表） | `app/service/route/` | task1 分段状态机 | — | `DONE`（契约 §20 冻结 5eaa41f；代码 6cb338c；审计无发现；拓扑同步见 §10。E01 0 命中 / E02 无越界 / E03 334 PASS 0 FAIL＝310 基线+24 / E04 exit 0、0 诊断、route.o 经 linkInfo.xml 确证进链 3 引用。arch-auditor 六轴全过、亲验 motion.c IDLE/DONE drive-free；route 每拍≤1 子服务、不构成第四 Chassis_Update 推进点/第二 Imu_Update 排空点，V21 扩条/V23 登记；catch-up 防幻纠偏落定 §20.3） |
| SYS01 | 装配入口更新 | `app/system/` | sys_init.c 增量改造 | — | 随各阶段（**W2 §22.2 World-2 点亮 DONE**：main→Scheduler_Run，SpeedTune 条目接 tuning，旧 SysRun 停用，418 PASS。**W3 §23.3 app_compose 接入 EncoderTest/MotorDir 两 DEBUG 诊断条目 DONE（2026-07-19）：DEBUG 组三条目，429 PASS，本提交**。**W4 §24 app_compose 接入 GrayTest 12 路灰度数字量遥测条目 DONE（2026-07-19）：DEBUG 组四条目，434 PASS，本提交**。**W5 §25 动态调参框架 DONE（2026-07-19）：TUNE 参数组 + 片内 flash 持久化 + 循迹外环增益首参数集。PT1 param_store Driver（18fc9b4，442 PASS）+ PT2 param_tune Service+LineFollow_GetGains（bd9a67b，448 PASS）+ PT3 menu action 钩子+app_compose TUNE 接线（451 PASS，本提交）。板载 TUNE 组按钮调循迹外环增益、K3 SAVE 掉电 flash 保存；真实 flash 擦/写硬件边界待用户上板验证。**W6 §26 app_compose 接入 LineFollow 循迹运行条目 DONE（2026-07-19）：DEBUG 组第 5 条（idx4）——on_enter `LineFollow_Init` 归零→`ParamTune_Init` 重推持久增益（关闭 V28）→`Start`、on_step `Update` 级联 Chassis 沿线跑、on_exit `Stop` 安全停车；`s_lf_cfg` 保守 UNCALIBRATED 占位（低速起步/超时有界/element_mask=0），几何用建议式 recovery≈2.7×pitch；契约 §26 冻结 4aab90b，代码 b907003。E01 仅 app_compose.c 在范围 / E02 arch-scan 空输出 / E03 接线序 Init→ParamTune_Init→Start / E04 exit=Stop+保守配置 / E05 451 PASS 0 FAIL 无回归 / E06 exit 0、0 诊断、app_compose.o 进链 5 符号 .text 可达。arch-auditor 六红线全过、无发现放行。现场调参闭环成立：选它跑→TUNE 改增益 SAVE→再跑→看效果。标定量上板实测替换待用户自理。**W8 §30 GimbalTune 云台位置环静态调参条目 DONE（2026-07-21）：DEBUG 组第 7 条（idx6）——VOFA 命令模式（cmd×7 XP/XD/YP/YD/DB/MS/GO、tx×13 波形），gimbal 增 SetAimTuning/ReselectTopic/遥测扩两字段，tuning 增 GIMBAL_AIM profile+tuning_gimbal 子模块；进页 DB=10000 确定性零出力（floor-1 爬行事实下零增益不安全），退页确定性停；520 PASS，契约 2399390+修订 0a8c463，代码本提交；docs/云台位置环调参指南.md 交付**） |
| M04 | move_profile 距离参数化梯形速度剖面（Middleware 纯函数：已行进距离+目标+加减速→前馈基速，加速-匀速-减速自带位置反馈） | `middleware/move_profile/` | — | — | `DONE`（契约 §27 冻结 b98cf18，修订1 64fe6d5；代码 536883f；arch-auditor 契约逐条成立、无阻断/无重要、1 建议级文档处置；拓扑同步见 §10。E01 8 文件在范围 / E02 依赖纯净仅 `<math.h>`/`<stddef.h>` / E03 无关 / E04 467 PASS 0 FAIL＝451 基线+10（move_profile 单测）/ E05 exit 0、0 诊断、move_profile.o 经 linkInfo.xml `<input_file>` 进链、新符号零调用者 DCE 可达性待 T01。距离剖面纯函数，与 speed_plan（横向误差→基速）输入域不同非复刻；「定长运动速度剖面」新单一所有者落定，mm→m 仅量纲对齐非第二距离所有者） |
| S06c | motion 定长直行原语（`Motion_StartProfiledStraight`：move_profile 前馈 + 既有航向保持 PID，旧恒速 STRAIGHT 不改） | `app/service/motion/`（S06 契约修订流程扩面） | — | — | `DONE`（契约 §27 冻结 b98cf18；代码 536883f；arch-auditor 六项通过、1 建议级（base∓corr 终段 nuance）文档处置入 §27.4；拓扑同步见 §10。E01 8 文件在范围 / E02 无关 / E03 唯一新增 include=move_profile.h（Service→Middleware）/ E04 467 PASS 0 FAIL＝含 motion profiled 6 / E05 exit 0、0 诊断、motion.o 经 linkInfo.xml 进链、新符号零调用者 DCE 可达性待 T01。纵向按距离剖面无纵向 PID（用户 2026-07-19 裁定）；V21 推进点 3→4；旧 STRAIGHT/TURN/ARC 一字未改；过零换向所有者仍 motor.c 非新安全缺口） |
| MS02 | ProfiledStraight 运行条目 + DRIVE 参数组按钮调参持久化（motion 剖面 setter/getter + param_tune 扩 blob schema2 + app_compose 接 DEBUG 条目/DRIVE 组 + §8.1 防跑飞看门狗） | `app/service/motion/`+`app/service/param_tune/`+`app/system/` | — | — | `DONE`（契约 §28 冻结 4990b09，修订1 7c94c03；代码 7c891cb + 看门狗 f333333；arch-auditor 6 声明 5 成立/1 建议级已加 §8.1 看门狗处置；拓扑同步见 §10。E01 8 文件在范围 / E02 param_tune+app_compose 仅 +motion.h、arch-scan exit0 / E03 host 479 PASS 0 FAIL＝467+motion 7+param_tune 5 / E04 接线序 Init→ParamTune_Init→Start / E05 schema2 往返+旧blob退默认 / E06 exit0、0诊断、四.o进链、新符号进.map 可达零调用者解除。距离按钮可调默认1000mm、DRIVE 独立组、heading_hold=false、schema升2旧blob一次性失效。**§8.1 双看门狗归 motion**：profile_timeout_ticks 防物理过冲(修订1 f333333)、profile_stall_ticks 防堵转保护 TB6612(修订2 f73a8ea，用户关切)。mm_per_pulse 占位上板须标定；底盘速度环默认增益0需先调 SpeedTune 否则不动） |
| T01 | 赛题 Task（薄编排）+ 旧 tasks 整体删除 | `app/tasks/` | 全部旧 `tasks/**` | V03/V07/V13 残余/V15 全关，baseline 清空 | **最后**（赛题公布后） |

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
| Q6 | ~~S07 分段路线执行器的范围：段类型集合、段表由谁持有、主触发源~~ **已定案（S07 契约 §20，2026-07-18 用户三问裁定）**：① **段类型集合** = FOLLOW_UNTIL(元素事件)/STRAIGHT/TURN/ARC 四类，支持任意混合序；② **段表由装配层/T01 持有并经 `Route_Setup` 注入**（route 是纯执行机制，同 scheduler 条目表 / menu 分组表先例，换赛题=换段表 route 零改动）；③ **主触发源按段类型自然完成分派**——FOLLOW_UNTIL 以 `LineFollow_PollElementEvents` 元素上升沿事件为完成源，STRAIGHT/TURN/ARC 以 `Motion_IsDone`（motion 内部里程/航向判据）为完成源，无单一「里程 vs 元素」全局主触发；④ 段间**确定性交接**（隔拍刹停间隙 + 进 motion 段前 odometry catch-up）+ 每段**可选完成超时兜底**（承 S06 §15.5 deferred，超时/LOST/段启动被拒 → FAULT+确定性刹停）。 | S07 契约 §20 |
| Q7 | ~~视觉帧解析归属 + 帧格式二选一~~ **已定案（S05 契约 §21.0，2026-07-18 用户三轮裁定）**：照搬 Q2 先例——帧编解码归 Driver 新建 `driver/uart_vision`（吸收 vision_bus/vision_coord 帧职责，`vision_uart` 增 TX），坐标→轴映射归 Middleware（S05b），选题握手+收敛+前馈归 Service（S05c）。**协议非「二选一」而是两条并行链**：`0xAA 0x55`+len+payload+CRC16-MODBUS = 控制/坐标帧（视觉→主控 RX，运行期，坐标 float32）；`0xFF`+主任务+子任务+`0xFE`（无校验，4B）= 选题/握手帧（双向，setup 期）。运行期只收坐标、不混包、不要回馈帧。冻结 `vision_coord.c`（0x55AA+和校验+int16）判为过时，S05a 全重写不照搬。详见 §21.0。 | S05 契约 §21.0（closed） |
| Q8 | 双机协同（大纲 P2-B：双车蓝牙）与测距/避障（P3）：本仓库无对应硬件与 Driver，硬件未定案。登记为余力项，赛题/硬件明确后再立项，不预支协议设计。 | 赛题后 |
| Q9 | 声光提示（大纲 P0-D：buzzer/LED 时序）：Driver 库存无 buzzer；题目普遍要求声光。硬件确认引脚后补小 Driver + 上层包装（归属 hmi 扩展 or 独立，届时定）。 | 硬件定案后 |
| Q10 | PID 特性缺口（大纲 P0-B 要求积分分离/死区/微分先行；现库有增量/位置/双限幅/微分滤波）：**按需契约时补**，不预支——首个真实需求最可能出现在 S05 云台收敛环或 S06 航向环，届时以契约修订进 `middleware/pid`。**云台瞄准控制器设计意向（2026-07-19，用户）**：P+D、**不用 I**——步进 `cur_pulse` 累加即积分器（`vision_aim` 输出 delta_pulse 增量、调用方累加，`vision_aim.h:13,69`），静态目标已收敛到死区内零稳态误差，再加控制器 I = 双积分器、超调/积分饱和。**位置式 = 每拍从当前误差重算增量** `delta=Kp·e+Kd·Δe`，非增量式（不累加指令本身，否则叠 cur_pulse 累加成双积分）。三陷阱留契约：① 输出必须仍是**每拍脉冲增量加到 cur_pulse**，切勿把「位置式」做成「输出=步进绝对位置=Kp·e」（误差归零回中、且非 I 保不住位）；② D 作用于像素误差**必须滤波**（视觉噪声、裸微分放大抖动＝反防抖）——`middleware/pid` 已有位置式+微分滤波，**倾向复用**而非 vision_aim 手搓 Kd（免第二控制所有者）；③ PD 需逐轴 e(k−1)，与 vision_aim「纯函数不持状态」冲突，须定 D-state 归属。移动目标（车动瞄静物＝像素里目标在动、斜坡输入）P+D 为 type-1 有跟随滞后，消滞后用**前馈 yaw-rate** 非 I。全程零几何常量。 | 需求出现时（S05 契约修订） |
| Q12 | **灰度位序左右（bit0_is_left）的单一所有者**（2026-07-19，用户裁定，待测量输入）：车头左右到底对应灰度位图哪一端，是**一条板级接线事实**，不是每个 debug 条目各填各的。① **测量源**：用 DEBUG 组 `GrayTest`（12 路灰度数字量遥测）实测——人看哪个物理探头触发哪一路，读出 bit0 究竟在车头左还是右；**以实测为准**，撤销 W6 `s_lf_cfg.bit0_is_left=true` 的猜测占位。② **归属**：链接落在**一个公共层**（候选=`driver/gray` 板级方向事实 or 一处共享常量），**GrayTest 的通道标注与 LineFollow 的 `bit0_is_left` 同源引用它**，禁止在 debug 装配层给每个条目各写死左右。③ **时序**：硬件到、GrayTest 测出方向后用户告知，届时以实现契约冻结公共层所有者与真值，`s_lf_cfg` 该字段改为引用公共源。现在不预建公共层抽象（无第二消费者、无实测值，§8.3）。 | 公共层契约（GrayTest 测出后） |
| Q11 | 整车几何标定：压角点转弯的「灰度阵列中心→驱动轴纵向距离 `L`」及车身长宽今天**零消费者**，按 S07 裁定属 T01 段表常量，**现在不建 struct 字段**。标定规格已出 `agent/GEOMETRY_CALIBRATION.md`：灰度间距/轮距/脉冲距映射既有单一所有者字段（`pitch_mm` track_error.h:33 / `track_width_mm` motion.h:58 / `mm_per_pulse` odometry.h:35），实测填值；`L` 定为**已有原语组合** `FOLLOW_UNTIL(角)→STRAIGHT(L)→TURN(90°)`（非新 motion 原语），与车身几何登记待 T01 段表定案（题目角点几何决定 `L` 微调）。**纠正**：高精度距离判断归 `mm_per_pulse` 非轮距。**云台几何裁定（2026-07-19，用户澄清 + 实读 S05a/b/c 更正）**：分工无张力——上位机 owns 感知（只输出像素 xy 误差），MSPM0 owns 伺服（`vision_aim` 比例增益 kp：像素误差→脉冲增量，`gimbal` 驱动步进）。**kp 吸收像素→脉冲尺度，瞄准全程不需任何几何/单位常量**（`vision_aim.h:20` 明示全仓无「度→脉冲」所有者、不做单位换算）。故**云台几何一律不测不建**，撤回前一版 A5/A6/A7。唯一会引入几何+第二瞄准源的是 odometry 运动前馈——`gimbal.h:16` 已**故意不接线**（需有目标世界模型时才以契约修订补入），不接即天然单源。运动瞄准若不够稳准，杠杆全在 `vision_aim` 增益（现为纯比例 P，动目标有稳态滞后）或可选 yaw-rate 前馈（现成 IMU 角速度、零几何）。**更正**：前一版记的「甲/乙张力」是只读计划摘要脑补，实读 S05c 无此问题、无需裁定。 | 现状即定案，无需裁定 |

### 5.1 视觉组通信协议——已登记输入（2026-07-18，Q7 的输入，设计不预支）

视觉组交付协议原件 `docs/通信协议.md`。以下为**事实登记**（帧格式二选一与归属裁定留待 S05 契约，Q7）：

- **物理链路**：视觉端 = 香橙派 5pro，UART @ **230400 8N1**。本仓库对接口 = 既有
  `board_uart/vision_uart`（波特率/引脚需在 S05 契约与 `board.syscfg` 核对，**不在此预改**）。
- **方向与语义（主控视角）**：
  - 主控**接收**（视觉→主控）：`0x01` = 坐标，x、y 各 float32 小端（8B）；
    `0x02` = 目标状态，2 × 状态位域字节。
  - 主控**发送**（主控→视觉）：主任务号 + 子任务号；`0x01`/`0x02` = 执行、
    `0x03` = 复位（子任务 `0x02` = 确认复位）。
  - **消歧注意**：两方向复用 `0x01/0x02` 但语义不同，S05 契约须按方向区分，不得混用同一张表。
- **两种候选帧格式（视觉端二选一，待定案）**：
  - 包 1：`0xFF` + 主任务 + 子任务 + 数据 + `0xFE`（无校验）。
  - 包 2：`0xAA 0x55` + 长度(1B) + [主任务 + 子任务 + 数据] + CRC16-MODBUS
    （小端，校验范围 = 长度字节 + payload）。
- **归属仍按 Q7/Q2 先例不变**：帧解析归 Driver（吸收冻结的 vision_bus，或新建
  `driver/uart_vision`），坐标→角度/位姿映射归 Middleware，收敛/触发归 Service。
  选哪种帧格式、是否照搬先例、与冻结 vision_bus 及现有 vision_uart 波特率的差异——
  **全部 S05 契约冻结时定，此处不预支**。

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

## 12. 赛前能力预制编排（2026-07-18，依 `docs/电赛纯控制题_应用层框架开发plan.md` v2）

### 12.1 输入定位与层映射

- 该 docs 报告是**需求侧输入**（2021–2025 纯控制题统计、模块频次、命题规律）；
  本计划表仍是执行侧唯一权威，架构权威仍是 AGENTS.md。
- 层映射：报告 L1 HAL ≈ 本工程 Driver（phase2 已完成）、L2 算法 ≈ Middleware、
  L3 应用框架 ≈ App Service、L4 应用任务 ≈ App Task。
- **冲突裁定（两处，AGENTS.md 优先）**：①报告「L3→L1 必须经 L2」严于依赖矩阵
  （Service→Driver 允许）——从矩阵，不新增禁令；②报告主控写 G3507/STM32 双实现——
  本仓库目标是 G3519 单板，云台/视觉端协处理器代码**不在本仓库范围**（届时另立仓库或目录，
  经 UART 协议对接，协议归 S05 契约）。

### 12.2 差距核对表（报告模块 → 工程现状 → 处置）

| 报告模块 | 工程现状 | 缺口与处置 |
|---|---|---|
| P0-A HAL | Driver 层 phase2 全绿（encoder QEI/motor/gray 12 路/imu 串口单轴/emm42/oled/key/uart 群） | 无缺口；STM32 双实现范围外（§12.1） |
| P0-B PID | `middleware/pid`：增量/位置、双限幅、微分滤波、NaN 回退 | 积分分离/死区/微分先行按需补（Q10），不预支 |
| P0-C 底盘运动 | S01 速度环 DONE；imu Driver 已备（yaw+yaw_rate，unwrap 明示归 Middleware） | **缺里程计（M01）与语义运动接口（S06）**——报告 `chassis_straight/turn/arc` 的对应物 |
| P0-D 赛场生存 | S03 调参（VOFA≈报告的串口波形）、S04 hmi DONE | **缺菜单+参数表（UI01，下一项）**；声光 Driver 缺口（Q9） |
| P1-A 循迹框架四层 | 感知层（gray+track_error）与执行层（S02 外环+S01 内环）已有 | **缺元素识别层（M02）、速度规划（M03）、段级状态机（S07）**；S02 事件面经 S02b 契约修订补 |
| P1-B 云台框架 | Driver 侧齐备（emm42、vision_uart、imu 500Hz 前馈档）；25E 旧实现冻结在 platform_2d 可作参照 | **S05 整群缺**：坐标映射/收敛判定/搜索/轨迹发生/运动前馈；前馈输入=车端里程计→依赖 M01 |
| P2-A 视觉协议 | vision_uart 字节层已有；帧解析在冻结的 vision_bus | 归属按 Q2 先例，S05 契约定（Q7） |
| P2-B 双机协同 / P3 测距避障等 | 无硬件无 Driver | 余力项（Q8），不预支 |

### 12.3 编排依据（报告命题规律 → 排序决策）

1. 「赛题=循迹车×(云台/视觉/协同/避障) 模块重组」→ 能力模块在 T01 之前全部预制，
   T01 保持薄编排（用户既有裁定不变）；**S05 由『赛题明确后』改为赛前预制**——
   23E/25E 连续两届国赛本科大题含云台，等题=最大单块风险。
2. 「循迹形态扩展链 + 时间指标收紧」→ 循迹深化（M02 元素/M03 速度规划）频次最高
   （7/9 题），排在云台群之前。
3. 「行进间耦合任务（25E 发挥）」→ 前馈通道数据方向：M01 里程计 → S05 云台，
   决定 M01 必须先行。
4. 「封箱禁烧录」→ UI01 菜单+参数表是赛场生存件且阻塞已清，最先做；
   同时成为其后每个预制模块上板验证的入口（硬件验证用户自理不变）。
5. 报告里程碑 M1/M2「重构 24H/25E 进框架回归」在本工程的对应物：24H 场地（半圆+8 字）
   与 25E 正方形是 S06/S02b/S07 的验收参照场景；25E 云台打靶是 S05 的验收参照——
   各契约冻结时把可主机化的部分写进证据行，场地实测归用户。
6. 施工纪律不变：每模块仍走 embedded-closed-loop（契约冻结→TDD→证据→审计→拓扑），
   新 Middleware 归 §8.2 data-chain 检查域，S05/S06 涉电机步进归 §8.1 motor-safety 检查域。

## 13. UI01 契约（menu 菜单重写：分问选择 + 参数表）——冻结

- **task_id**: UI01-menu
- **goal**: 新建 `app/ui/menu/`：赛场生存用板载控制面板（大纲 P0-D「封箱禁烧录」）——
  在 hmi 面板与 scheduler 条目表之上的导航/选择/参数编辑外壳。两项能力：
  **分问选择**（RUN_LIST 列出 scheduler 条目，选中即 `Scheduler_EnterEntry`，激活期 BACK 停止）
  与**参数表**（PARAM_LIST/PARAM_EDIT，经调用者提供的整数取/设访问器就地调参，供无上位机连接的封箱现场）。
  匹配 `Scheduler` 的 `background_step` 钩子签名（`scheduler.h` 已明示「UI/菜单泵送位」），
  由 SYS01/T01 装配时注册——**UI01 只交付构件，零调用者是预期状态**（V07/S04 同款过渡态）。
- **V14 处置（裁定，2026-07-18）**：旧 `app/ui/oled/menu_core.*`/`menu_pages.*` **不删除、继续冻结**——
  其调用者 `task_groups.c:231/235`、`sys_init.c:72` 是冻结文件（§1/§15.4 禁触碰），删旧菜单会破坏构建。
  故 UI01 = 建成 V14 替代面（新 `app/ui/menu/**`，零调用者），旧菜单与新菜单**过渡期双实现登记拓扑**；
  V14 本体的删除与形式关闭随 T01 整体替换（同 V03/V07/V13 残余/V15 收尾）。计划表 §3「UI01 关闭 V14」
  应理解为「UI01 建成 V14 替代、T01 删旧关闭」；此裁定与 S04 §10「V14 本体在 UI01 关闭…UI01/T01 阶段删除、
  过渡期双实现、不强行接线」的操作意图一致，收工时向用户报告该措辞张力。
- **接口辩护**（菜单能做什么）：能列出并选择运行条目、能启动/停止一个条目、能列出并就地调整一组
  命名参数、能报告当前所处界面、能被周期泵送以消费输入与按需渲染。仅此成为公共面。

### 13.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/ui/menu/menu.h` / `.c` | 新建（公共面 + 顶层界面状态机 + RUN_LIST/RUN_ACTIVE + 泵送/渲染编排） |
| `hc-team/app/ui/menu/menu_param.h` / `.c` | 新建（参数表子视图：PARAM_LIST/PARAM_EDIT + 整数取设 + int32→dec 格式化，私有子模块） |
| `tests/host/test_menu.c` | 新建 |
| `tests/host/Makefile` | 追加 test_menu 目标/clean/.PHONY |
| `.gitignore` | 追加 test_menu / test_menu.exe |
| `Debug/makefile` | 登记 menu.o、menu_param.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 + 本契约 |

forbidden_files：`hc-team/app/ui/oled/**`（冻结旧菜单，零触碰）、`hc-team/app/service/**`
（含 hmi，只调用不修改）、`hc-team/app/scheduler/**`（只调用不修改）、
`hc-team/app/{tasks,system}/**`、`hc-team/driver/**`、`hc-team/middleware/**`、
tests/host 既有 `test_*.c` 与 `fake_*.c`（fake_board_gpio 已有按键电平/边沿注入面，
fake_i2c_port 已有 OLED 字节抓取面 + 自带可设定 `Clock_NowMs`）。

### 13.2 公共接口（最小面）

> **修订 2（2026-07-18）超越以下原始 §13.2 冻结面**：单级 `RUN_LIST`+`Params` 尾项升级为两级分类
> 外壳。以下代码块与状态机已按修订 2 更新为权威；变更理由见 §13.5 修订 2。

```c
typedef enum {
    MENU_SCREEN_GROUP_LIST = 0, /* L1 根界面：分类列表（DEBUG/TEST/PARAMS/TASK…） */
    MENU_SCREEN_RUN_LIST,       /* L2：某运行组的条目子列表 */
    MENU_SCREEN_RUN_ACTIVE,     /* 某条目激活中：菜单让出整屏显示，仅响应 BACK 停止 */
    MENU_SCREEN_PARAM_LIST,     /* L2：某参数组的参数表浏览 */
    MENU_SCREEN_PARAM_EDIT,     /* 单参数就地调整 */
} Menu_Screen;

/* 可调参数描述符（值存储/限幅归拥有 Service，菜单只经 get/set 读写）。 */
typedef struct {
    const char *name;            /* ASCII 参数名（显示用），不得为 NULL */
    int32_t   (*get)(void);      /* 读当前值（整数口径；单位/精度/限幅由拥有者定，菜单不复做） */
    void      (*set)(int32_t v); /* 写新值（经拥有它的 Service API 应用；限幅归拥有者） */
    int32_t     step;            /* 每次 UP/DOWN 的调整增量 */
} Menu_Param_T;

/* L1 分类的种类：运行条目组 or 参数组（互斥）。 */
typedef enum {
    MENU_GROUP_RUN = 0,  /* 运行条目组（DEBUG/TEST/TASK）：选中→条目子列表→EnterEntry */
    MENU_GROUP_PARAM,    /* 参数组（PARAMS，按钮动态调参）：选中→参数表 */
} Menu_GroupKind;

/* L1 分类描述符（由装配层 SYS01/T01 命名与填充；菜单只是其上的视图/导航）。 */
typedef struct {
    const char        *name;         /* ASCII 分类名（显示用），不得为 NULL */
    Menu_GroupKind      kind;
    const uint8_t      *entries;     /* kind==RUN：本组含的 scheduler 全局条目索引数组 */
    uint8_t             entry_count; /* kind==RUN：条目数（0 合法=空子列表） */
    const Menu_Param_T *params;      /* kind==PARAM：本组参数表 */
    uint8_t             param_count; /* kind==PARAM：参数个数（0 合法=空参数表） */
} Menu_Group_T;

void Menu_Setup(const Menu_Group_T *groups, uint8_t group_count);
    /* 复位导航状态为 GROUP_LIST + dirty；不触碰任何硬件/Service。groups==NULL 配 count=0
     * 合法（空菜单）；表（含各组 entries/params）生命周期由调用方保证覆盖使用期。
     * 名为 Setup 而非 Init：冻结旧 menu_core.c 仍导出 Menu_Init，双实现共链期符号冲突（修订 1）。 */
void Menu_Tick(uint32_t now_ms);
    /* 匹配 background_step 签名。每拍：① Hmi_Update()；② Hmi_PollInput() 取一语义事件 →
     * 依当前 screen 在 GROUP_LIST/RUN_LIST/RUN_ACTIVE/PARAM_* 间转移/编辑/切换 scheduler 条目；
     * ③ 非 RUN_ACTIVE 且 dirty 且 Hmi_IsDisplayReady() → 经 Hmi_PrintLine 渲染当前屏并清 dirty。
     * now_ms 预留匹配钩子签名，菜单事件驱动、不做时间门控（门控归 hmi/scheduler）。 */
Menu_Screen Menu_GetScreen(void);   /* 当前界面（查询/渲染/测试所需） */
```

- **界面状态机（两级，修订 2；转移表随 .c 注释交付）**：
  - GROUP_LIST（L1 根界面）：项 = 各分类名（`groups[i].name`，装配层提供 DEBUG/TEST/PARAMS/TASK…）。
    UP/DOWN 移光标（环绕），3 行可视窗口随光标滚动；ENTER 落在分类 i →
    kind==RUN：s_run_cursor 复位 0、→RUN_LIST；kind==PARAM：`MenuParam_Enter(groups[i].params, count)`、→PARAM_LIST。
    BACK/空表 → no-op（根界面无上级）。活动分类索引 = GROUP_LIST 光标（下探期不变）。
  - RUN_LIST（L2，作用域=活动组的条目子列表）：项 = `Scheduler_GetEntryName(g->entries[j])` 实时查询
    （**不缓存副本**）。UP/DOWN 移光标（环绕，同款滚动窗口）；ENTER 落在子列表位 j →
    `Scheduler_EnterEntry(g->entries[j])`（把子列表位映射为 scheduler 全局索引；成功则 →RUN_ACTIVE）；
    BACK → GROUP_LIST；空子列表 ENTER → no-op。
  - RUN_ACTIVE：**菜单不写任何 Hmi 行——整屏显示所有权完全归激活条目的 on_step**（避免双写者冲突，
    与 V21 双泵同构的显示所有权隔离）；仅 BACK → `Scheduler_LeaveEntry()` → →RUN_LIST（回本组子列表，
    置 dirty 重绘）；其余事件在激活期被菜单丢弃（当前无条目消费输入；如未来条目需输入，归 T01 再裁）。
  - PARAM_LIST：项 = 参数名（`MenuParam` 子视图持有活动组表指针）；UP/DOWN 移光标（环绕，同款窗口）；
    ENTER → PARAM_EDIT（聚焦该参数）；BACK → GROUP_LIST。
  - PARAM_EDIT：显示 name + 当前值（`get()` 经 int32→dec 格式化）；UP → `set(get()+step)`；
    DOWN → `set(get()-step)`；调整后回读 `get()` 重显（**菜单不存值副本、不限幅**——回读即反映拥有者限幅）；
    ENTER/BACK → PARAM_LIST。
  - **调参双通道隔离（用户裁定 2026-07-18）**：PARAM 组 = 按钮动态调参（本菜单唯一拥有）；VOFA 静态
    调参走独立平行链（§5.5 tuning/S03）——两者当前互不联通，菜单不接 VOFA、不复做限幅（单一所有者）。
- **单一所有者声明**：运行条目枚举与 enter/exit 转移序唯一在 `scheduler.c`（菜单只调 Enter/Leave/查询，
  不复算转移）；语义输入映射与行式显示唯一在 `hmi.c`（菜单只调 PollInput/Update/PrintLine/IsDisplayReady）；
  参数值存储与限幅唯一在调用者 accessor 背后的拥有 Service（菜单零值副本、零限幅）。
  本模块唯一拥有：**导航界面状态机 + 光标/滚动窗口 + 参数编辑焦点 + int32→dec 显示格式化**。
- **头不暴露 Driver 类型**（§3.4，同 hmi/scheduler）：公共面只用 `Menu_Screen`/`Menu_Param_T`/`Hmi_Input`
  间接（`Hmi_Input` 是 Service 类型，合法）；不出现 `Key_Id_e`（正是 V14 要消除的暴露）。
- **前置条件**：SYS01/T01 装配层在 `Menu_Init` 前完成 `Scheduler_Init`（条目表已登记）、`Hmi_Init`，
  并在主循环把 `Menu_Tick` 注册为 `Scheduler` 背景钩子（或直接周期调用）；参数表由装配层提供，
  其 accessor 内部调各拥有 Service 的公共 API。UI01 不做此接线（零调用者预期）。

### 13.3 preserved_behavior

- `app/ui/oled/**`（冻结旧菜单）、`app/service/**`、`app/scheduler/**`、其余 `app/**`、
  `driver/**`、`middleware/**` 零改动；主机非菜单 200 用例零改动全过（菜单 14 旧例被两级
  新例替换，修订 2）；固件行为不变（menu.o/menu_param.o 进链接但仍零调用者）。

### 13.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `driver/\|middleware/\|app/tasks/\|app/system/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/ui/menu`，`#include` 行） | 0 命中（`app/service/hmi/hmi.h` 与 `app/scheduler/scheduler.h` 是合法同层/Service 依赖，不在告警集） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §13.1 | 无 allowed_files 之外的改动（尤其零触碰 `app/ui/oled/**`） |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥216 PASS / 0 FAIL（200 非菜单基线 + ≥16 两级菜单用例，替换旧 14 例），链接组成同旧：真实 `menu.c`+`menu_param.c`+`hmi.c`+`key.c`+`oled_hardware_i2c.c`+`scheduler.c` + `fake_board_gpio.c` + `fake_i2c_port.c`（分组表/条目表/参数 accessor 在 test_menu.c 内以 spy 定义）。必含：Init 静默（零 I2C 事务、零按键读）+ 初始 screen=GROUP_LIST；未就绪 Tick 不绘制且保持 dirty、就绪后首拍渲染 GROUP_LIST；无输入多拍不重绘；GROUP_LIST 上/下环绕 + 分类数>3 时滚动窗口保持光标可视；ENTER RUN 组→RUN_LIST；**RUN_LIST 子列表位→scheduler 全局索引映射正确**（如 TEST 组第 2 项 ENTER→`GetActiveEntry` 命中对应全局条目）+ screen=RUN_ACTIVE；RUN_ACTIVE 期菜单零绘制（多拍 I2C 事务计数不变）；BACK 自 RUN_ACTIVE→回本组 RUN_LIST（非 GROUP_LIST）+ 活动置 NONE + 重绘；BACK 自 RUN_LIST→GROUP_LIST；ENTER PARAM 组→PARAM_LIST；PARAM_EDIT UP=`set(get()+step)`/DOWN=`set(get()-step)` 回读重显；菜单不存值副本（外部改背后变量→下次读 live）、菜单不限幅（越界 set 由 accessor 反映）；BACK 自 PARAM_LIST→GROUP_LIST、自 EDIT→PARAM_LIST；int32→dec 含负值/零/多位/INT32_MIN；空分组表 Tick 不崩且各键安全；空条目 RUN 组 ENTER no-op→BACK 回 GROUP_LIST |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、menu.o 与 menu_param.o 经 linkInfo.xml 确证进入 .out 链接 |

### 13.5 契约修订记录

- **修订 1（2026-07-18，本提交，施工中发现——单独提交，先于满足它的代码）**：§13.2 公共接口
  `Menu_Init` 改名为 **`Menu_Setup`**。原因：E04 固件链接实测 `error #10056: symbol "Menu_Init"
  redefined`——冻结旧 `app/ui/oled/menu_core.c` 仍导出同名 `Menu_Init`，且其调用者
  `task_groups.c:231`/`sys_init.c:72` 是冻结文件（禁触碰），旧 `menu_core.o` 必须留在
  ORDERED_OBJS 中，双实现共链期两个 `Menu_Init` 符号冲突。核对旧 menu_core 导出面
  （Menu_Init/Menu_HandleKey/Menu_RenderIfDirty/Menu_SetCurrentPage/Menu_RequestRedraw/
  Menu_IsDirty/Menu_GetCurrentPage）与新面（Menu_Setup/Menu_Tick/Menu_GetScreen）：**仅
  Menu_Init 冲突**，故只改此一名，Menu_Tick/Menu_GetScreen/MenuParam_* 保持不变。E03/E04
  预期数值不变（仅符号名变更）。T01 删除旧 menu_core 后，本名可保留或由 T01 定夺，不预支。

- **修订 2（2026-07-18，用户裁定后重构——单独提交，先于满足它的代码）**：分问选择从**单级**
  （一个 `RUN_LIST` = 全部 scheduler 条目 + `Params` 尾项）升级为**两级分类外壳**。
  1. **动因（用户原话要点）**：菜单终点构思是固定四类一级菜单 `DEBUG/TEST/PARAMS/TASK`，各自带
     独立二级子菜单；「静态调参用 VOFA、动态调参用按钮」——PARAMS＝按钮通道，TEST 运行态＝VOFA 通道。
     原单级外壳无法表达「四类分组 → 各自子列表」的层级，是真实机制缺口。
  2. **用户已定范围（本轮问答）**：① **只做两级外壳**——不建 DEBUG/TEST/TASK 的具体台架/控制环条目
     （其 Service 多未建成，随各自 Service 到位，符合 Service 先行/T01 最后/零调用者预期）；
     ② **分类由装配层命名的通用分组**——外壳只认 N 个 `Menu_Group_T`，`DEBUG/TEST/PARAMS/TASK`
     名字由 SYS01/T01 填，UI 机制不硬编码赛题策略（延续 §13「内容归装配层、外壳只是机制」）。
  3. **接口变更**：`Menu_Setup(const Menu_Param_T*, uint8_t)` → `Menu_Setup(const Menu_Group_T*, uint8_t)`；
     新增公共类型 `Menu_GroupKind`（RUN/PARAM）、`Menu_Group_T`（name+kind+entries/entry_count+params/param_count）；
     `Menu_Screen` 增 `MENU_SCREEN_GROUP_LIST=0`（新根界面，枚举值重排——零外部调用者，无影响）。
     `Menu_Tick`/`Menu_GetScreen`/`Menu_Param_T` 签名不变。私有子模块 `menu_param`：`MenuParam_Init(params,count)`
     并入 `MenuParam_Enter(const Menu_Param_T*, uint8_t)`（进 PARAM 组时绑表+复位），`MenuParam_Handle` 的
     BACK 返回值由 `MENU_SCREEN_RUN_LIST` 改为 `MENU_SCREEN_GROUP_LIST`。
  4. **RUN 组条目索引语义**：`Menu_Group_T.entries[]` 是 scheduler **全局**条目索引数组，子列表位 j →
     `Scheduler_EnterEntry(entries[j])`。scheduler 仍是运行条目与 enter/exit 转移的唯一所有者，菜单只是视图。
  5. **allowed_files 不变**（menu.{h,c}/menu_param.{h,c}/test_menu.c/plan）；**Debug/makefile 不触碰**
     （无新增 object，menu.o/menu_param.o 已登记）；`.gitignore` 不变。E01/E02/E04 判据不变；
     E03 基线数学改为「200 非菜单 + ≥16 两级用例」（见修订后 §13.4），旧 14 菜单例被替换。
  6. **零调用者仍是预期**：新面无真实调用者（装配在 T01）；V14/V21/V10 关闭条件不变（本轮不挂 line_follow/
     tuning 钩子，双泵仍靠 scheduler 单活动条目不变量结构性排除）。三层参数中文子分类（速度环/循迹/云台）
     属**装配层内容或未来三级**，两级外壳不承载（用户选「只做两级」）。
  7. **审计处置（2026-07-18，arch-auditor 1 建议级 finding，含代码修订）**：`run_entry_name_of`
     把 `Scheduler_GetEntryName` 的**返回值**喂给共享 `render_item`，而 scheduler 契约文档化该
     返回值「越界返回 NULL」——`entries[]` 与 scheduler 登记表是两张独立维护的表，失步时
     索引越界→NULL→硬件 HardFault，且旧注释「条目名契约非 NULL」会误导修复者。处置采纳
     审计方案 1：在 `run_entry_name_of` 这**唯一**可空边界把 NULL 收敛为占位串 `"?"`
     （scheduler 仍是名字所有者，menu 不复算），并更正 `render_item` 注释区分「结构体字段名
     契约非 NULL」与「scheduler 返回值边界收敛」。E03 追加 1 必含用例
     `test_run_list_tolerates_stale_entry_index`（越界 entries 渲染不解引用 NULL），
     总数相应 ≥217（200 非菜单 + 18 菜单）。其余审计点（依赖矩阵/单一所有者/ISR/边界/数据链）无发现。

## 14. M01 契约（odometry 里程计 + 航向 unwrap）——冻结

- **task_id**: M01-odometry
- **goal**: 新建 `middleware/odometry/`：Middleware 纯算法，把编码器增量与 IMU 航向融成底盘平面
  位姿。两个同层子模块：`heading.{h,c}` = IMU yaw 去卷（[-180,180) → 连续多圈角，`imu.h:12`
  明示 unwrap 归本层的落点，**unwrap 唯一所有者**）；`odometry.{h,c}` = 位姿 dead-reckoning
  （前进距离 × 航向积分 x,y，内嵌 `Heading_T`）。成为「编码器 Δ + IMU 航向 → x,y,θ」这条新链的
  唯一算法所有者，为后续 S06 语义运动与 S05 云台运动前馈供位姿源（§12 行进间耦合前馈通道）。
- **航向来源裁定（用户确认 2026-07-18，选 IMU unwrap 为权威）**：位姿 θ = IMU yaw 去卷 × 符号
  修正；前进距离 = (ΔL+ΔR)/2 × mm_per_pulse 沿该航向积分。**不用轮差分航向**（抗轮滑、免 track_width
  这一无实测机械常数），IMU 无效拍保持上次航向续走。
- **文件拆分裁定（用户确认 2026-07-18）**：heading + odometry 双文件双主机测试（unwrap 跨界边界
  独立验证 + 沿用 S02 line_follow/lost_line 拆分先例 + §3「契约时定拆分」）。
- **接口辩护**（里程计能做什么）：底盘能报告自己的连续航向角、能报告平面位置 (x,y)、能被喂
  「一拍编码器增量 + 一拍 IMU 航向」推进位姿、能复位到原点。仅此成为公共面。

### 14.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/middleware/odometry/heading.h` / `.c` | 新建 |
| `hc-team/middleware/odometry/odometry.h` / `.c` | 新建 |
| `tests/host/test_heading.c` / `test_odometry.c` | 新建 |
| `tests/host/Makefile` | 追加两 target/run/clean/.PHONY |
| `.gitignore` | 追加两测试产物 |
| `Debug/makefile` | 登记 heading.o、odometry.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 + 本契约 |

forbidden_files：`hc-team/driver/**`、`hc-team/app/**`、`hc-team/middleware/{pid,track_error}/**`、
tests/host 既有 `test_*.c` 与 `fake_*.c`。（Debug/ 下本地生成物 `subdir_vars.mk`/`sources.mk`/
`ccsObjs.opt` 是本地补列不入库，不列入 allowed。）

### 14.2 公共接口（最小面）

```c
/* ---- heading.h：IMU yaw 去卷（唯一所有者） ---- */
typedef struct {                 /* 字段全私有，调用者不得读写 */
    float   last_wrapped_deg;    /* 上一有效样本 */
    int32_t wrap_count;          /* 累计跨界圈数 */
    bool    seeded;              /* 是否已收首样本 */
} Heading_T;

void  Heading_Reset(Heading_T *ctx);                       /* 清零，seeded=false */
float Heading_Unwrap(Heading_T *ctx, float yaw_wrapped_deg);
    /* 首样本：seed（last=yaw, wrap=0）并原值返回。后续：delta = yaw − last；
     * delta < −180 → wrap_count++；delta > 180 → wrap_count−−；last = yaw；
     * 返回 yaw + wrap_count×360。ctx==NULL 返回传入值（无副作用）。
     * 假设 |连续有效样本间 yaw 变化| < 180°（航向 Nyquist；200/500Hz IMU 下
     * 任何现实 yaw rate 成立）；掉线 gap 期转向 >180° 会误计（dead-reckoning 固有）。 */

/* ---- odometry.h：位姿 dead-reckoning ---- */
typedef struct {
    float mm_per_pulse;   /* 脉冲→距离换算，>0 机械安装事实，无默认值，实测标定 */
    float heading_sign;   /* IMU yaw 符号修正唯一点（imu.h:11），+1/−1，默认 +1 不预设 */
} Odometry_Config_T;
typedef struct { float x_mm, y_mm, heading_deg; } Odometry_Pose_T;
typedef struct {          /* cfg 之外字段全私有 */
    Odometry_Config_T cfg;
    Heading_T heading;    /* 内嵌 unwrap 状态 */
    float x_mm, y_mm, heading_deg;
} Odometry_T;

void Odometry_Init(Odometry_T *ctx, const Odometry_Config_T *cfg);
    /* 拷 cfg（按值）+ 清位姿（x/y/heading=0）+ Heading_Reset。cfg==NULL 时 cfg 归零。 */
void Odometry_Reset(Odometry_T *ctx);          /* 清位姿 + Heading_Reset，保留 cfg */
void Odometry_Update(Odometry_T *ctx, int32_t delta_left_pulses,
                     int32_t delta_right_pulses, float yaw_wrapped_deg, bool heading_valid);
    /* ① heading_valid → heading_deg = Heading_Unwrap(&heading, yaw_wrapped_deg) × cfg.heading_sign；
     *    否则保持上次 heading_deg（不推进 unwrap 状态）。
     * ② fwd_mm = (delta_left + delta_right) × 0.5 × cfg.mm_per_pulse。
     * ③ x_mm += fwd_mm × cos(heading_deg·π/180)；y_mm += fwd_mm × sin(...)。
     * ctx==NULL 无副作用返回。不含时间门控（空间积分，不需 elapsed_ms）。 */
void Odometry_GetPose(const Odometry_T *ctx, Odometry_Pose_T *out);  /* out/ctx==NULL 无副作用 */
```

- **单位链**：`delta_pulses` 有符号增量脉冲（`encoder.c` 已方向修正，V06）；`yaw_deg` 度、器件
  已 Kalman 解算未 unwrap；`mm_per_pulse` mm/脉冲（实测）；输出 x/y 毫米、heading 度连续。
- **单一所有者声明**：编码器方向反转归 `encoder.c s_direction_sign`（M01 收到已修正 delta，不复反转）；
  IMU yaw 符号修正**唯一点** = `cfg.heading_sign`（答 imu.h:11 强制单点，别处无第二开关）；
  脉冲→距离换算**唯一点** = `cfg.mm_per_pulse`（新变换，不碰 `speed_mps` 速度链）；yaw unwrap
  **唯一点** = `heading.c`（imu.h:12 指定本层，≠ 二次滤波，无损提升，不再对 yaw 滤波/积分）；
  采样与 elapsed 所有权归 `chassis.c`（M01 按值收参，永不调 Encoder_Update/GetSnapshot/Imu_*）。
- **头不含 Driver 类型**（§3.3）：公共面只用自持类型与标量；不 `#include` encoder.h/imu.h，
  编码器增量与 yaw 按字段值传入（同 track_error「位图按值传入」范式）。
- **前置条件**：调用方（未来 S06/装配层）每拍读 `Encoder_GetSnapshot()`+`Imu_GetSnapshot()`，把
  `delta_pulses[L/R]`/`yaw_deg`/`valid` 作参数喂入；运行起点建议 `Imu_ZeroYaw()` 使首航向≈0。
  M01 不采样、不推进 Encoder（唯一状态推进点仍是 Chassis，多处 GetSnapshot 只读复制不构成双采样）。

### 14.3 preserved_behavior

- `middleware/{pid,track_error}`、`driver/**`、`app/**` 零改动；主机既有 218 用例全过；
  固件行为不变（heading.o/odometry.o 进链接但零调用者——S06/S05 未建，V07 同款过渡态）。

### 14.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `driver/\|app/\|middleware/pid/\|middleware/track_error/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/middleware/odometry`，`#include` 行） | 0 命中（同模块 `heading.h`、`<math.h>` 不在告警集） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §14.1 | 无 allowed_files 之外的改动 |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥232 PASS / 0 FAIL（218 基线 + ≥14 新用例）。必含 —— **heading**：首样本 seed 原值返回；CCW 跨界 +179→−179 连续角单调越 +180；CW 跨界越 −180；多圈累计（连续同向多次跨界）；微小 delta 不改 wrap；Reset 清零后重新 seed；ctx==NULL 返回原值。**odometry**：Init/Reset 清位姿；θ=0 直行 x 增 y≈0；θ=90° 走 +y；mm_per_pulse 尺度正确；heading_sign=−1 翻转转向感；`heading_valid=false` 保持上次航向续走且无 NaN；负增量倒退 x 减；跨 wrap 曲线路径位姿有限且合理；GetPose/ctx NULL 安全 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、heading.o 与 odometry.o 经 linkInfo.xml 确证进入 .out 链接 |

- **主机测试链接组成**（事实登记）：test_heading = 真实 `heading.c` + `test_heading.c`（纯逻辑，
  仅 `-lm`）；test_odometry = 真实 `odometry.c` + 真实 `heading.c` + `test_odometry.c`（`-lm`）。
  不链接任何 fake（M01 零端口依赖）。

### 14.5 契约修订记录

- （冻结初版，无修订。）

## 15. S06 契约（motion 语义运动服务 v1）——冻结

- **task_id**: S06-motion
- **goal**: 新建 `app/service/motion/`：语义运动服务，把「走直线 N mm（可选 IMU 航向保持）、
  原地转到相对角度、随时确定性停车」这三条底盘语义动作，建成在 chassis 速度内环（S01）与
  odometry 位姿源（M01）之上的**非阻塞状态机**。吸收旧 `task1.c` 的直行/原地转编排语义
  （旧文件冻结不迁移，按新数据链重建；task1 用裸 Gz 积分判角，本服务改用 odometry 去卷连续航向）。
  成为「pose + 语义目标 → chassis 速度目标」这条链的唯一编排所有者，为后续 S07 段路线执行与
  S05 云台运动前馈提供语义运动基元。**圆弧原语移出 v1（用户 2026-07-18 裁定，→S06b 单独契约）**。
- **接口辩护**（底盘能做什么）：底盘能走一段指定前进距离后停（可选按 IMU 航向纠偏保持直线）、
  能原地转到一个相对角度后停、能随时确定性停止、能报告当前运动是否完成与位姿/状态遥测。
  仅此成为公共面。
- **IMU 泵所有权裁定（用户 2026-07-18 确认，选「motion 激活期独占」）**：新 Service 层此前无人
  调 `Imu_Update()`（IMU FIFO 排空节奏无所有者）。本服务成为 `Imu_Update()` 的所有者——
  `Motion_Update()` 每次调用先 `Imu_Update()` 排空并刷新快照，再 `Imu_GetSnapshot()` 读值。
  类比 chassis 独占 `Encoder_Update()`；单活动条目不变量保证 motion 与 line_follow/tuning
  不并发，故不存在第二个 `Imu_Update` 所有者。（登记为新增所有权，收工时进拓扑 §6/§7。）

### 15.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/motion/motion.h` / `.c` | 新建 |
| `tests/host/test_motion.c` | 新建 |
| `tests/host/Makefile` | 追加 test_motion 目标/clean/.PHONY |
| `.gitignore` | 追加 test_motion / test_motion.exe |
| `Debug/makefile` | 登记 motion.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 + 本契约 |

forbidden_files：`hc-team/app/service/{chassis,line_follow,tuning,hmi}/**`（chassis 只调用不修改，
其余零触碰）、`hc-team/app/{tasks,scheduler,system,ui}/**`、`hc-team/driver/**`（Encoder/Imu 只经
公共 `*_GetSnapshot`/`Imu_Update` 调用，零改动）、`hc-team/middleware/**`（odometry/pid 只调用不改）、
tests/host 既有 `test_*.c` 与 `fake_*.c`（fake_board_gpio 已有编码器原始计数注入面、
fake_uart_port 已有 IMU 帧注入面、fake_clock 已有时间注入面、fake_motor_hw 已有电机抓取面）。

### 15.2 公共接口（最小面）

```c
typedef enum {
    MOTION_IDLE = 0,   /* 无原语；底盘静默（不泵内环，刹车真值表保持） */
    MOTION_STRAIGHT,   /* 直行中 */
    MOTION_TURN,       /* 原地转中 */
    MOTION_DONE,       /* 原语完成；已 Chassis_Stop，底盘静默（同 IDLE 语义，确定性驻停） */
} Motion_State;

typedef struct {
    /* 里程计标定（透传给 odometry；脉冲→距离、yaw 符号的单一所有者仍是 Odometry_Config） */
    float mm_per_pulse;        /* >0，实测标定 */
    float heading_sign;        /* +1/−1，实测标定 */
    /* 运动基速 */
    float straight_speed_mps;  /* 直行基速（前进为正） */
    float turn_speed_mps;      /* 原地转单轮速度幅值上限（>0） */
    /* 直行航向保持外环（位置式 PID：输入航向误差 deg → 输出差速修正 m/s） */
    float hold_kp, hold_ki, hold_kd;
    float hold_diff_limit_mps; /* 纠偏差速对称限幅 = 该 PID out_limit（限幅唯一所有者 = 此 cfg） */
    /* 定角转（比例控制 deg→m/s） */
    float turn_kp;
    /* 到位判据 */
    float straight_tol_mm;     /* 直行到位容差（>0） */
    float turn_tol_deg;        /* 转角到位容差（>0） */
} Motion_Config_T;

typedef struct {
    Motion_State state;
    float x_mm, y_mm, heading_deg;  /* 当前 odometry 位姿快照 */
    float target;                   /* 当前原语目标：STRAIGHT=距离 mm；TURN=相对角 deg；否则 0 */
    float progress;                 /* 已完成量：STRAIGHT=已行进 mm；TURN=已转过 deg；否则 0 */
} Motion_Telemetry_T;

void Motion_Init(const Motion_Config_T *cfg);
    /* Odometry_Init(cfg 透传) + 航向保持 PID Init + state=IDLE + last_total 待首拍同步标志置位。
     * 不发电机命令、不采样、不调 Encoder_Update/Imu_Update。cfg==NULL 视为误用（同 pid/odometry
     * 契约口径，不做运行期拒绝）。前置：装配层已 Chassis_Init 且已设 Chassis 速度环增益
     * （否则底盘不出力、目标不被跟踪——底盘调参非 motion 职责）。 */

bool Motion_StartStraight(float distance_mm, bool heading_hold);
    /* distance_mm<=0 → 返回 false，保持当前态。否则：捕获当前位姿为起点参考、清航向保持 PID 史、
     * 记录 heading_hold 开关、state=STRAIGHT、返回 true。 */

bool Motion_StartTurn(float relative_deg);
    /* relative_deg==0 → 返回 false。否则：捕获当前 heading 为基准、记录目标相对角、state=TURN、
     * 返回 true。符号约定：+ = CCW（左转，odometry 航向递增方向），− = CW（右转）。 */

void Motion_Update(void);
    /* 每次调用（任意态，无自门控——事件驱动）：
     *  ① Imu_Update()（motion 独占，排空 FIFO 刷新快照）；
     *  ② Encoder_GetSnapshot() + Imu_GetSnapshot()（只读复制，不推进 Encoder，不构成双采样）；
     *  ③ 首拍同步 last_total←total（不产生位移）；此后 dΔ = total − last_total、last_total←total，
     *     以 dΔ[L/R]（total_pulses 差值，非 delta_pulses 字段）+ yaw + valid 推进 Odometry_Update；
     *  ④ Odometry_GetPose() 取当前位姿；
     *  ⑤ 依 state：
     *     STRAIGHT：dist=hypot(x−x0,y−y0)；dist≥target → Chassis_Stop + state=DONE；否则
     *       left=base∓corr、right=base±corr（heading_hold 且本拍 IMU 有效时 corr=位置式 PID(0, rel)，
     *       rel=heading−heading0；heading_hold 关或 IMU 无效 → corr=0 直行开环，不在陈旧航向上纠偏）
     *       → Chassis_SetTargetMps；
     *     TURN：rel=heading−heading0；err=target−rel；|err|≤turn_tol_deg → Chassis_Stop + state=DONE；
     *       否则 cmd=clamp(turn_kp·err, −turn_speed, +turn_speed)、left=−cmd、right=+cmd
     *       → Chassis_SetTargetMps；
     *     IDLE/DONE：不设新目标；
     *  ⑥ STRAIGHT/TURN 末尾恒推进 Chassis_Update()（内环自门控 10ms，S02 同款级联）；
     *     IDLE/DONE **完全静默**——不泵 Chassis_Update（刹车真值表保持，确定性驻停，
     *     S02 修订 1 / tuning Exit 同款显示/驱动所有权隔离）。 */

void Motion_Stop(void);
    /* 任意态 → Chassis_Stop + state=IDLE。随时可从正常控制流调用的确定性停止（§8.1）。 */

Motion_State Motion_GetState(void);
bool Motion_IsDone(void);                 /* state==MOTION_DONE */
void Motion_GetTelemetry(Motion_Telemetry_T *out);  /* out==NULL 无副作用 */
```

- **数据链（§8.2 登记，单位与所有者）**：
  `BoardGpio 原始计数 → Encoder_Update(chassis 拥有,10ms) → Encoder_Snapshot.total_pulses[有符号累计脉冲]`；
  motion 读 `total_pulses`，以「本服务持有的 last_total 差值」得**恰好消费一次**的增量脉冲
  （不用 `delta_pulses` 字段——那是 chassis 速度环经 `speed_mps` 消费的口径；motion 用 total 差值
  是对累计里程计只读一次消费，**非第二份 delta 计算**）→ `Odometry_Update` → `Odometry_Pose_T`
  [x_mm,y_mm 毫米、heading_deg 连续度] → motion 语义误差（距离 mm / 相对角 deg）→ 控制律 →
  `Chassis_SetTargetMps` [m/s]。IMU 链：`ImuUart FIFO → Imu_Update(motion 拥有) → Imu_Snapshot
  [yaw_deg∈[-180,180), valid, age_ms]` → 按值喂 `Odometry_Update`。
- **单一所有者声明**（motion 一律不复做）：编码器采样节奏/elapsed = chassis.c；输出限幅/slew/
  换向过零+死区/命令超时归零/刹车真值表 = motor.c（经 chassis）；脉冲→距离换算 = `Odometry_Config.
  mm_per_pulse`；IMU yaw 符号 = `Odometry_Config.heading_sign`；yaw unwrap = heading.c；
  底盘目标限幅 = chassis 既有空缺（motion 不抢先补——见 chassis.h 注释）；底盘速度环增益 =
  装配层/tuning。motion **唯一拥有**：IMU FIFO 排空节奏（新增）、里程计消费节奏（last_total 一次性
  消费）、语义运动状态机 + 到位判据 + 航向保持外环 PID + 定角转比例律 + 起点/基准参考捕获。
- **头不暴露 Driver 类型**（§3.4）：公共面只用自持枚举/结构与标量；不出现 `Encoder_*`/`Imu_*`/
  `Motor_*`/`Chassis_Side` 类型。motion.c 依赖矩阵合法：调 Driver（Encoder/Imu 只读快照 + Imu_Update）、
  Middleware（odometry/pid）、同层 Service（chassis）——均为允许边。
- **确定性停止与安全态（§8.1）**：Init 不发电机命令（初始安全）；到位/Stop 走 `Chassis_Stop`
  （刹车 + 目标清零 + PID 复位）后转 IDLE/DONE 静默，刹车真值表保持；motion 若中途停止被泵
  （不再调 Motion_Update），chassis 停泵 → motor 100ms 命令超时归零兜底（Driver 既有保护）。
  motion 不在陈旧 IMU 航向上纠偏（valid=false → corr=0）。无新增功率路径，全部经 chassis 下发。
- **V21 双泵**：motion 是 `Chassis_Update()` 第三个泵送者（现有 line_follow、tuning），落进同一
  缓解机制——单活动条目不变量（scheduler `EnterEntry/LeaveEntry` 互斥）；motion 的启停挂未来
  scheduler 条目 `on_enter/on_exit`，不与 line_follow/tuning 同时驱动。收工时 topo-updater 把
  motion 追加进 V21 条目文本。

### 15.3 preserved_behavior

- `app/service/{chassis,line_follow,tuning,hmi}/**`、旧 `app/**`、`driver/**`、`middleware/**` 零改动；
  主机既有 235 用例全过；固件行为不变（motion.o 进链接但零调用者——Task 未写，V07 同款过渡态）。

### 15.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/\|app/scheduler/\|app/ui/\|app/system/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/service/motion`，`#include` 行） | 0 命中（`chassis.h` 同层 Service、`odometry.h`/`pid.h` Middleware、`encoder.h`/`imu.h` Driver 均为矩阵允许边，不在告警集） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §15.1 | 无 allowed_files 之外的改动 |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥249 PASS / 0 FAIL（235 基线 + ≥14 新用例）。必含安全/行为项：Init 静默（零电机命令、零 Encoder_Update/Imu_Update）+ state=IDLE + IsDone=false；StartStraight(d≤0)→false 保持、(d>0)→STRAIGHT；注入编码器增量→pose 前进、dist≥target→Chassis_Stop(BrakeAll)+DONE+IsDone；DONE/IDLE 静默（多拍 Update 不泵内环、刹车真值表保持、无新电机命令刷新）；直行航向保持 ON + 注入偏航→差速纠偏方向正确（rel>0 CCW 漂移→左快右慢 CW 修正）；IMU 本拍无效→corr=0（不在陈旧航向纠偏、仍按编码器测距前进）；StartTurn(0)→false、(+deg)→TURN 且原地转（左负右正=CCW）、(−deg)→反向、到 turn_tol_deg→Chassis_Stop+DONE；里程计恰好消费一次（两次连续 Update 间 total 不变→pose 不二次前进，无双计数）；Motion_Stop 任意态→Chassis_Stop+IDLE；Imu_Update 独占路径（注入 IMU 帧经真实 imu.c 解析后 valid 翻真喂入 odometry）；遥测 state/pose/target/progress 一致 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、motion.o 经 linkInfo.xml 确证进入 .out 链接 |

- **主机测试链接组成**（事实登记）：test_motion = 真实 `motion.c` + 真实 `chassis.c` + 真实
  `odometry.c` + 真实 `heading.c` + 真实 `encoder.c` + 真实 `motor.c` + 真实 `pid.c` + 真实 `imu.c`
  + 真实 `board_uart/{imu,vision,vofa,stepmotor}_uart.c`（imu_uart 与同表共链，同 test_imu）
  + `fake_board_gpio.c`（编码器原始计数注入）+ `fake_motor_hw.c`（电机抓取）
  + `fake_uart_port.c`（IMU 帧注入）+ `fake_clock.c`（时间注入）。**链 `fake_clock.c` 不链
  `fake_i2c_port.c`**：后者自带 `Clock_NowMs` 会与 fake_clock 重定义（S04/M01 已证实的坑），
  本测试无 OLED/I2C 路径，统一走 fake_clock 注入时间。

### 15.5 契约修订记录

- （冻结初版，无控制律/接口/证据行修订。）
- **审计处置（2026-07-18，arch-auditor 1 建议级 finding，无契约行修订——文档级）**：定角转比例律
  `cmd = clamp(turn_kp·err, ±turn_speed)` 在接近容差时 cmd→`turn_kp·turn_tol_deg`，若低于电机实测
  启动速度会物理失速使 `|err|≤turn_tol_deg` 永不成立、`MOTION_DONE` 不触发、轮询 `Motion_IsDone()`
  的上层挂起。主机 fake 无静摩擦、理想 yaw 注入掩盖该停滞区，**主机侧无法验证任何修复**（§8.1 决议：
  带载/硬件行为验证用户自理；§8.3：保护须能被硬件现象验证）。处置为**登记标定约束、不改冻结控制律、
  不加不可验证的最小驱动下限**：① 标定须保证 `turn_kp·turn_tol_deg` > 实测电机启动速度；② 或由调用者
  （T01 编排）对单次转向设完成超时兜底（`Motion_Stop` 恒可用）。落点：`motion.c motion_step_turn` 代码
  注释 + 本条。最小驱动下限/转向完成超时作为 S06b 或 T01 阶段的候选加固，v1 接受此裕度。
  其余审计项（依赖矩阵/重复数据处理/采样所有权/V21 双泵/电机安全/控制律符号/防御代码）无发现。

## 16. M02 契约（track_elements 循迹元素检测）——冻结

- **task_id**: M02-track_elements
- **goal**: 新建 `middleware/track_elements/`：纯算法循迹元素检测器。从**调用者按值传入**的
  12 路深色位图流中，逐拍提取几何特征（深色路数 / 跨度 / 触边），经**逐检测器连续置信计数**去毛刺，
  在确认时输出**上升沿事件**。检测的是位图的**几何类别**（断线 / 横线 / 左岔 / 右岔），**不做赛道语义
  裁定**（十字 vs 终点、第几次横线归 S07/T01——Q6 不预支）。成为 gray.h / track_error.h 明示
  「不做赛道特征识别」所留 Middleware 空白的唯一所有者。为 S02b 循迹深化提供段切换触发源基元。
- **接口辩护**（算法能做什么）：能对一串深色位图判定当前压在哪一类循迹路面元素上、能用连续拍
  置信计数把瞬时毛刺与确认元素区分、能报告元素确认的上升沿事件与当前置信电平。仅此成为公共面。
- **输入所有权（§8.2 关键）**：M02 **不采样**。`Gray_ReadDarkBitmap()` 的唯一触发所有者仍是
  `LineFollow_Update()`（10ms 门控，未来 S02b 同址喂入）；M02 以位图**按值入参**消费，绝不新开
  第二个 `Gray_ReadDarkBitmap()` 调用点（否则同 V21 双泵）。位序左右修正只用**同一个**
  `bit0_is_left` 标志（H2 实测值，track_error 已落点）：M02 与 track_error 一致地应用它计算
  「车左→车右」position，**不新增第二个反转开关**（encoder `s_direction_sign` 教训，§8.2）。

### 16.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/middleware/track_elements/track_elements.h` / `.c` | 新建 |
| `tests/host/test_track_elements.c` | 新建 |
| `tests/host/Makefile` | 追加 test_track_elements 目标/clean/.PHONY |
| `.gitignore` | 追加 test_track_elements / test_track_elements.exe |
| `Debug/makefile` | 登记 track_elements.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 + 本契约 |

forbidden_files：`hc-team/middleware/track_error/**`、`hc-team/middleware/odometry/**`、
`hc-team/middleware/pid/**`（同层，零触碰——M02 自持位图数学，不复用不包含）、
`hc-team/driver/**`、`hc-team/app/**`、tests/host 既有 `test_*.c` 与 `fake_*.c`。
（Debug/ 下 `subdir_*.mk` 为本地生成物，不入库，不列。）

### 16.2 公共接口（最小面）

```c
#define TRACK_ELEMENTS_CHANNEL_COUNT 12u

/* 元素类别 = 单张位图可区分的几何形态；语义（十字/终点、第几次）归调用者（S07/T01），此处不判。 */
typedef enum {
    TRACK_ELEMENT_GAP = 0,      /* 断线：有效位全 0 */
    TRACK_ELEMENT_FULL_BAR,     /* 横线：触车左且触车右、深色路数 ≥ full_bar_min_count（十字或终点横线，不分语义） */
    TRACK_ELEMENT_BRANCH_LEFT,  /* 左岔/左直角：触车左、未触车右、深色跨度 ≥ branch_min_span */
    TRACK_ELEMENT_BRANCH_RIGHT, /* 右岔/右直角：触车右、未触车左、深色跨度 ≥ branch_min_span */
    TRACK_ELEMENT_COUNT
} TrackElement_Kind;

typedef struct {
    bool     bit0_is_left;       /* 位序左右唯一修正点（与 track_error 同语义透传），决定 car-left→car-right */
    uint8_t  full_bar_min_count; /* FULL_BAR 最小深色路数（几何相关，安装标定后给定），1..12 */
    uint8_t  branch_min_span;    /* BRANCH 最小深色跨度（几何相关），1..12 */
    uint8_t  confirm_ticks;      /* 连续满足多少拍置 confirmed（去毛刺）；0 归一化为 1 */
    uint16_t enable_mask;        /* bit(kind)=1 启用该检测器；未启用者永不计数/触发 */
} TrackElements_Config_T;

typedef struct {
    /* 私有状态——调用者分配（无 malloc）、不得直接读写，一律经下方 API 访问。 */
    TrackElements_Config_T cfg;
    uint8_t  count[TRACK_ELEMENT_COUNT];
    uint16_t confirmed_mask;
    uint16_t just_confirmed_mask;
} TrackElements_Detector_T;

void     TrackElements_Init(TrackElements_Detector_T *det, const TrackElements_Config_T *cfg);
    /* 存配置、计数/掩码清零。cfg==NULL 或 det==NULL 时静默无副作用返回吸收（同 pid/odometry 口径：
     * NULL 视为调用者误用，不做断言/错误码，也不改任何状态）。confirm_ticks==0 归一化为 1。 */
void     TrackElements_Update(TrackElements_Detector_T *det, uint16_t dark_bitmap);
    /* 一拍推进：屏蔽高位 → 按 bit0_is_left 求 car 坐标下的 count/leftmost/rightmost/span/touch →
     * 每个启用检测器谓词成立则 count 饱和自增至 confirm_ticks、否则清 0；count≥confirm_ticks 置
     * confirmed，false→true 的那一拍进 just_confirmed_mask。dark_bitmap 仅低 12 位有效。 */
uint16_t TrackElements_PollEvents(TrackElements_Detector_T *det);
    /* 取并清 just_confirmed_mask（元素确认上升沿事件掩码，段切换触发源）。det==NULL 返回 0。 */
uint16_t TrackElements_GetConfirmed(const TrackElements_Detector_T *det);
    /* 当前确认电平掩码（谓词持续成立期间保持置位）。det==NULL 返回 0。 */
uint8_t  TrackElements_GetConfidence(const TrackElements_Detector_T *det, TrackElement_Kind kind);
    /* 该检测器当前连续置信计数 0..confirm_ticks（硬件标定 confirm_ticks / 阈值用）；kind 越界或 NULL 返回 0。 */
```

- **置信计数口径**：以「连续 Update 拍数」计（非时间）——由 S02b 在 10ms 循迹门控内每拍喂一次，
  故 confirm_ticks 单位 ≈ 10ms。谓词一旦某拍不成立立即清 0（要求连续），去除单拍毛刺。
- **谓词（car-left→car-right 坐标，position 0=车左，11=车右）**：先 `bitmap &= 低 12 位`；
  `count`=置位数，`leftmost/rightmost`=最小/最大 position，`span`=rightmost−leftmost+1，
  `touch_left`=(leftmost==0)、`touch_right`=(rightmost==11)。
  GAP: count==0；FULL_BAR: touch_left && touch_right && count≥full_bar_min_count（用路数=实心度，
  抗稀疏噪声）；BRANCH_LEFT: touch_left && !touch_right && span≥branch_min_span；
  BRANCH_RIGHT: touch_right && !touch_left && span≥branch_min_span（用跨度=延伸度，抗中间缺路）。
  四类天然互斥（count 0 vs >0、触双边 vs 触单边），正常窄线簇触发 0 类（无误报）。
- **单一所有者声明**（M02 一律不复做）：位图采样 / 端口读 = gray Driver（触发 owner = 调用者
  LineFollow_Update / S02b）；位序左右修正 = 唯一 `bit0_is_left`（M02 与 track_error 同值同用，不新增
  开关）；误差重心量化 mm = track_error（M02 不算误差，输出是元素身份）；丢线**控制响应**
  （RECOVERING / 停车）= line_follow/lost_line（M02 的 GAP 是元素身份事件，**不驱动底盘**、不参与控制恢复）。
  M02 **唯一拥有**：几何特征提取（count/span/touch）、逐检测器连续置信去毛刺、元素上升沿事件。
- **头不暴露下层类型**（§3.3）：纯标准 C 类型 + 自持枚举 / 结构；不含任何 Driver / 其他 Middleware 头。

### 16.3 preserved_behavior

- 旧 `app/**`、`driver/**`、`middleware/{track_error,odometry,pid}/**`、其余全部零改动；主机既有
  253 用例全过；固件行为不变（track_elements.o 进链接但零调用者——S02b 未写，V07 同款过渡态）。

### 16.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `driver/\|app/\|middleware/pid/\|middleware/track_error/\|middleware/odometry/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/middleware/track_elements`，`#include` 行） | 0 命中（自身 `track_elements.h`、`<stdint.h>`/`<stdbool.h>` 不在告警集） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §16.1 | 无 allowed_files 之外的改动 |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥267 PASS / 0 FAIL（253 基线 + ≥14 新用例）。必含：Init 清零（计数/掩码 0、无事件）；GAP 连续 confirm_ticks 拍 → 确认+上升沿事件，非空位图 → 计数清 0、电平落；FULL_BAR 触双边且路数达阈 → 确认，稀疏（触双边但路数不足）不确认；BRANCH_LEFT 触左未触右且跨度达阈 → 确认、BRANCH_RIGHT 不确认；BRANCH_RIGHT 对称；bit0_is_left 反转使 BRANCH_LEFT↔BRANCH_RIGHT 互换（唯一修正点，无第二开关）；去毛刺（confirm_ticks−1 拍不确认、单拍毛刺不确认）；中途 miss 清 0 须重新累计；PollEvents 上升沿取一次即清（无新变化再取=0）、GetConfirmed 电平持续；enable_mask 关闭的检测器即使形态出现也永不计数/确认；高位屏蔽（0xF000==空==GAP）；正常窄线簇不误报任何元素；confirm_ticks==0 归一化为 1；GetConfidence 反映计数 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、track_elements.o 经 linkInfo.xml 确证进入 .out 链接 |

- **主机测试链接组成**（事实登记）：test_track_elements = 真实 `track_elements.c` + `test_track_elements.c`
  （纯逻辑，仅 `-lm`）；不链接任何 fake（M02 零端口依赖，同 track_error / heading）。

### 16.5 契约修订记录

- （冻结初版，无控制律/接口/证据行修订。）
- **审计处置（2026-07-18，arch-auditor 无阻断/无重要级，1 建议级 finding——文档级，无证据行修订）**：
  `TrackElements_Init` 头注释原写「不做运行期拒绝」与实现的 `if(NULL) return;` 字面矛盾
  （行为与兄弟模块 odometry 的 return-on-NULL 一致，非新增违规）。处置为**措辞澄清**：
  头注释与 §16.2 一并改为「NULL 时静默无副作用返回吸收，不做断言/错误码」，贴合实际行为，
  不改任何逻辑与证据行。其余审计项（依赖矩阵纯净 / §8.2 不采样+bit0_is_left 无第二反转+GAP 与
  lost_line 职责正交 / 单一所有者 / §8.3 防御代码逐条有故障模型与测试 / 封装 / 16 用例覆盖）无发现。

## 17. M03 契约（speed_plan 速度规划）——冻结

- **task_id**: M03-speed_plan
- **goal**: 新建 `middleware/speed_plan/`：纯算法速度规划器。把**调用者按值传入**的横向误差幅值
  `|error_mm|`（曲率代理）映射为巡航基速目标，并对**调用者持有的**当前基速做**有状态有界斜坡**
  （直道→朝 `straight_speed` 提速、入弯→朝 `min_speed` 降速），输出建议基速 m/s。成为「基速调制」
  这一数据变换的唯一所有者，为 S02b 循迹深化提供可插拔的巡航速度规划基元。
- **接口辩护**（算法能做什么）：能把一个曲率代理量（误差幅值）映射为目标基速、能以受限的加/减速率
  平滑地把当前基速斜坡到目标、能报告当前规划基速、能复位重新起步。仅此成为公共面。
- **输入所有权 / 数据链（§8.2 关键）**：M03 **不采样、不量化误差、不碰 Chassis/电机、不含任何
  Driver/App/其他 Middleware 头**。误差量化的唯一所有者仍是 `track_error`（S02b 接线时由 line_follow
  取 `TrackError` 已算好的 `error_mm`、`fabsf` 后按值喂入，M03 绝不复算误差、绝不新开采样点）。
  M03 唯一拥有：`|error| → 目标基速` 的映射 + 基速斜坡状态 + **输出基速自持限幅**
  （拓扑 §3 已登记「Chassis 无目标限幅」空缺——限速上/下限落在 M03 自身配置，同 `diff_limit_mps`
  先例，不指望 Chassis 兜底）。差速修正与差速限幅仍归外环 PID（M03 零触碰）。

### 17.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/middleware/speed_plan/speed_plan.h` / `.c` | 新建 |
| `tests/host/test_speed_plan.c` | 新建 |
| `tests/host/Makefile` | 追加 test_speed_plan 目标/clean/.PHONY |
| `.gitignore` | 追加 test_speed_plan / test_speed_plan.exe |
| `Debug/makefile` | 登记 speed_plan.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 + 本契约 |

forbidden_files：`hc-team/middleware/{track_error,track_elements,odometry,pid}/**`（同层，零触碰——
M03 自持速度数学，不复用不包含）、`hc-team/driver/**`、`hc-team/app/**`（含 line_follow——S02b 才接线）、
tests/host 既有 `test_*.c` 与 `fake_*.c`。（Debug/ 下 `subdir_*.mk` 为本地生成物，不入库，不列。）

### 17.2 公共接口（最小面）

```c
/** 速度规划配置。全部是标定事实，由调用者提供，无默认值。 */
typedef struct {
    float straight_speed_mps;   /* 直道巡航基速（|error|≈0 时的目标上限），> 0 */
    float min_speed_mps;        /* 入弯最低基速（|error|≥curve_error_mm 时的目标下限），0 ≤ min ≤ straight */
    float curve_error_mm;       /* 达到 min_speed 的误差幅值阈（|error|≥此值→min_speed），> 0；
                                   ≤ 0 视为退化配置：不做曲率降速，目标恒为 straight_speed */
    float accel_mps_per_s;      /* 提速斜坡速率上限（m/s per second），> 0 */
    float decel_mps_per_s;      /* 降速斜坡速率上限（m/s per second），> 0 */
} SpeedPlan_Config_T;

/** 规划器上下文。**调用者分配**（栈或静态皆可，无 malloc）。current_mps 为私有斜坡状态。 */
typedef struct {
    SpeedPlan_Config_T cfg;
    float current_mps;          /* 当前规划基速（斜坡状态），恒被约束在 [min, straight] */
} SpeedPlan_T;

void  SpeedPlan_Init(SpeedPlan_T *sp, const SpeedPlan_Config_T *cfg);
    /* 存配置；current_mps 复位为 min_speed_mps（安全起步：从最低速斜坡上来，不猛冲）。
     * sp==NULL 或 cfg==NULL 时静默无副作用返回吸收（同 pid/track_elements/odometry 口径）。 */
float SpeedPlan_Update(SpeedPlan_T *sp, float abs_error_mm, uint32_t elapsed_ms);
    /* 一拍推进并返回新的规划基速：
     *  ① e = fabsf(abs_error_mm)（内部取绝对值，防调用者误传符号误差）；
     *  ② 映射目标：curve_error_mm ≤ 0 → target = straight_speed（退化：不降速）；
     *     否则 frac = min(e / curve_error_mm, 1) → target = straight + frac × (min − straight)，
     *     再夹到 [min, straight]（普通夹紧，兜住插值端点浮点舍入；min≤straight 由本契约前置保证）；
     *  ③ 斜坡：Δcap = (target>current ? accel : decel) × elapsed_ms/1000；
     *     |target−current| ≤ Δcap 直接到位、否则朝 target 步进 Δcap（elapsed_ms==0 → Δcap=0 → 不变）；
     *  ④ 存并返回 current_mps。sp==NULL → 返回 0，无副作用。 */
float SpeedPlan_GetSpeed(const SpeedPlan_T *sp);   /* 当前规划基速。sp==NULL → 0。 */
void  SpeedPlan_Reset(SpeedPlan_T *sp);            /* current_mps 回 min_speed（重新起步，不改 cfg）。NULL 吸收。 */
```

- **单位链（§8.2 登记）**：输入 `abs_error_mm` = mm（track_error 出口误差的幅值，S02b 由调用者 `fabsf`）；
  `elapsed_ms` = 真实毫秒（调用者 10ms 门控的 elapsed，与 line_follow/chassis 同源）；输出/内部
  `current_mps` = m/s（Chassis 目标基速口径，与 `base_speed_mps` 同尺度）。斜坡速率 m/s per second，
  乘 `elapsed_ms/1000` 化为每拍增量。**无反向/无滤波/无积分**——纯有界斜坡；曲率来源是误差幅值，
  非重新量化。
- **状态与所有权**：`current_mps` 是唯一斜坡状态，调用者持有（无模块 static、无 malloc）；与
  `track_elements` 的置信去毛刺状态、`pid` 的积分状态互不干扰（各自独立上下文）。基速调制唯一所有者
  = M03；S02b 接线后 line_follow 用 M03 输出替换其**固定** `base_speed_mps`，合成 `Chassis_SetTargetMps(base±diff)`
  的**唯一**合成点仍在 `line_follow_apply`（M03 不下沉进 Chassis 语义）。
- **不变量**：初始化/复位后 `current_mps == min_speed_mps`；任意次 Update 后 `min ≤ current ≤ straight`
  （min≤straight 前提下，由 target 夹紧 + 朝 target 斜坡保证，永不越界）。
- **头不暴露下层类型**（§3.3）：纯标准 C 类型 + 自持结构；不含任何 Driver / 其他 Middleware 头
  （需要 `fabsf` 时仅 `<math.h>`；`NULL` 比较用 `<stddef.h>`）。

### 17.3 preserved_behavior

- 旧 `app/**`、`driver/**`、`middleware/{track_error,track_elements,odometry,pid}/**`、其余全部零改动；
  主机既有 269 用例全过；固件行为不变（speed_plan.o 进链接但零调用者——S02b 未接线，V07 同款过渡态）。

### 17.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `driver/\|app/\|middleware/pid/\|middleware/track_error/\|middleware/track_elements/\|middleware/odometry/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/middleware/speed_plan`，`#include` 行） | 0 命中（自身 `speed_plan.h` 及 `<stdint.h>`(头)/`<math.h>`/`<stddef.h>` 不在告警集） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §17.1 | 无 allowed_files 之外的改动 |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥281 PASS / 0 FAIL（269 基线 + ≥12 新用例）。必含：Init/Reset 后 current==min_speed；直道（err=0）逐拍朝 straight **提速且受 accel 斜坡限制**（单拍增量=accel×elapsed/1000，非瞬跳）、足够拍后到 straight 不过冲；入弯（err≥curve_error）从 straight 朝 min **降速且受 decel 斜坡限制**、足够拍后到 min；中间误差线性插值（err=curve_error/2 稳态≈straight−0.5×(straight−min)）；输出恒夹在 [min,straight]；elapsed_ms==0 不改 current；超大 elapsed 一拍到位不过冲 target；curve_error_mm≤0 退化（目标恒 straight、大误差也不降速）；负 abs_error 内部取绝对值（与正误差同目标）；NULL 吸收（Init/Update/GetSpeed/Reset 不崩，Update/GetSpeed(NULL)→0）；GetSpeed 反映 current |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、speed_plan.o 经 linkInfo.xml 确证进入 .out 链接 |

- **主机测试链接组成**（事实登记）：test_speed_plan = 真实 `speed_plan.c` + `test_speed_plan.c`
  （纯逻辑，仅 `-lm`）；不链接任何 fake（M03 零端口依赖，同 track_elements / track_error / heading）。

### 17.5 契约修订记录

- （冻结初版。）
- **修订 1（2026-07-18，本提交，审计后处置——单独提交，先于满足它的代码 fix）**：arch-auditor
  无阻断/无重要级，2 建议级 finding，处置如下：
  1. **F1（§8.3 防御代码）**：`speed_plan.c` 目标夹紧原用 `clampf(target, fminf(min,straight),
     fmaxf(min,straight))` 排序夹紧，专吸收 `min>straight` 误配——但该条件被 §17.2 明列为契约违反
     （`0≤min≤straight`），等同 NULL 的调用者误用，且 16 用例全在 `min<straight` 下运行、排序分支
     从未触发（§8.3 三要件之③可验证性缺失）。**采纳方案 b**：删 `fminf/fmaxf`，简化为
     `clampf(target, min_speed, straight)`（保留普通夹紧兜插值端点浮点舍入——此路径有真实故障模型
     且被 curve/straight 端点用例覆盖；`min≤straight` 前提由契约保证）。§17.2 Update ② 与「头不暴露」
     bullet 相应去 `fminf/fmaxf` 措辞。E03 用例数不变（16），简化后重跑确认不回归。
  2. **F2（证据文本精确性，非架构违反）**：E01 白名单文本误列 `<stdbool.h>`（实际未用）、漏列
     `<stddef.h>`（`.c` 为 `NULL` 实用）。更正为 `<stdint.h>`(头)/`<math.h>`/`<stddef.h>`；0 命中结论不变。
  其余审计维度（依赖矩阵/重复数据处理/单位链/封装/ISR·电机安全/契约符合性）逐项通过，无发现。

## 18. S02b 契约（line_follow 深化：M02 事件面 + M03 速度调制接入）——冻结

- **task_id**: S02b-line_follow
- **goal**: 在既有 `app/service/line_follow/` 上一次性接入两个已建成的 Middleware 纯算法基元：
  1. **M03 `speed_plan` 速度调制**——用规划基速替换 `LineFollow_Config_T` 里的**固定** `base_speed_mps`：
     每拍以 `fabsf(error_mm)`（曲率代理）+ 真实 `elapsed_ms` 推进 `SpeedPlan_Update`，其输出成为
     `line_follow_apply` 里 `base±diff` 合成的 `base`。**合成点不搬家**——仍唯一在 `line_follow_apply`；
     M03 只产建议基速，绝不写 `Chassis_SetTargetMps`。
  2. **M02 `track_elements` 元素事件面**——把 `LineFollow_Update` 已读的**同一张**深色位图**按值并列**
     喂给 `TrackElements_Update`（不新开采样点），并新增公共出口 `LineFollow_PollElementEvents()`
     导出确认的上升沿事件掩码，作为未来 S07 段切换的触发源。
- **零消费者是预期状态**：事件面/调制均无上层调用者（S07/T01 未写，V07/S04 同款过渡态）——
  不得为「让它有人调」把 line_follow 临时接进旧 tasks。
- **接口辩护**（循迹功能扩面后能做什么）：在既有「沿线行驶/丢线恢复/超时停车/报告误差遥测/调增益」
  之上，新增两项可用「循迹底盘能做什么」解释的能力——**按弯道曲率自动调整巡航速度**（直道提速、
  入弯减速）、**报告经过的循迹几何元素事件**（断线/横线/左岔/右岔的确认上升沿）。仅此进公共面。
- **拆分背景**：本任务是 §2 第 7 条裁定的 S02b——M03 已作纯算法独立立项并 DONE（§17），此处只做
  line_follow 服务接线（S02 契约修订流程，非重写 chassis/lost_line/track_error）。

### 18.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/line_follow/line_follow.h` | 修改（配置字段换代 + 新元素枚举 + PollElementEvents + 遥测扩项） |
| `hc-team/app/service/line_follow/line_follow.c` | 修改（接入 speed_plan + track_elements，合成点不搬家） |
| `tests/host/test_line_follow.c` | 修改（k_cfg 适配新字段 + ≥12 新用例） |
| `tests/host/Makefile` | 修改（test_line_follow 目标追加 `$(TRACK_ELEMENTS_SRC)` + `$(SPEED_PLAN_SRC)`，两处：依赖表 + 编译行） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 + 本契约 |

forbidden_files：`hc-team/middleware/speed_plan/**`、`hc-team/middleware/track_elements/**`
（只消费不修改，二者 DONE 冻结）、`hc-team/app/service/line_follow/lost_line.{h,c}`（丢线策略不动）、
`hc-team/middleware/{track_error,pid,odometry}/**`、`hc-team/app/service/chassis/**`（只调用不修改）、
`hc-team/app/{tasks,scheduler,system,ui}/**`、`hc-team/driver/**`、tests/host 其余既有 `test_*.c` 与
`fake_*.c`（fake_gray_port 已有注入面，无需改）。**`Debug/makefile` 不改**：`speed_plan.o`/
`track_elements.o` 已由 M02/M03 登记进链（§16.4/§17.4 E04 已证），`line_follow.o` 已在册——
S02b 只让 `line_follow.o` 重编并引用这两个既有目标，无新增 ORDERED_OBJS。
（`.gitignore` 亦不改：`test_line_follow` 产物 S02 已登记。）

### 18.2 公共接口变更（最小面）

```c
/** 循迹几何元素事件位（= 1u<<kind，与 middleware/track_elements 的 TrackElement_Kind 同位序；
 *  .c 内 _Static_assert 锁定一致，头不暴露「用了哪个 Middleware」——§3.4）。 */
typedef enum {
    LINE_FOLLOW_ELEM_GAP          = 1u << 0, /* 断线：有效位全 0 */
    LINE_FOLLOW_ELEM_FULL_BAR     = 1u << 1, /* 横线：触双边且深色路数达阈（十字/终点，不分语义） */
    LINE_FOLLOW_ELEM_BRANCH_LEFT  = 1u << 2, /* 左岔/左直角 */
    LINE_FOLLOW_ELEM_BRANCH_RIGHT = 1u << 3, /* 右岔/右直角 */
} LineFollow_Element;

typedef struct {
    float    pitch_mm;             /* 相邻探头中心间距，必须 > 0（透传 track_error / track_elements 几何） */
    bool     bit0_is_left;         /* 位序唯一修正点——track_error 与 track_elements 共用此一标志，
                                      各自对同一位图独立应用（并列消费，非级联二次反转） */
    /* —— 巡航速度规划（M03 speed_plan，替换原固定 base_speed_mps）—— */
    float    straight_speed_mps;   /* 直道巡航基速上限（|error|≈0 目标），> 0；即原 base_speed 语义 */
    float    min_speed_mps;        /* 入弯最低基速下限，0 ≤ min ≤ straight */
    float    curve_error_mm;       /* 达 min_speed 的误差幅值阈，> 0；≤0 退化=不降速（基速恒 straight） */
    float    accel_mps_per_s;      /* 提速斜坡速率上限，> 0 */
    float    decel_mps_per_s;      /* 降速斜坡速率上限，> 0 */
    /* —— 差速外环 —— */
    float    diff_limit_mps;       /* 差速修正限幅，必须 > 0（外环 PID out_limit，唯一所有者） */
    /* —— 丢线恢复（lost_line，不变）—— */
    float    recovery_error_mm;    /* 丢线回退误差幅值（建议 ≈ 2.7 × pitch_mm） */
    uint32_t lost_timeout_ms;      /* 丢线恢复上限，超时 → LOST 停车 */
    /* —— 循迹元素检测（M02 track_elements）—— */
    uint8_t  full_bar_min_count;   /* 判 FULL_BAR 的最小深色路数（1..12），几何安装事实 */
    uint8_t  branch_min_span;      /* 判 BRANCH 的最小深色跨度（1..12），几何安装事实 */
    uint8_t  element_confirm_ticks;/* 连续满足多少拍置确认（去毛刺）；0 归一化为 1 */
    uint16_t element_enable_mask;  /* bit(LineFollow_Element)=1 启用该检测器；0 = 全不检测 */
} LineFollow_Config_T;              /* 移除原 base_speed_mps（被 straight_speed_mps 语义承接） */

typedef struct {
    uint16_t dark_bitmap;          /* 最近一次采样位图 */
    float    error_mm;             /* 最近一拍使用的误差（含回退误差） */
    float    base_speed_mps;       /* 【新】最近一拍规划基速（speed_plan 输出，合成 base±diff 的 base） */
    float    diff_cmd_mps;         /* 最近一拍差速修正 c（left=base+c, right=base−c） */
    uint16_t confirmed_elements;   /* 【新】当前确认电平掩码（LineFollow_Element 位；非事件、不清） */
    LineFollow_State state;
} LineFollow_Telemetry_T;

/** 【新】取出并清空循迹元素确认的上升沿事件（段切换触发源）；无事件 → 0。
 *  语义 = middleware/track_elements 的 PollEvents 透传（取后清）；与遥测 confirmed_elements
 *  （电平、不清）互不冲突。IDLE/LOST 静默期不采样故不产生新事件。 */
uint16_t LineFollow_PollElementEvents(void);
```

- **`LineFollow_Init/SetGains/Start/Update/Stop/GetState/GetTelemetry` 签名不变**；仅 `Init` 内部
  多映射两组 flat 配置到 `SpeedPlan_T`/`TrackElements_Detector_T`，`Start` 多做 `SpeedPlan_Reset`
  （回 `min_speed`，安全起步）+ `TrackElements` 复位（清陈旧置信/事件，避免跨 run 误触发）。
- **配置有效性判据不变**：`s_cfg_valid = (pitch_mm>0) && (diff_limit_mps>0)`——speed_plan 退化配置
  （straight=min / curve_error≤0）安全无害（自持夹紧），不纳入 Start 门控，避免无依据防御。

### 18.3 数据链与单一所有者声明（§8.2，本任务核心）

接线后一拍数据流（`LineFollow_Update` 到期路径）：

```text
Gray_ReadDarkBitmap()  ← 唯一采样点（不变，10ms 门控）
   ├─（并列消费者 1）TrackElements_Update(&s_elements, bitmap)   ← M02，同一位图按值，几何元素置信/事件
   └─（并列消费者 2）TrackError_FromDarkBitmap → error_mm         ← 误差量化唯一所有者（不变）
error_mm →（丢线时 LostLine_Tick 给回退误差，不变）
   ├─ SpeedPlan_Update(&s_speed_plan, fabsf(error_mm), elapsed_ms) → base   ← M03，基速调制唯一所有者
   └─ Pid_UpdatePositional(&s_outer_pid, error_mm, 0) → diff                ← 差速修正唯一所有者（不变）
base, diff → line_follow_apply: Chassis_SetTargetMps(base+diff, base−diff)  ← 唯一合成点（不搬家）
   → Chassis_Update()   ← 唯一内环泵（不变，V21 单泵，不新增第四推进点）
```

- **采样单一所有者**：`Gray_ReadDarkBitmap()` 仍是 `LineFollow_Update` 内唯一一次原子读；track_elements
  与 track_error 与 speed_plan(经 error) 全部消费这**一次**读的结果，**不新开第二采样点**（不构成 V21 双泵）。
- **误差量化单一所有者**：`middleware/track_error`（不变）。`speed_plan` 只读 `fabsf(error_mm)` 的**副本**，
  **不复算误差**；`track_elements` 由位图独立判几何类别，与误差量化正交，**不复算**。
- **位序左右单一修正点**：`s_cfg.bit0_is_left` **一个**字段，透传给 `track_error` 与 `track_elements`
  **各一次**、对同一位图**并列独立**应用——非级联，无第二反转开关（encoder `s_direction_sign` 教训）。
- **基速调制单一所有者**：`middleware/speed_plan`（§17 落定）。line_follow 只在 `line_follow_apply`
  **唯一**合成点用其输出替换原固定常量，不在别处二次调制/夹紧基速。
- **差速限幅单一所有者**：外环 `Pid_T.cfg.out_limit`(= `diff_limit_mps`)（不变）。
- **轮速闭环/电机保护（slew/换向/超时/刹车）**：chassis 及 motor.c 既有所有者（不变，本服务零复做）。
- **元素事件语义**：`track_elements` 的 `just_confirmed_mask` 取后清语义经 `PollElementEvents` 透传；
  遥测 `confirmed_elements` 走 `GetConfirmed`（电平、不清），二者读不同源不互相清除。

### 18.4 preserved_behavior

- `app/service/chassis/**`、`lost_line.{h,c}`、`middleware/{speed_plan,track_elements,track_error,pid,odometry}/**`、
  `driver/**`、其余 `app/**` **零改动**（speed_plan.c / track_elements.c 只被链接消费，源码不动）。
- **既有 12 条 line_follow 用例行为不变**：标准 `k_cfg` 映射为**退化 speed_plan**
  （`straight=min=0.5`、`curve_error≤0` → base 恒 0.5），使原有中央直行/差速符号/差速限幅/丢线/
  超时/积分/重获/Stop/门控断言逐字保持；仅 `k_cfg` 字段名随结构体换代。其余测试文件零改动。
- 固件行为不变：`line_follow.o` 重编后引用 `speed_plan.o`/`track_elements.o`（已在链），仍**零调用者**，
  上层不泵 `LineFollow_Update` 则不产生任何底盘命令。

### 18.5 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/\|app/scheduler/\|app/ui/\|app/system/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/service/line_follow`，`#include` 行） | 0 命中（新增 `middleware/speed_plan`、`middleware/track_elements` 是 Service→Middleware 合法边，不在告警集） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §18.1 | 无 allowed_files 之外的改动（尤其 `Debug/makefile`、`speed_plan/**`、`track_elements/**`、`lost_line.*`、`chassis/**` 零改） |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥297 PASS / 0 FAIL（285 基线 + ≥12 新用例）。必含——**M03 调制**：直道恒 err=0 时 base 从 min 受 accel 斜坡逐拍提速朝 straight（单拍增量=accel×elapsed/1000，非瞬跳）、双轮=base（diff=0）；入弯（err≥curve_error）base 受 decel 斜坡朝 min 降速；`base±diff` 合成点消费的是 speed_plan 输出而非常量（telemetry.base_speed_mps 反映斜坡）；Start 后 base 复位 min（安全起步）；RECOVERING 大回退误差驱动 base 朝 min（丢线降速安全）。**M02 事件面**：GAP/FULL_BAR/BRANCH_LEFT/BRANCH_RIGHT 各自连续 confirm_ticks 拍 → PollElementEvents 出对应上升沿位；单拍毛刺不触发（去毛刺）；PollEvents 取后清（二次取=0）、GetConfirmed 电平续；bit0_is_left=false 使 BRANCH_LEFT↔RIGHT 互换（共用同一标志，无第二反转）；enable_mask=0 时任何形态不产生事件。**单一所有者/无双采样**：驱动元素检测不额外增加 Gray 读计数（每拍仍 1 次，元素与误差并列消费同一读）；IDLE/LOST 静默期不采样故不产生新元素事件。 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、`line_follow.o` 重编并经 linkInfo.xml 确证仍在 .out 链接（`speed_plan.o`/`track_elements.o` 已在链，无 ORDERED_OBJS 变更） |

- **主机测试链接组成**（事实登记）：test_line_follow = 真实 `line_follow.c` + `lost_line.c` + `chassis.c`
  + `speed_plan.c` + `track_elements.c` + `track_error.c` + `pid.c` + `encoder.c` + `motor.c`
  + `gray.c` + fake 端口（`fake_gray_port` / `fake_board_gpio` / `fake_motor_hw` / `fake_clock`）。
  只 fake 端口/硬件边界，不 fake 被测 Service/Middleware/Driver 本体。

### 18.6 契约修订记录

- （冻结初版。审计后处置若有，单独提交并在此追加修订原因 + 提交哈希。）

## 19. S06b 契约（motion 圆弧原语：定半径 + 定角）——冻结

- **task_id**: S06b-motion-arc
- **goal**: 在既有 `app/service/motion/` 上扩面一条运动基元——「以定半径 R、按定角
  `arc_deg` 走一段圆弧后停」的**非阻塞状态机**，建成在 chassis 速度内环（S01）与 odometry
  连续航向/位姿（M01）之上。控制律 = **双轮速度比前馈**（由 R 与轮距推导内外轮速）
  **+ 航向误差反馈修正**（用 odometry 连续航向纠正半径漂移）。为后续 S07 段路线执行的
  `ARC` 段类型提供现成基元（本任务是 S07 该段的前置件，用户 2026-07-18 裁定先于 S07）。
- **交付形态**：沿用 S06 motion 服务闭环流程**扩面**——`motion.{h,c}` 修改（非新建）：
  新增一个公共函数 `Motion_StartArc`、一个状态 `MOTION_ARC`、两个 cfg 字段
  （`track_width_mm`/`arc_speed_mps`）；`Motion_Update` 增一条 ARC 分支。既有 STRAIGHT/TURN
  行为逐字不变。
- **接口辩护**（底盘能做什么）：底盘能沿一个指定半径、转过指定圆心角后确定性停车，
  行进中用航向反馈把半径守在标称值。仅此一条新能力进入公共面。
- **IMU 泵 / 里程计消费 / 内环泵所有权（继承 S06，不新增所有者）**：ARC 复用
  `Motion_Update` 同一主循环——`Imu_Update()` 仍是 motion 激活期独占的**唯一**排空点（V23，
  不新增第二个）；里程计 `total_pulses` 差值仍**恰好消费一次**（V22，last_total 机制不变）；
  `Chassis_Update()` 仍是 motion 既有第三推进点（V21），ARC 落进同一推进点，**不构成第四个
  推进者**，未来挂 scheduler `on_enter/on_exit` 时与 STRAIGHT/TURN 一并纳管。

### 19.0 轮距单一所有者裁定（本契约核心，回应拓扑 S06b 待裁项）

- 全仓此前**无 `track_width` 常量**（M01 航向权威 = IMU 去卷，位姿 = 编码器位移，均不需轮距）。
  圆弧「双轮速度比」需要轮距才能由 R 推导内外轮速——**新增单一所有者点
  `Motion_Config_T.track_width_mm`**（mm，实测标定，装配层注入）。
- **该字段仅用于圆弧前馈速度比，绝不构成第二个航向权威**：圆弧的实际航向仍由
  odometry 连续航向（IMU 源）测量与判完成，轮距只设**标称**内外轮速；反馈修正读 odometry
  航向。故不存在「轮差分测航向」与 IMU 航向竞争的双源。V22 登记扩一条：轮距新所有者
  = motion cfg，用途限定前馈几何，收工时 topo-updater 记入 §6/§7。
- **不复算**：轮距不参与脉冲→距离（那仍是 `Odometry_Config.mm_per_pulse` 唯一所有者）、
  不参与航向符号（`heading_sign` 唯一所有者）、不参与 unwrap（`heading.c` 唯一所有者）。

### 19.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/motion/motion.h` | 修改（+`MOTION_ARC`、+2 cfg 字段、+`Motion_StartArc` 声明与注释） |
| `hc-team/app/service/motion/motion.c` | 修改（+ARC 状态机分支、+StartArc、+前馈/修正控制律、+轮距几何） |
| `tests/host/test_motion.c` | 修改（追加 ≥10 条圆弧用例；既有直行/转弯用例逐字不动） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 + 本契约冻结/修订 |

forbidden_files：`hc-team/app/service/{chassis,line_follow,tuning,hmi}/**`（chassis 只调用不改，
其余零触碰）、`hc-team/app/{tasks,scheduler,system,ui}/**`、`hc-team/driver/**`（Encoder/Imu 仍只经
`*_GetSnapshot`/`Imu_Update`，零改）、`hc-team/middleware/**`（odometry/pid 只调用不改）、
`Debug/makefile`（motion.o 已登记，无 ORDERED_OBJS 变更）、`tests/host/Makefile`（test_motion 目标
已存在，无新增源）、`.gitignore`（已忽略 test_motion）、tests/host 其余既有 `test_*.c` 与 `fake_*.c`。

### 19.2 公共接口变更（最小面）

```c
/* 状态枚举新增一项（其余不变） */
typedef enum { MOTION_IDLE = 0, MOTION_STRAIGHT, MOTION_TURN, MOTION_ARC, MOTION_DONE } Motion_State;

/* Motion_Config_T 新增两字段（其余字段不变；hold_kp/ki/kd + hold_diff_limit_mps + turn_tol_deg 被 ARC 复用） */
    float track_width_mm;      /* >0，轮距实测标定；仅用于圆弧前馈内外轮速比（§19.0 单一所有者） */
    float arc_speed_mps;       /* 圆弧圆心线速度基速（前进为正，>0） */

bool Motion_StartArc(float radius_mm, float arc_deg);
    /* 拒绝（返回 false，保持当前态）：radius_mm<=0；arc_deg==0；radius_mm < track_width_mm/2
     *   （内轮将反向——本原语约束为前进圆弧，不做单轮倒转的原地掰弯，那属 TURN 语义）。
     * 成功：捕获当前 odometry 航向为 heading0、当前位姿为路径长基准（arc_len=0、prev=当前 x/y）、
     *   清航向修正 PID 史（复用 hold PID 实例，单活动原语保证不与直行纠偏并发）、记录 arc_deg、
     *   state=MOTION_ARC、返回 true。符号约定同 TURN：arc_deg>0 = CCW（左转，航向递增），<0 = CW。 */
```

- `Motion_Update` ARC 分支（并入既有共享前段：`Imu_Update`→快照→`Odometry_Update`→`Odometry_GetPose`）：
  1. `rel = heading − heading0`（odometry 连续角，签名带向）；
  2. 路径长累计 `arc_len_mm += hypot(x−prev_x, y−prev_y)`（**消费 odometry 毫米位姿输出**，
     非第二次脉冲→距离换算；motion 自持派生量）；`prev_x/prev_y ← x/y`；
  3. 完成判据（**航向驱动，IMU 权威**）：`|arc_deg| − |rel| ≤ turn_tol_deg` → `Chassis_Stop`
     + `state=MOTION_DONE`；
  4. 否则前馈：`dir = sign(arc_deg)`；`half = track_width_mm/2`；
     `v_inner = arc_speed_mps·(R−half)/R`、`v_outer = arc_speed_mps·(R+half)/R`（R=radius_mm）；
     CCW(dir>0)：left=inner、right=outer；CW：left=outer、right=inner；
  5. 航向修正（**读 odometry 航向**）：`exp = dir·deg(arc_len_mm/R)`，幅值夹至 `|arc_deg|`；
     `corr = Pid_UpdatePositional(hold_pid, exp, rel)`（setpoint=exp、feedback=rel，
     out_limit = `hold_diff_limit_mps` 唯一所有者）；`left −= corr`、`right += corr`
     （与直行纠偏同一符号律：欠转→corr 推向「转更多」，CCW/CW 经 exp/rel 带号自洽）；
  6. `Chassis_SetTargetMps(left, right)` → 末尾恒推 `Chassis_Update()`（内环自门控 10ms）。
     IDLE/DONE 仍**完全静默**（不泵内环、刹车真值表保持，S06 同款）。
- **遥测复用**（`Motion_Telemetry_T` 结构不变）：ARC 时 `target=arc_deg`、`progress=rel`（已转过度），
  `x_mm/y_mm/heading_deg` 照旧。

### 19.3 数据链与单一所有者声明（§8.2）

- **前馈几何**：`R, half=track_width_mm/2 → (R±half)/R` 为无量纲比，乘 `arc_speed_mps`[m/s]
  得左右 [m/s]。轮距唯一所有者 = motion cfg（§19.0），仅前馈用。
- **完成与修正基准分离**：完成判据读 **odometry 连续航向**（IMU 源，权威）；修正参考
  `exp` 由 **motion 自持路径长 arc_len_mm**（消费 odometry 毫米位姿，非复算脉冲）÷R 得。
  已知特性（显式登记，不加无据防御）：`exp` 依赖里程标定与 R 一致；标定偏差使半径略偏，
  但完成角由 IMU 航向锁定不受影响——实车整定归用户，本原语接受此裕度。
- **不复做**（一律经既有所有者）：脉冲→距离 = `Odometry_Config.mm_per_pulse`；yaw 符号 =
  `heading_sign`；unwrap = `heading.c`；输出限幅/slew/换向过零死区/命令超时/刹车真值表 =
  `motor.c`（经 chassis）；差速修正限幅 = `hold_diff_limit_mps`(= 该 PID out_limit)；
  底盘目标限幅 = chassis 既有空缺（motion 不抢先补——外轮速 `v_outer>arc_speed` 是几何必然，
  不额外夹紧，与 STRAIGHT/TURN 同口径）。motion 本任务**新增唯一拥有**：轮距前馈几何 +
  圆弧路径长累计 + 圆弧状态机 + 航向修正参考生成。
- **头不暴露 Driver 类型**（§3.4）：新增面只用 `float`/`bool`/自持枚举，不出现 Driver 类型。

### 19.4 preserved_behavior

- `app/service/{chassis,line_follow,tuning,hmi}/**`、旧 `app/**`、`driver/**`、`middleware/**` 零改动；
  既有 motion 直行/原地转行为逐字不变（新增 ARC 分支不改 STRAIGHT/TURN 路径）；主机既有
  300 用例全过；固件行为不变（`motion.o` 重编后仍**零调用者**，上层不泵 `Motion_Update`
  则不产生任何底盘命令——Task 未写，V07 同款过渡态）。

### 19.5 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/\|app/scheduler/\|app/ui/\|app/system/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/service/motion`，`#include` 行） | 0 命中（`chassis.h` 同层、`odometry.h`/`pid.h` Middleware、`encoder.h`/`imu.h` Driver 均矩阵允许边） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §19.1 | 无 allowed_files 之外的改动（尤其 `Debug/makefile`、`chassis/**`、`middleware/**`、`driver/**` 零改） |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥310 PASS / 0 FAIL（300 基线 + ≥10 新用例）。必含——**拒绝/起步**：StartArc(R≤0)/(arc_deg=0)/(R<track/2) 各返回 false 且保持前态；合法 StartArc→MOTION_ARC 且 IsDone=false。**前馈速度比**（起步 arc_len≈0、exp≈0、corr≈0 时）：CCW 圆弧右轮(外)>左轮(内)、比值 ≈ (R+L/2)/(R−L/2)（容差内）；CW 圆弧左轮(外)>右轮(内)。**航向修正**：注入编码器位移使 arc_len 增而 IMU 航向滞后（欠转）→ CCW 下修正使右轮更快/左轮更慢（转更多），CW 反向。**完成（IMU 权威）**：注入航向扫到 arc_deg → \|arc_deg\|−\|rel\|≤turn_tol_deg → Chassis_Stop(BrakeAll)+DONE+IsDone。**静默/安全**：DONE 后多拍 Update 不泵内环、刹车真值表保持、无新电机命令刷新；ARC 中 Motion_Stop→Chassis_Stop+IDLE。**无双计数**：ARC 中两次连续 Update 间 total 不变→arc_len/pose 不二次前进。**回归**：既有直行/原地转用例逐字全过。 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、`motion.o` 重编并经 linkInfo.xml 确证仍在 .out 链接（无 ORDERED_OBJS 变更） |

- **主机测试链接组成**（不变，事实登记）：test_motion = 真实 `motion.c` + `chassis.c` + `odometry.c`
  + `heading.c` + `encoder.c` + `motor.c` + `pid.c` + `imu.c` + `board_uart/{imu,vision,vofa,stepmotor}_uart.c`
  + fake 端口（`fake_board_gpio`/`fake_uart_port`/`fake_clock`/`fake_motor_hw`）。新增用例仍用同一链接组成。

### 19.6 契约修订记录

- **冻结初版**（8b030a5），无契约行修订。
- **审计处置（2026-07-18，无契约行修订）**：arch-auditor 六轴通过（依赖矩阵 / 单一所有者 /
  电机安全 / ISR 所有权 / 控制律符号 / 无据防御），唯一 finding 为建议级——`motion.h` 遥测
  `target`/`progress` 头注释未覆盖 ARC（仍写「否则 0」，而 ARC 态实返回 arc_deg/rel）。
  处置：注释级修正为「ARC=圆心角/已转过 deg；IDLE/DONE=0」，逻辑不动，随代码提交 2aa2ba9 一并入库。
  审计另记一条非发现观察（契约 §19.3 已显式接受）：欠转修正在 `hold_diff_limit_mps>v_inner`
  时可使内轮瞬时为负——差速转向常态，过零+死区由 motor.c 独占，motion 不抢先夹紧（同 STRAIGHT/TURN 口径）。

## 20. S07 契约（route 分段路线执行服务）——冻结

- **task_id**: S07-route
- **goal**: 新建 `app/service/route/`：分段路线执行服务。以**装配层注入的段表**（`Route_Segment_T[]`）
  驱动一个**非阻塞段级状态机**，逐段执行四类运动基元——`FOLLOW_UNTIL`（循迹直到命中指定循迹元素
  事件）/`STRAIGHT`/`TURN`/`ARC`（后三者=motion 语义原语），按每段**自然完成**推进到下一段，
  直至全部段完成（DONE）或段失败（FAULT）。成为「段序编排 + 段完成触发判定 + 段间确定性交接」这条
  多环触发+逻辑编排链的唯一 Service 所有者（AGENTS.md §15.6 解除后裁定：多环触发+逻辑编排归 Service）。
  吸收旧 `task1.c` 的分段状态机语义（旧文件冻结不迁移，按新数据链重建；旧 task1 用裸角积分+固定编排，
  本服务改用已建成的 line_follow 元素事件面 + motion 去卷航向原语）。为 T01 赛题 Task 提供「换段表即换赛题」
  的现成执行器。
- **接口辩护**（底盘能做什么）：底盘能按一张段表依次「沿线走到某个循迹元素、走一段直线、原地转一个角度、
  走一段定半径圆弧」，每段完成自动切下一段，全程可随时确定性停车，能报告当前执行到第几段与总体状态。
  仅此成为公共面。
- **交付形态**：新建 `route.{h,c}` + `test_route.c`（不改 line_follow/motion/chassis，只调用其公共 API）。
- **Q6 三问定案（用户 2026-07-18 三问裁定，见 §5 Q6 / §2.9）**：① 段类型 = 四类且任意混合；
  ② 段表由**装配层/T01 持有并经 `Route_Setup` 注入**（route 纯执行机制，同 scheduler/menu 先例）；
  ③ 完成触发源**按段类型分派**（FOLLOW_UNTIL=元素事件 / motion 段=`Motion_IsDone`），无全局单一主触发；
  ④ 段间**确定性交接**（隔拍刹停间隙 + 进 motion 段前 odometry catch-up）+ 每段**可选完成超时兜底**。
- **零调用者是预期状态**（T01 未写）：route 只交付被挂载的构件（`Route_Start/Update/Stop` 与
  `Scheduler_Entry_T` 钩子签名兼容），由 T01 装配填入条目表——**方向是 Scheduler 调 route，非 route 调
  Scheduler**（V07/S04/UI01 同款过渡态）。不得为「让它有人调」把 route 临时接进旧 tasks。

### 20.0 单一所有者与推进不变量（本契约核心，回应 topo-navigator 双泵告警）

- **route 不直接触碰任何下层**：route.c 只 `#include "line_follow.h"` 与 `"motion.h"`（Service→Service
  同层受控，矩阵允许），**绝不** include `scheduler.h`（Service→Scheduler 矩阵**禁止**）、
  `track_elements.h`（元素位序经 line_follow 透传的 `LineFollow_Element`，route 不复引）、
  任何 `driver/**` / `middleware/**` / `chassis.h`（chassis 由 line_follow/motion 独占，route 不旁路）。
  route **绝不**直接调 `Chassis_Update` / `Chassis_SetTargetMps` / `Imu_Update` / `Odometry_*` /
  `Encoder_*` / `Gray_*`——全部经 line_follow / motion 间接。
- **每拍至多推进一个子服务（V21/V23 结构性排除）**：`Route_Update` 在 RUNNING 期，一拍只推进当前段的
  **唯一**子服务——FOLLOW_UNTIL 段推 `LineFollow_Update()`、motion 段推 `Motion_Update()`（进 motion 段
  的 catch-up 也是 motion 自身，非并发第二服务）。route 因此**不构成第四个 `Chassis_Update` 推进点**
  （V21）、**不构成第二个 `Imu_Update` 排空点**（V23）——两个子服务永不在同一拍并发驱动底盘，
  Chassis_Update 的实际驱动源恒 ≤1。此不变量与 scheduler 单活动条目不变量同构（route 自身运行在
  单活动条目内）。
- **route 新增唯一拥有**：段序状态机 + 段完成触发判定（按段类型选 元素事件 / `Motion_IsDone`）+
  段间确定性交接（隔拍刹停间隙 + 进 motion 段前的 odometry catch-up）+ **段级完成超时兜底**
  （承 S06 §15.5 deferred 的「转向完成超时」归此编排层）。
- **route 一律不复做**（经既有所有者）：循迹外环/丢线/速度调制/元素几何检测 = line_follow 及其
  Middleware（track_error/speed_plan/track_elements）；语义运动状态机/到位判据/航向保持/圆弧前馈/
  IMU 排空/里程计消费/unwrap = motion 及其 Middleware（odometry/heading/pid）；底盘速度环/输出限幅/
  slew/换向过零死区/命令超时/刹车真值表 = chassis + motor.c；元素位序 `bit0_is_left` = line_follow 配置
  （route 不新增第二反转）。route 对任何数据变换零复做——它只编排既有原语的启停与推进节奏。

### 20.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/route/route.h` / `.c` | 新建 |
| `tests/host/test_route.c` | 新建 |
| `tests/host/Makefile` | 追加 test_route 目标/clean/.PHONY |
| `.gitignore` | 追加 test_route / test_route.exe |
| `Debug/makefile` | 登记 route.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 + 本契约冻结/修订 |

forbidden_files：`hc-team/app/service/{line_follow,motion,chassis,tuning,hmi}/**`（line_follow/motion
只调用不修改，其余零触碰）、`hc-team/app/{tasks,scheduler,system,ui}/**`、`hc-team/driver/**`、
`hc-team/middleware/**`（含 track_elements——route 不 include）、tests/host 既有 `test_*.c` 与
`fake_*.c`（复用既有注入面，无需改）。（Debug/ 下本地生成物 `subdir_*.mk`/`sources.mk`/`ccsObjs.opt`
本地补列不入库，不列。）

### 20.2 公共接口（最小面）

```c
/** 段类型：单张段表可混合排列的四类运动基元。 */
typedef enum {
    ROUTE_SEG_FOLLOW_UNTIL = 0, /* 循迹直到命中指定元素事件（line_follow 驱动） */
    ROUTE_SEG_STRAIGHT,         /* 直行定距（motion 驱动） */
    ROUTE_SEG_TURN,             /* 原地定角转（motion 驱动） */
    ROUTE_SEG_ARC,              /* 定半径定角圆弧（motion 驱动） */
} Route_SegKind;

/** 段描述符（装配层填写；kind 决定哪些字段有效，其余忽略）。 */
typedef struct {
    Route_SegKind kind;
    /* FOLLOW_UNTIL */
    uint16_t until_elements;    /* LineFollow_Element 位掩码，任一位命中(OR)即段完成；
                                   ==0 = 无元素目标（该段仅靠 timeout/LOST/Route_Stop 终止，谨慎） */
    /* STRAIGHT */
    float    distance_mm;       /* 前进距离（>0，透传 Motion_StartStraight） */
    bool     heading_hold;      /* 直行航向保持开关（透传 Motion_StartStraight） */
    /* TURN */
    float    turn_deg;          /* 相对角（≠0，+CCW/−CW，透传 Motion_StartTurn） */
    /* ARC */
    float    arc_radius_mm;     /* 半径（>track_width/2，透传 Motion_StartArc） */
    float    arc_deg;           /* 圆心角（≠0，+CCW/−CW，透传 Motion_StartArc） */
    /* 安全：段完成超时兜底（route 唯一拥有的段级 liveness 保护） */
    uint32_t timeout_ms;        /* >0 = 该段自进入起超过此时长仍未完成 → FAULT + 确定性停车；
                                   ==0 = 禁用段级超时（该段仅靠自然完成 / line_follow LOST /
                                   Route_Stop 终止；FOLLOW_UNTIL 仍有 line_follow 自身丢线超时兜底，
                                   motion 段若失速 [S06 §15.5] 则可能永不完成——见 §20.3 安全注释） */
} Route_Segment_T;

/** 服务状态。 */
typedef enum {
    ROUTE_IDLE = 0,   /* 未运行 / 已停：route 不推任何子服务，底盘静默（刹车真值表保持） */
    ROUTE_RUNNING,    /* 执行某段中 */
    ROUTE_DONE,       /* 全部段完成：末段已确定性停车，静默 */
    ROUTE_FAULT,      /* 段失败（line_follow LOST / 段超时 / 段启动被拒）：已确定性停车，静默 */
} Route_State;

/** 一次性读出的服务遥测。 */
typedef struct {
    Route_State   state;
    uint8_t       segment_index;  /* 当前（RUNNING）或最后处理（DONE/FAULT）的段索引 */
    uint8_t       segment_count;  /* 段表长度 */
    Route_SegKind current_kind;   /* segment_index 段的类型（count==0 时无意义） */
} Route_Telemetry_T;

void Route_Setup(const Route_Segment_T *segments, uint8_t count);
    /* 登记段表（调用方保证表生命周期覆盖使用期）+ 复位为 IDLE。segments==NULL 或 count==0 →
     * 合法空表（Route_Start 立即 DONE）。不触碰任何硬件/子服务。
     * 前置：装配层已 LineFollow_Init + Motion_Init（各带有效标定 cfg）。 */
void Route_Start(void);
    /* 匹配 scheduler on_enter 签名。空表 → state=DONE（trivially 完成）。非空 → cur=0、
     * 标记首段待进入、state=RUNNING；本调用不驱动底盘（驱动始于首个 Route_Update）。 */
void Route_Update(uint32_t now_ms);
    /* 匹配 scheduler on_step 签名。见 §20.3 段状态机。RUNNING 期每拍至多推进一个子服务；
     * IDLE/DONE/FAULT 完全静默（不推任何子服务，刹车真值表保持）。 */
void Route_Stop(void);
    /* 匹配 scheduler on_exit 签名。任意态 → 停当前活动子服务（FOLLOW_UNTIL:LineFollow_Stop /
     * motion 段:Motion_Stop）→ state=IDLE。确定性停止（§8.1），随时可从正常控制流调用。 */
Route_State Route_GetState(void);
bool Route_IsDone(void);                          /* state==ROUTE_DONE */
void Route_GetTelemetry(Route_Telemetry_T *out);  /* out==NULL 无副作用 */
```

- **钩子签名兼容**：`Route_Start`(void)/`Route_Update`(uint32_t)/`Route_Stop`(void) 恰配
  `Scheduler_Entry_T` 的 `on_enter`/`on_step`/`on_exit`——T01 可把 route 直接注册为一个运行条目，
  无需适配器。`now_ms` 是**实用参数**（段级超时基准，非 Menu_Tick 那样的预留位）。
- **头不暴露 Driver/下层类型**（§3.4）：公共面只用自持枚举/结构 + `LineFollow_Element` 位号
  （Service 层类型，合法）；不出现 `Motion_*`/`Chassis_*`/`Encoder_*` 类型。段参数用 route 自持字段，
  由 route.c 透传给对应 `Motion_StartXxx`。

### 20.3 段状态机与交接语义（转移表随 .c 注释交付）

`Route_Update(now_ms)` 在 `RUNNING` 态每拍逻辑（`seg = &segments[cur]`）：

1. **段尚未进入**（`seg_entered==false`）——只做进入，不驱动、不判完成，本拍即返回：
   - FOLLOW_UNTIL：`LineFollow_Start()`；返回 false（line_follow 配置无效）→ `abort_fault`。
   - STRAIGHT/TURN/ARC：**先 odometry catch-up**——调一次 `Motion_Update()`（此刻 motion 处 IDLE/DONE，
     按 motion.h:124 只排空 IMU + 推进 odometry 刷新位姿/航向，**不驱动底盘**），使随后 `Start` 捕获的
     起点/航向基准反映 FOLLOW_UNTIL 期的真实位姿（FOLLOW_UNTIL 期无人推进 odometry 而冻结，若不
     catch-up 则 `heading0` 陈旧 → motion 首拍产生幻纠偏差速）。再 dispatch `Motion_StartStraight/
     StartTurn/StartArc`（透传段参数）；返回 false（段参数非法：distance≤0 / turn_deg==0 /
     radius<track/2 / arc_deg==0）→ `abort_fault`。
   - 记 `seg_deadline_base = now_ms`；`seg_entered=true`；**本拍返回**（进入拍不驱动——保证进入拍只触碰
     这一个子服务，且与上一段完成拍隔一拍刹停间隙，交接确定性无正负跳变震荡，§8.1 换向经停）。
2. **段已进入**——推进 + 判完成/失活：
   - 推进当前段**唯一**子服务：FOLLOW_UNTIL → `LineFollow_Update()`；motion 段 → `Motion_Update()`。
   - **完成检查**（推进后才成立）：
     - FOLLOW_UNTIL：`events = LineFollow_PollElementEvents()`；`(events & seg.until_elements)!=0`
       → 段完成。
     - motion 段：`Motion_IsDone()` → 段完成。
     - 段完成 → `finish_segment`：FOLLOW_UNTIL 调 `LineFollow_Stop()`（→IDLE+Chassis_Stop，刹车保持）；
       motion 段已 DONE+Chassis_Stop+静默（无需额外动作）。随后 `cur++`、`seg_entered=false`；
       `cur>=count` → `state=DONE`（末段已刹停），否则下一拍进入下一段（隔拍刹停间隙）。
   - **失活/超时检查**（仅当本拍未完成）：
     - FOLLOW_UNTIL 且 `LineFollow_GetState()==LINE_FOLLOW_LOST`（丢线恢复超时）→ `abort_fault`。
     - `timeout_ms>0` 且 `(uint32_t)(now_ms − seg_deadline_base) >= timeout_ms` → `abort_fault`。

`abort_fault`：停当前活动子服务（FOLLOW_UNTIL:`LineFollow_Stop` / motion 段:`Motion_Stop`——motion 可能
仍在驱动，必须显式刹停；进入即被拒的段无活动子服务，底盘本就静默）→ `state=FAULT`，此后 Update 静默。

`IDLE/DONE/FAULT` 态：`Route_Update` 完全静默——**不推任何子服务**（刹车真值表保持，确定性驻停，
S06/S02 修订 1 同款显示/驱动所有权隔离）。

- **安全注释（§8.1，软件可证）**：init-to-safe = Setup/Start 不驱动、进入拍不驱动（底盘持上一段刹停态）；
  确定性停止 = Route_Stop 任意态刹停、段失败恒 Chassis_Stop（经子服务）；超时兜底 = 段级 timeout_ms
  是 route 唯一拥有的 liveness 保护（motion 失速 [S06 §15.5] 的编排层兜底落点）；换向经停 = 段间隔拍
  刹停间隙避免两驱动源同拍并发。**`timeout_ms==0` 的取舍**：禁用段级超时后，FOLLOW_UNTIL 仍有
  line_follow 自身丢线超时兜底，但 motion 段失速将永不完成——此时须靠 Route_Stop 外部干预；装配层
  对 motion 段建议设 timeout_ms>0（本契约不强制默认值，避免无实测依据的臆造上限，§8.3）。

### 20.4 数据链与单一所有者声明（§8.2）

接线后一拍数据流（`Route_Update` RUNNING 段已进入路径）：

```text
Route_Update(now_ms)
  ├─ FOLLOW_UNTIL 段：LineFollow_Update() ── 唯一采样/循迹/元素/速度调制/内环级联（line_follow 既有链，不变）
  │      └─ LineFollow_PollElementEvents() → 元素上升沿事件掩码 → &until_elements → 段完成判定
  └─ motion 段：Motion_Update() ── 唯一 IMU 排空 + 里程计消费 + 语义运动 + 内环级联（motion 既有链，不变）
         └─ Motion_IsDone() → 段完成判定
进 motion 段前：Motion_Update()（IDLE catch-up，仅刷新 odometry/航向，不驱动）→ Motion_StartXxx 捕获真实基准
```

- **完成触发源单一所有者**：元素事件唯一来自 `LineFollow_PollElementEvents`（track_elements 经 line_follow，
  V24 已锁位序，route 不复算、不 include track_elements.h）；motion 到位唯一来自 `Motion_IsDone`
  （odometry 里程/航向判据在 motion，route 不复算距离/角度）。route 只做「事件掩码按位与 / 布尔查询」
  的编排判定，非任何数据变换。
- **段级超时**：唯一以 route 自持 `seg_deadline_base` + 入参 `now_ms` 无符号减法计（同 chassis/hmi/
  scheduler 门控口径），不新开时间源（route 无 `clock.h`，时间经钩子参数注入，同 SCH01 Q1 定案）。
- **不复做**（一律经既有所有者，§20.0 已列）：脉冲→距离 = odometry cfg；yaw 符号/unwrap = odometry/
  heading；输出限幅/slew/换向/超时/刹车 = motor.c（经 chassis）；差速/基速/元素几何 = line_follow 子链。
  route **新增唯一拥有**：段序 + 完成分派 + 交接 + 段级超时。
- **头不暴露 Driver 类型**（§3.4）：新增面只用 `float`/`bool`/`uint*`/自持枚举/`LineFollow_Element` 位号。

### 20.5 preserved_behavior

- `app/service/{line_follow,motion,chassis,tuning,hmi}/**`、旧 `app/**`、`driver/**`、`middleware/**`
  零改动；主机既有 310 用例全过；固件行为不变（route.o 进链接但**零调用者**——T01 未写，上层不泵
  `Route_Update` 则不产生任何底盘命令，V07 同款过渡态）。

### 20.6 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/\|app/scheduler/\|app/ui/\|app/system/\|middleware/\|driver/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/service/route`，`#include` 行） | 0 命中（`line_follow.h`/`motion.h` 同层 Service、`<stdint.h>`/`<stdbool.h>`/`<stddef.h>` 不在告警集；尤其 `app/scheduler/` 与 `middleware/track_elements` 零命中＝矩阵关键项） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §20.1 | 无 allowed_files 之外的改动（尤其 `line_follow/**`、`motion/**`、`chassis/**` 零改） |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥326 PASS / 0 FAIL（310 基线 + ≥16 新用例）。必含——**生命周期/静默**：Setup+Start 前及 IDLE/DONE/FAULT 态 Update 不推子服务（零新电机命令刷新、刹车保持）；空表 Start→DONE+IsDone；Start 本身不驱动（进入拍无底盘命令）。**FOLLOW_UNTIL 段**：注入位图使目标元素连续 confirm_ticks 拍 → 命中 `until_elements` → 段完成+LineFollow_Stop 刹停+推进下一段；未命中不推进；`until_elements` OR 语义（多目标位任一命中即完成）；line_follow 丢线超时 LOST → ROUTE_FAULT+确定性刹停。**motion 段**：STRAIGHT/TURN/ARC 各注入编码器/IMU 使 `Motion_IsDone` → 推进；段参数非法（distance≤0 / turn=0 / R<track/2 / arc=0）→ 段启动被拒 → FAULT+静默。**进 motion 段 catch-up**：FOLLOW_UNTIL（odometry 冻结）期注入 IMU 航向漂移后进 STRAIGHT heading_hold —— 首个驱动拍不产生幻纠偏差速（heading0=catch-up 后真实航向）。**段间确定性交接**：FOLLOW_UNTIL→TURN 混合序，段 N 完成拍刹停、隔一拍再驱动段 N+1（进入拍无底盘命令）；交接无正负跳变。**每拍单子服务/无双泵**：任一拍 line_follow 与 motion 不同时被推进（Chassis_Update 驱动源≤1）。**段级超时**：timeout_ms>0 段超时未完成 → FAULT+确定性刹停；timeout_ms==0 不触发超时（自然完成路径不受影响）。**Route_Stop**：RUNNING 中（FOLLOW_UNTIL / motion 段）→ 停当前子服务+Chassis_Stop+IDLE，此后静默。**遥测**：state/segment_index/segment_count/current_kind 一致 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、route.o 经 linkInfo.xml 确证进入 .out 链接 |

- **主机测试链接组成**（事实登记，= line_follow 与 motion 两测试链接组成之并集）：test_route = 真实
  `route.c` + `line_follow.c` + `lost_line.c` + `motion.c` + `chassis.c` + `odometry.c` + `heading.c`
  + `track_error.c` + `track_elements.c` + `speed_plan.c` + `pid.c` + `encoder.c` + `motor.c` + `gray.c`
  + `imu.c` + `board_uart/{imu,vision,vofa,stepmotor}_uart.c` + fake 端口（`fake_gray_port` /
  `fake_board_gpio` / `fake_motor_hw` / `fake_uart_port` / `fake_clock`）。**链 `fake_clock.c` 不链
  `fake_i2c_port.c`**（route 路径无 OLED/I2C，避免 `Clock_NowMs` 重定义——S04/M01/motion 已证坑）。
  只 fake 端口/硬件边界，不 fake 被测 Service/Middleware/Driver 本体。`chassis.c`/`encoder.c`/`motor.c`/
  `pid.c` 两子树共用同一真实 .o，单次链接无符号冲突。

### 20.7 契约修订记录

- **冻结初版**（本提交）。审计后处置若有，单独提交并在此追加修订原因 + 提交哈希（契约先于代码，
  契约行有错在单独显式提交中修订，绝不与满足它的代码同一提交）。
- **收工闭环待办登记**（TDD/施工会话执行，非本契约冻结范围）：施工完成后 topo-updater 须在拓扑
  §6/§7 登记——V21 扩条（route 每拍≤1 子服务推进，非新增第四 Chassis_Update 推进点）、V23（route 经
  motion 间接排空 IMU，非第二排空点）、route 新增所有权（段序状态机 + 完成分派 + 段间交接 + 段级超时）；
  并在 §3 覆盖清单登记 route 服务、§10 追加日志。
- **收官（2026-07-18，代码 6cb338c）**：契约**零修订**——arch-auditor 六轴无发现，route.c 与
  §20.0/§20.3/§20.4 逐条一致（亲验 motion.c `MOTION_IDLE/DONE` 分支 drive-free，证实进 motion 段前
  catch-up 不破坏单一推进不变量）。四证据行复现：E01 依赖纯净 0 命中 / E02 范围无越界
  （改 route.{h,c}、test_route.c、tests/host/Makefile、Debug/makefile、.gitignore，本地 subdir_*.mk 未入库）/
  E03 334 PASS 0 FAIL（310 基线+24）/ E04 固件 exit 0、0 诊断、route.o 经 linkInfo.xml 确证进链（3 引用）。
  test_route.c 一处断言在施工中修正（STRAIGHT(0) 被拒后 abort_fault 恒经子服务 Chassis_Stop 发刹车命令，
  写计数≠0——原「静默＝零写」断言误解，改判 FAULT+刹车生效，属证据行内实现修正非契约行修订）。
  topo-updater 已同步：索引 §6（V21 扩条 + V23 补注）、§7（Route 覆盖行）、§10 日志，app.md §3
  （Route_API 类块 + 2 条出边到 line_follow/motion）。

## 21. S05 契约群（云台/视觉服务群，三闭环）——§21.1 S05a 冻结

> S05 体量最大，按 §2.6 与用户裁定「契约时拆分」为三个独立闭环，各自走完整
> embedded-closed-loop（契约冻结 → TDD → 施工 → 证据 → 审计 → 拓扑 → 收官）。
> 本提交只冻结 **§21.0 共享协议定案** + **§21.1 S05a Driver 契约**；S05b/S05c 在各自闭环冻结，
> 不预支其内部设计。

### 21.0 协议定案（用户 2026-07-18 三轮裁定，Q7 closed）——所有三闭环共享事实

视觉链路 = 单条物理 UART `UART_VISION`（board.syscfg：UART1 外设，PA8 tx / PA9 rx，230400 8N1，
收发均 DMA——**TX 通道已在 syscfg provision，本阶段不改 syscfg**）。链路上并行**两条协议**，
按首字节区分，方向与用途不同（**非二选一**）：

- **控制/坐标帧（视觉 → 主控，RX，运行期）**：`0xAA 0x55` + 长度(1B) + payload + CRC16-MODBUS(2B 小端)。
  - CRC 范围 = **长度字节 + payload**（承 `docs/通信协议.md` 包二）。
  - payload = `[cmd(1B)] + 数据`；坐标帧 `cmd=0x01` + x:float32 小端(4B) + y:float32 小端(4B)，故 len=9、整帧 14B。
  - **坐标是 float32**（非冻结代码的 int16）；Middleware（S05b）按浮点像素坐标做映射。
  - 运行期视觉**只发这一种包**（用户不变量：不混包）；同帧框架未来若载 `0x02` 目标状态属 T01 赛题事，
    S05a 按 CRC 通用分帧、未知 cmd 校验通过后静默丢弃，不预建 0x02 处理。
- **选题/握手帧（双向，setup 期，非运行期）**：`0xFF` + 主任务号(1B) + 子任务号(1B) + `0xFE`（无校验，定长 4B，中间不带数据字段）。
  - 主控 → 视觉：下发主/子任务号选题（告诉视觉用哪套算法）。
  - 视觉 → 主控：**同格式回一帧确认**（回显任务号）。主控收到确认帧才执行题目运行——防止视觉解析失败而小车盲跑。
- **两条硬不变量（用户裁定，写入 S05c 编排约束）**：① 运行期只允许视觉发一种数据包，不允许混包；
  ② 运行期主控**绝不**向视觉要回馈帧（高精度云台运行时海量坐标流里夹几帧回馈帧难以成功解析）——
  故 0xFF 握手严格限 setup 期，运行期 TX 静默。
- **与 VOFA 隔离（用户提示 2026-07-18）**：VOFA 是调试链路（uart_vofa，归 S03 tuning）；视觉**另开独立 bus**
  `driver/uart_vision`，不复用 uart_vofa、不走 vofa_register。
- **归属（照搬 Q2/Q7 先例）**：帧编解码归 Driver（`driver/uart_vision`，坐落于 `vision_uart` 字节层之上，
  Driver→Driver 同层受控）；坐标→轴映射归 Middleware（S05b）；选题握手 + 收敛 + odometry 前馈归 Service（S05c）。
- **过时件处置**：冻结的 `app/tasks/platform_2d/{vision_bus,vision_coord}.*`（0x55AA + 8 位和校验 + int16）
  判为过时，S05a **全部重写不照搬**；旧文件继续冻结（arch-baseline 第 19–21 行登记），随 T01 删除。

### 21.1 S05a 契约（`driver/uart_vision` 视觉链路编解码 Driver）——冻结

- **task_id**: S05a-uart_vision
- **goal**: 新建 `driver/uart_vision/`：`UART_VISION` 之上的**视觉协议编解码 Driver**——RX 侧自同步分帧
  解析 `0xAA 0x55` 坐标控制帧（CRC16-MODBUS 校验 → float32 坐标）与 `0xFF` 选题确认帧；TX 侧组 `0xFF`
  选题帧下发。为支持握手 TX，**给 `driver/board_uart/vision_uart` 增补 TX 字节搬运**（镜像 stepmotor_uart：
  `TryWrite`/`IsTxIdle`/`ConsumeTxDone`；board.syscfg 已 provision UART_VISION DMA TX）。零调用者是预期状态
  （S05c 未写前）。
- **接口辩护**（视觉链路能做什么）：主控能取到视觉最新目标坐标（float32，带单调序号供判时效）、
  能向视觉下发选题（主/子任务号）、能查询视觉是否已回选题确认帧。仅此四类成为公共面。
- **设计定案（自同步分帧，不引 Clock）**：长度前缀 + CRC16 + cmd/len 白名单校验使分帧**确定性自恢复**
  （CRC 失败/未知 cmd → 丢一字节重扫），故**不设逐字节超时、不依赖 `Clock`**——codec 保持纯字节→结构体
  变换（比冻结 vision_bus 的 20ms 超时更简、更可测）。时效判定归 Service（消费 seq 变化），codec 不持墙钟。

#### 21.1.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/driver/uart_vision/uart_vision.h` / `.c` | 新建（视觉协议编解码：RX 分帧 + TX 组帧） |
| `hc-team/driver/board_uart/vision_uart.h` | 修改（增 TX API：`VisionUart_TryWrite`/`IsTxIdle`/`ConsumeTxDone`） |
| `hc-team/driver/board_uart/vision_uart.c` | 修改（增 TX 字节搬运 + HOST_TEST 注入/抓取钩子，镜像 stepmotor_uart.c） |
| `hc-team/driver/mspm0_runtime/mspm0_runtime.c` | **修改（修订 1，见 §21.1.5）**：DMA VISION_TX 完成分发分支补 `VisionUart_IsrTxDone()` 回调 + 前向声明——TX 完成 ISR 接线是 vision_uart TX 完整性的一部分 |
| `tests/host/test_uart_vision.c` | 新建 |
| `tests/host/fake_uart_port.c` | 修改（增 `FakeUartPort_CompleteVisionTx`/`CopyVisionTx` + 声明 Vision TX 测试钩子） |
| `tests/host/Makefile` | 追加 test_uart_vision 目标/clean/.PHONY + `UART_VISION_DRIVER_SRC` |
| `.gitignore` | 追加 test_uart_vision / test_uart_vision.exe |
| `Debug/makefile` | 登记 uart_vision.o（ORDERED_OBJS、两处 -include、clean）；vision_uart.o 已登记 |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |

forbidden_files：`hc-team/app/**` 全部、`hc-team/middleware/**` 全部、`hc-team/driver/**` 其余
（含 `uart_vofa/**`、`step_motor/**`、其它 `board_uart/*`）、`board.syscfg`、tests/host 既有 `test_*.c`
与其它 `fake_*.c`。（Debug/ 下本地生成物不入库，不列。）

#### 21.1.2 公共接口（最小面）

```c
/* driver/uart_vision/uart_vision.h —— 视觉协议编解码，坐落 vision_uart 字节层之上 */
typedef struct { float x; float y; } UartVision_Coord_T;   /* 视觉像素坐标，float32，视觉坐标系原样透传 */

void UartVision_Init(void);   /* 清编解码状态 + VisionUart_Init；不发任何帧 */
void UartVision_Poll(void);   /* 任务态：drain VisionUart_Read → 自同步分帧 → 更新坐标/确认缓存 */

/* 坐标控制帧（RX，运行期）——单一所有者：坐标 float32 解析只在此 */
bool     UartVision_GetLatestCoord(UartVision_Coord_T *out); /* 收到过坐标帧→true+写出；否则 false */
uint32_t UartVision_GetCoordSeq(void);                       /* 单调递增序号（每成功坐标帧 +1）；Service 判时效 */

/* 选题/握手帧（setup 期）——TX 组帧 + RX 确认解析 */
bool     UartVision_SendTopic(uint8_t main_task, uint8_t sub_task); /* 组 0xFF 帧经 VisionUart_TryWrite 下发；TX 忙→false */
bool     UartVision_GetTopicAck(uint8_t *main_task, uint8_t *sub_task); /* 收到确认帧→true+回显任务号 */
uint32_t UartVision_GetTopicAckSeq(void);                    /* 确认帧单调序号；Service 判「本轮选题是否已确认」 */
```

- **单一所有者声明**：视觉帧框架/同步字/CRC16/分帧缓冲/float32 拆包全部唯一在 `uart_vision.c`；
  `vision_uart` 只做字节搬运（RX FIFO + 新增 TX），不认帧；坐标时效判定归 S05c（消费 seq），codec 不持墙钟；
  坐标→轴映射归 S05b，本层坐标**原样透传不做单位/极性变换**（不预支 Middleware 职责）。
- **`vision_uart` TX 增补**：镜像 `stepmotor_uart` 语义——`TryWrite(data,len)` 提交一帧（TX 忙→false），
  `IsTxIdle()`/`ConsumeTxDone()` 暴露事实不做策略。固件侧 DMA TX 走 UART_VISION 既有 syscfg 通道；
  HOST_TEST 侧提供 `VisionUart_TestCompleteTx`/`VisionUart_TestCopyLastTx`（镜像既有 stepmotor/vofa 钩子）。
- **前置条件**：System 装配层已 `UartVision_Init`（含 VisionUart_Init）。本层无电机/功率路径，无 §8.1 安全项。

#### 21.1.3 preserved_behavior

- 其余 `driver/**`（除 vision_uart 增 TX）、`app/**`、`middleware/**` 零改动；主机既有 334 用例全过；
  固件行为不变（uart_vision.o 进链接但零调用者；vision_uart 增 TX 但旧 RX 路径与既有 4 个 uart_fifo/vofa_rx/
  stepmotor_uart/imu 测试的既有断言不变——TX 为纯新增面）。

#### 21.1.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `hc-team/app/\|hc-team/middleware/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/driver/uart_vision`，`#include` 行） | 0 命中（仅 `driver/board_uart/vision_uart.h` + `<stdint/stdbool/string>`；Driver→上层/HAL-config 零命中＝矩阵关键项。vision_uart.c 固件段含 `ti_msp_dl_config` 属既有 board_uart 层，不在本 path） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §21.1.1 | 无 allowed_files 之外的改动（尤其 `app/**`、`middleware/**`、`board.syscfg` 零改） |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥346 PASS / 0 FAIL（334 基线 + ≥12 新用例）。必含——**坐标帧 RX**：合法 0xAA55 帧 CRC 正确→坐标 float32 精确解析 + seq+1；CRC 错→拒收、seq 不变；分两次喂（半帧→补齐）成功；前置垃圾字节 + 帧→重同步成功；校验通过但未知 cmd→静默丢弃、无坐标更新；连续两坐标帧 seq 递增且取最新。**选题 TX**：SendTopic 经 VisionUart TryWrite 发出精确 `0xFF main sub 0xFE`（CopyVisionTx 比对）；TX 忙→false 且不发。**确认帧 RX**：喂 0xFF 确认帧→GetTopicAck true+回显任务号 + ackseq+1；无确认→false。**vision_uart TX 面**：TryWrite→IsTxIdle 翻转、CompleteTx→ConsumeTxDone 语义。**Init 静默**：Init 后无 TX、无坐标（GetLatestCoord false） |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、uart_vision.o 与重编的 vision_uart.o 经 linkInfo.xml 确证进入 .out 链接 |

- **主机测试链接组成**（事实登记）：test_uart_vision = 真实 `uart_vision.c` + 真实 `vision_uart.c`（含新 TX）
  + `fake_uart_port.c`（视觉 RX 注入 + TX 抓取/完成钩子）+ test_uart_vision.c。**不链 fake_clock/fake_i2c_port**
  （codec 不依赖 Clock，无 I2C）。既有 uart_fifo/vofa_rx/stepmotor_uart/imu 四测试仍各自链 vision_uart.c，
  TX 为纯新增符号故既有断言不受影响。

#### 21.1.5 契约修订记录

- **冻结初版**（提交 c1d5421）。
- **修订 1（本提交，arch-auditor 阻断 finding 处置——契约行修订，单独提交先于修复代码）**：
  §21.1.1 allowed_files 增补 `hc-team/driver/mspm0_runtime/mspm0_runtime.c`。
  原因：审计**阻断级** finding——`vision_uart` 新增 TX 后，固件 DMA TX 完成分发
  （`mspm0_runtime.c` `RT_DMA_VISION_TX` 分支）只清中断标志、未回调 `VisionUart_IsrTxDone()`
  （stepmotor/vofa 分支均有对应回调），且文件顶部缺该函数前向声明。后果：目标板上
  `s_vision_uart_tx_busy` 首帧后永不清零 → `TryWrite` 恒 false、`IsTxIdle` 恒 false，
  **选题握手在硬件上只能发一帧后死锁**。主机测试因 `FakeUartPort_CompleteVisionTx` 手工调
  `IsrTxDone` 而全绿，掩盖此缺陷（fake 证消费者契约、永不证真实 ISR/DMA 边界——闭环铁律）。
  TX 完成 ISR 接线是「vision_uart 增 TX」本身的完整性组成，原契约漏列该文件属证据盲区，
  故按闭环协议补入 allowed_files 并在此显式登记，修复代码在下一提交。E04 固件构建行不变
  （仍须 exit 0、0 诊断、uart_vision.o + vision_uart.o 进链），修复后重跑确证。
  **测试盲区登记**：host 测试无法覆盖 runtime ISR 接线（无硬件行，§6 决策），此接线正确性
  由「与 stepmotor/vofa 分支逐行同构 + 固件构建」保证，硬件 TX 往返由用户上板验证。

### 21.2 S05b 契约（`middleware/vision_aim` 视觉坐标→轴脉冲映射 Middleware）——冻结

- **task_id**: S05b-vision_aim
- **goal**: 新建 `middleware/vision_aim/`：纯算法 Middleware，把视觉像素坐标（float32 x/y）映射成云台
  X/Y 双轴的**有符号脉冲增量**（int32/轴）。重建（不照搬）冻结遗留 `2DPlatform_LaserStrike.c` 的
  `visionhdl_run_track` / `visionhdl_step_from_error` / `visionhdl_clamp_delta` 几何，成为「死区 / 比例步长 /
  步长限幅 / 极性 / 轴程限幅几何」的**唯一所有者**。零调用者是预期态（S05c 未写前，同 M01/M02/M03 先例）。
- **接口辩护**（该算法能做什么）：给定一帧像素坐标 + 该轴当前累计位置，能算出「本拍该往哪个方向走多少脉冲
  （含死区静止、比例步长、每拍步长封顶、极性、不越轴程软限位）」。仅此一类能力成为公共面。
- **口径定案（topo-navigator 切片 + emm42.h 事实）**：输出**只定成有符号相对脉冲增量（int32/轴）**，
  不定成角度——全仓无「度→脉冲」所有者，emm42 协议最终单位就是 pulses+dir+rpm。方向/幅值拆分（dir+magnitude）
  归 S05c，本层只出带符号 int32。
- **纯算法 + 单一所有者定案**：
  1. **纯函数，不持墙钟、不持物理位置状态**：`VisionAim_Map` 是 (coord, cur_pulse, config) 的纯函数；
     轴的**累计物理位置状态归调用方（S05c）持有并逐拍传入**——vision_aim 只拥有「轴程限幅的几何公式」，
     不拥有「当前位置这个数」。避免旧实现里 `s_pos_*_pulse` 全局积分器造成的「第二位置所有者」。
  2. **误差符号 = 视觉坐标系原始方向**：`error_px = coord - center`（float，**不截断**——修旧代码先 `(int32)coord`
     再减的早失精度 bug，新协议坐标本就是 float32）。极性反转是**唯一**开关 `sign[axis]`（±1），机械/坐标系
     装反在此吸收（同 encoder `s_direction_sign[]` 先例）；任何第二处极性反转都是违规。
  3. **越死区最小步长 = 1 脉冲**（承旧 `step_f` floor 1）：`|error|<=deadband → delta=0/active=false`；
     越死区 → `|delta| ∈ [1, max_step_pulse]`，杜绝「触发但不动」的卡滞。
  4. **限幅两级、各自单一所有者**：步长限幅 `max_step_pulse`（每拍幅值封顶，纯几何）+ 轴程限幅
     `travel_limit_pulse`（±绝对位置软限位，依 cur_pulse，`<=0` 表示不限幅）——两者都在 vision_aim，
     不复用 `Pid_T.out_limit`（那含积分状态，语义不等价，topo-navigator 明确不建议复用）。
- **重建不照搬的存量事实**（冻结遗留，随 T01 删除）：`2DPlatform_LaserStrike.c` 硬编码 CENTER 320/240、
  DEADBAND 6、kp 0.15、MAX_STEP 48、LIMIT 400/800、SIGN +1，且 `s_pos_*_pulse` 有状态积分器 + 直调
  `Emm42_MoveRelative`（经 App 层遗留 `stepmotor_bus.c`）。新 vision_aim 把上述常量全部提为 `VisionAim_Config_T`
  字段（调用方给），只保留纯几何，绝不触碰电机/总线/VOFA。

#### 21.2.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/middleware/vision_aim/vision_aim.h` / `.c` | 新建（纯几何映射） |
| `tests/host/test_vision_aim.c` | 新建 |
| `tests/host/Makefile` | 追加 test_vision_aim 目标/clean/.PHONY + `VISION_AIM_SRC`/`VISION_AIM_TEST_SRC` |
| `.gitignore` | 追加 test_vision_aim / test_vision_aim.exe |
| `Debug/makefile` | 登记 vision_aim.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |

forbidden_files：`hc-team/app/**` 全部、`hc-team/driver/**` 全部（含 `uart_vision/**`——Middleware 禁 include
Driver，坐标由 S05c 拆包喂浮点）、`hc-team/middleware/**` 其余全部、`board.syscfg`、tests/host 既有 `test_*.c`
与全部 `fake_*.c`（纯算法无端口，不链任何 fake）。（Debug/ 下本地生成物不入库，不列。）

#### 21.2.2 公共接口（最小面）

```c
/* middleware/vision_aim/vision_aim.h —— 像素误差 → 轴脉冲增量 纯几何映射 */
typedef enum { VISION_AIM_AXIS_X = 0, VISION_AIM_AXIS_Y, VISION_AIM_AXIS_COUNT } VisionAim_Axis;

typedef struct {
    float   center_px[VISION_AIM_AXIS_COUNT];       /* 图像中心像素（视觉分辨率决定，调用方给） */
    float   deadband_px[VISION_AIM_AXIS_COUNT];     /* 逐轴死区半径(>=0)；|误差|<=死区→不动 */
    float   kp[VISION_AIM_AXIS_COUNT];              /* 比例增益 像素→脉冲(>=0) */
    int32_t max_step_pulse[VISION_AIM_AXIS_COUNT];  /* 每拍步长幅值上限(>0)；越死区后 |delta|∈[1,max_step] */
    int8_t  sign[VISION_AIM_AXIS_COUNT];            /* 极性唯一开关(+1/-1)；机械/坐标系装反在此吸收 */
    int32_t travel_limit_pulse[VISION_AIM_AXIS_COUNT]; /* 轴程绝对软限位 ±值；<=0 表示不限幅 */
} VisionAim_Config_T;

typedef struct {
    float   error_px[VISION_AIM_AXIS_COUNT];        /* 坐标-中心(float,不截断,符号=视觉坐标系原始方向) */
    bool    active[VISION_AIM_AXIS_COUNT];          /* 是否越死区(true=本拍产生运动) */
    int32_t delta_pulse[VISION_AIM_AXIS_COUNT];     /* 本拍有符号脉冲增量(已含极性+步长限幅+轴程限幅) */
} VisionAim_Result_T;

void VisionAim_Init(const VisionAim_Config_T *config);  /* 拷贝配置到私有 static；无副作用；NULL→忽略 */
void VisionAim_Map(float coord_x, float coord_y,
                   int32_t cur_x_pulse, int32_t cur_y_pulse,
                   VisionAim_Result_T *out);
    /* 纯函数。逐轴：误差=坐标-中心 → 死区门控 → |误差|*kp(floor 1, clamp max_step) → 极性 sign
     *   → 轴程限幅(依 cur_pulse，令 cur+delta 不越 ±travel_limit) → delta_pulse。
     * out=NULL 或未 Init → 不写出。coord 为视觉坐标原样(S05a 透传)，本层不做单位换算。 */
```

- **单一所有者声明**：死区 / 比例步长 / 步长限幅 / 极性 / 轴程限幅几何 = vision_aim 唯一所有者；
  轴累计物理位置**状态**归调用方（S05c）；坐标编解码归 `driver/uart_vision`（S05a）；脉冲→dir/magnitude 拆分
  与电机执行归 S05c/emm42。本层不含 Clock、不含 Driver/App/HAL 依赖、不做时效判定（seq 消费归 S05c）。
- **前置条件**：无。纯算法，调用方负责 `VisionAim_Init` 后再 `Map`；未 Init 调用 Map 视为无配置（不写出）。

#### 21.2.3 preserved_behavior

- `driver/**`、`app/**`、`middleware/**` 其余零改动；主机既有 361 用例全过；固件行为不变
  （vision_aim.o 进链接但零调用者）。

#### 21.2.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `hc-team/driver/\|hc-team/app/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/middleware/vision_aim`，`#include` 行） | 0 命中（仅 `<stdint/stdbool>` 与自身头；Middleware 禁 Driver/App/HAL＝矩阵关键项） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §21.2.1 | 无 allowed_files 之外的改动（尤其 `app/**`、`driver/**`、`board.syscfg` 零改） |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥376 PASS / 0 FAIL（361 基线 + ≥15 新用例）。必含——**死区**：\|误差\|<=死区→delta 0+active false；恰越死区→\|delta\|>=1（floor 1，杜绝触发不动）。**比例+步长限幅**：中等误差 delta≈round(\|误差\|*kp)、大误差被 max_step 封顶。**极性**：sign=+1 与 -1 对同一误差得反号 delta；误差符号=coord-center（右/下为正）。**轴程限幅**：cur 近 +limit 时正向 delta 被截到 limit-cur、近 -limit 时负向截到 -limit-cur、travel_limit<=0 时不截。**双轴独立**：X/Y 各用各的 config，互不串。**float 精度**：亚像素中心（如 center=319.5）不被截断、误差保留小数。**Init 前/NULL out**：Map 不写出、不崩。 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、vision_aim.o 经 linkInfo.xml 确证进入 .out 链接 |

- **主机测试链接组成**（事实登记）：test_vision_aim = 真实 `vision_aim.c` + test_vision_aim.c，**不链任何 fake、不链 -lm 外依赖**
  （纯算法，无端口/无 Clock/无 Driver，同 track_error/track_elements/speed_plan 先例）。

#### 21.2.5 契约修订记录

- **冻结初版**（提交 8b74cf0）。
- **审计处置（本完成提交，arch-auditor 无阻断/无重要，2 建议级——仅文档收敛，零行为改动）**：
  - F1（`active` 双语义）：头注释把 `active` 收敛为单一语义「误差是否在死区外」，并显式声明
    「轴程饱和拍 active 可为 true 而 delta 被限幅为 0，是否真的位移由调用方以 `delta_pulse!=0` 判断」。
    不改行为（`active` 仍在死区门控处置位，先于轴程限幅）——`active`=越死区 与 `delta_pulse!=0`=本拍位移
    是两个正交信号，供 S05c 分别使用。
  - F2（越界 `cur` 回拉幅值可超 `max_step`）：`vision_aim_clamp_to_travel` 注释注明「回拉增量幅值可超过
    每拍步长封顶，属有意的安全方向优先；常态闭环 faithful 回灌 cur+delta 永在界内、不走此分支」。
    **不加再夹 max_step 的防御**——该分支仅当调用方喂入越界 `cur`（运行中调小 travel_limit / 位置种子越界）
    才可达，属无实测失败模型的场景，按嵌入式基线不加无依据防御代码；回拉方向朝安全区，非失控危险。
  - 两项均为注释级修订，不动任何证据行/allowed_files/公共接口签名，故并入完成提交（同 S06 §15.5 先例），
    不单开契约修订提交。E03/E04 因仅改注释不受影响（vision_aim 主机测试完成后复跑确证仍 16 全过）。

### 21.3 S05c 契约（`app/service/gimbal` 云台视觉瞄准服务）——冻结

- **task_id**: S05c-gimbal
- **goal**: 新建 `app/service/gimbal/`：视觉三闭环最后一环，把 S05a `uart_vision`（坐标/握手编解码）
  与 S05b `vision_aim`（像素误差→轴脉冲增量几何）接线成一条完整链路——**选题握手编排** + **像素瞄准
  收敛环** + **确定性安全停**，并把步进总线**下沉为 Service→Driver 直连**（`emm42` 组包 + `stepmotor_uart`
  字节层），不再依赖冻结的 App Task `stepmotor_bus.c`。零调用者是预期态（T01 未写前，同 S05a/b 先例）。
- **接口辩护**（云台能做什么）：能向视觉下发选题并等确认帧后才动（防盲跑）、能按视觉最新目标坐标闭环
  驱动 X/Y 双轴步进收敛到目标、能在坐标失联/握手超时/被叫停时确定性安全停、能报告状态与轴位姿遥测。
  仅此四类成为公共面。
- **设计定案（用户 2026-07-18 双裁定，见 §5 Q7 延伸）**：
  1. **步进总线下沉 = Service 直连最小派发**（用户选定）：gimbal 直接用 `driver/step_motor/emm42`（组包）
     + `driver/board_uart/stepmotor_uart`（字节层）自持**最小 TX 派发 / 脉冲→dir+magnitude 拆分 / 换向 /
     安全停**；**不复刻** legacy `stepmotor_bus.c` 的 mgmt 队列 / RR 仲裁 / 0x35 读速度应答（那是给调试
     profile 的，瞄准环不需要）。**严禁 include `app/tasks/platform_2d/stepmotor_bus.h`**（跨 task 目录 +
     待删存量债，topo-navigator 明示的违规路径）。Driver 边界不破：字节/DMA 归 stepmotor_uart，协议帧归
     emm42，gimbal 只决定「发什么 / 何时发」= 合法 Service 控制编排。
  2. **odometry 运动前馈本轮只留读点 + 文档，几何推迟**（用户选定，Q10 模式）：仓库无「目标世界位置/
     测距」模型（测距硬件 Q8 未定案），全量视差前馈几何无法定义。本轮交付握手+像素闭环+安全停；前馈
     只在契约与 gimbal.c 注释**登记** `Odometry_GetPose()` 的预期接入点（AIMING 拍读位姿→折入瞄准），
     **本轮不 include `odometry.h`、不发起该调用**（否则是弃值死代码 + 未用依赖，违嵌入式基线）。几何等
     有目标世界模型时以契约修订补入（拓扑 §5.2 已有 gimbal 前馈虚线占位，V22 补注为届时检查点）。
- **数据链（§8.2，逐值确认）**：
  ```text
  视觉像素坐标 float32 + 单调 seq（uart_vision RX，坐标系原样）
    → gimbal 消费 seq 判时效（连续无进展达 coord_timeout_ms → 停）  [时效唯一所有者=gimbal]
    → VisionAim_Map(coord, cur_pulse, cfg.aim) → 每轴有符号 delta_pulse   [死区/kp/步长/极性/轴程几何唯一所有者=vision_aim，gimbal 不复算]
    → gimbal：delta≠0 且总线空 → 下发；成功后 cur_pulse += delta          [轴累计位置状态唯一所有者=gimbal，仅此一个累加点]
    → gimbal_stepbus：pulses 拆 dir(EMM42_DIR_CW/CCW)+magnitude → emm42 相对位置帧   [脉冲→dir/magnitude 拆分=gimbal_stepbus，承 S05b §21.2 边界]
    → StepmotorUart_TryWrite（字节/DMA）→ EMM42 硬件                       [RPM≤100 夹 + ×10 尺度唯一所有者=emm42.c，gimbal 传裸 speed 不复夹]
  ```
- **单一所有者复查（V26/V22 强制）**：① 坐标死区/比例/步长限幅/极性/轴程限幅几何**只在 vision_aim**，
  gimbal 逐拍把自持的 `cur_pulse` 传入 `VisionAim_Map`、绝不另算第二份（V26）；② 轴累计物理**位置状态**
  只在 gimbal（vision_aim 是纯函数不持位置，S05b §21.2 明文）；③ RPM 限幅+×10 只在 emm42.c；④ 坐标编
  解码/CRC/分帧只在 uart_vision；⑤ odometry 的 `mm_per_pulse`/`heading_sign` 只在 `Odometry_Config_T`，
  本轮不接线故不触碰（V22 补注留待前馈接线时复核）。
- **motor-safety（§8.1，步进链路，逐项）**：
  - **上电/Init 安全态**：`Gimbal_Init` 静默——不发选题、不发移动、**不 enable**（步进保持上电默认失能安全态）。
  - **步长限幅**（每拍幅值封顶）= vision_aim `max_step_pulse`（唯一所有者）；**轴程软限位** = vision_aim
    `travel_limit_pulse`（依 cur_pulse，唯一所有者）——gimbal 不复做。
  - **速度限幅** = emm42 `emm42_clamp_speed_rpm`≤100 + ×10（唯一所有者）；`Emm42_BuildPositionFrame` 加速度
    固定 0（器件事实），gimbal 传 `step_speed_rpm` 裸值。
  - **命令/反馈过期 → 停**：AIMING 期坐标 seq 连续无进展达 `coord_timeout_ms` → `STOPPED` 停止下发
    （gimbal 时效唯一所有者）；HANDSHAKING 期无确认帧达 `ack_timeout_ms` → `STOPPED`。短暂 seq 停顿
    （<超时）保持 AIMING 静默不动（步进保持上一相对移动终点位置），不补发大命令。
  - **换向**：步进经 UART 相对位置命令，方向是每帧 dir 位、EMM42 智能驱动器内部自管加减速；**无共享功率
    桥臂**，H 桥过零死区规则不适用（显式登记：此链路非 PWM/H 桥，无桥臂直通风险）。相对移动每帧幅值有界
    （≤max_step），逐帧到位即停，X→+5 后 X→−3 即两条独立到位命令，无脉冲震荡。
  - **确定性停止接口**：`Gimbal_Stop` 可从正常控制流调用——停止下发、清待发、→STOPPED；步进保持使能
    （保持瞄准位置力矩＝云台的硬件安全态，非失能垂落）。不依赖调试器暂停。
  - **ISR**：gimbal 无自有 ISR；TX/RX 完成中断归 stepmotor_uart（既有、已接线，本轮不改 runtime）。
- **文件层级**（不把全部塞一个文件）：`gimbal.{h,c}` = 公共面 + 握手/ARMING/AIMING 状态机 + 像素闭环 +
  安全停（含 uart_vision/vision_aim/clock）；`gimbal_stepbus.{h,c}` = 服务内私有的最小步进 TX 派发
  （含 emm42/stepmotor_uart，脉冲拆分 + 组包 + 发送 + RX drain/discard），可独立主机测试（同 S02 `lost_line` 先例）。

#### 21.3.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/gimbal/gimbal.h` / `.c` | 新建（公共面 + 状态机 + 像素闭环 + 安全停） |
| `hc-team/app/service/gimbal/gimbal_stepbus.h` / `.c` | 新建（服务内私有：最小步进 TX 派发 + 脉冲拆分 + 组包 + RX drain） |
| `tests/host/test_gimbal_stepbus.c` | 新建 |
| `tests/host/test_gimbal.c` | 新建 |
| `tests/host/Makefile` | 追加 test_gimbal_stepbus / test_gimbal 目标/clean/.PHONY + SRC 变量 |
| `.gitignore` | 追加两个测试产物 |
| `Debug/makefile` | 登记 gimbal.o、gimbal_stepbus.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |

forbidden_files：`hc-team/app/tasks/**`（尤其 `platform_2d/stepmotor_bus.h|c`、`2DPlatform_LaserStrike.*`、
`vision_bus.*`、`vision_coord.*`——全部冻结存量，只可弃用不可依赖）、`hc-team/app/{scheduler,system,ui}/**`、
`hc-team/app/service/**` 其余全部、`hc-team/driver/**`（`uart_vision`/`board_uart/{vision,stepmotor}_uart`/
`step_motor/emm42`/`clock` 只调用不修改；`mspm0_runtime` 本轮**零改**——stepmotor_uart TX 既有已接线）、
`hc-team/middleware/**`（`vision_aim` 只调用不修改；`odometry` 本轮不接线不 include）、`board.syscfg`、
tests/host 既有 `test_*.c` 与全部 `fake_*.c`（`fake_uart_port` 已有 Vision RX 注入 + Stepmotor/Vision TX
抓取/完成钩子，`fake_clock` 已有时间注入面，均无需改）。（Debug/ 下 `subdir_*.mk`/`sources.mk` 是本地生成物，不入库、不列。）

#### 21.3.2 公共接口（最小面）

```c
/* app/service/gimbal/gimbal.h —— 云台视觉瞄准服务 */
#include "middleware/vision_aim/vision_aim.h"   /* VisionAim_Config_T / VISION_AIM_AXIS_COUNT（同层 Middleware，矩阵允许边） */

typedef enum {
    GIMBAL_STATE_IDLE = 0,      /* 未选题 */
    GIMBAL_STATE_HANDSHAKING,   /* 已发选题，等视觉确认帧 */
    GIMBAL_STATE_ARMING,        /* 确认到达，逐拍下发 enable/set-zero 建立原点 */
    GIMBAL_STATE_AIMING,        /* 运行期视觉像素闭环 */
    GIMBAL_STATE_STOPPED,       /* 安全停 / 坐标超时 / 握手超时（需重新 SelectTopic） */
} Gimbal_State;

typedef struct {
    VisionAim_Config_T aim;      /* 透传 vision_aim：死区/kp/步长/极性/轴程限幅几何唯一所有者（gimbal 不复算） */
    uint16_t step_speed_rpm;     /* 相对移动下发速度；emm42 限幅唯一所有者夹到 ≤100，本服务不复夹 */
    uint32_t coord_timeout_ms;   /* AIMING 期坐标 seq 连续无进展达此时长 → STOPPED（安全停） */
    uint32_t ack_timeout_ms;     /* HANDSHAKING 期无确认帧达此时长 → STOPPED */
} Gimbal_Config_T;

typedef struct {
    Gimbal_State state;
    int32_t  cur_pulse[VISION_AIM_AXIS_COUNT];   /* 轴累计脉冲位置（gimbal 唯一所有者；仅成功下发后累加） */
    float    last_coord_x;
    float    last_coord_y;
    uint32_t last_coord_seq;                     /* 最近消费的坐标 seq */
    bool     axis_active[VISION_AIM_AXIS_COUNT]; /* 最近一拍该轴是否越死区（vision_aim active 透传） */
    uint8_t  ack_main;                           /* 已确认主任务号 */
    uint8_t  ack_sub;                            /* 已确认子任务号 */
} Gimbal_Telemetry_T;

void Gimbal_Init(const Gimbal_Config_T *config);
    /* 拷贝配置 + VisionAim_Init(&cfg.aim) + GimbalStepbus_Init；清状态 + cur_pulse=0 → IDLE。
     * 不发选题、不发移动、不 enable（安全起点）。config==NULL 视为误用，不写（同 pid/odometry 口径）。 */
bool Gimbal_SelectTopic(uint8_t main_task, uint8_t sub_task);
    /* setup 期：UartVision_SendTopic 下发 0xFF 选题帧 → 记待确认号 + 起始 ackseq → HANDSHAKING。
     * TX 忙 → false 保持原态。任意态可调（重选题重置到 HANDSHAKING）。 */
void Gimbal_Update(void);
    /* 末尾恒推进 GimbalStepbus_Service（消费 TX 完成 + drain/discard 步进 RX）。
     * 自门控 GIMBAL_UPDATE_PERIOD_MS=10（Clock_NowMs 无符号减法）；到期 → UartVision_Poll → 状态机：
     *   HANDSHAKING：GetTopicAckSeq 进展且回显号匹配 → ARMING（cur_pulse=0）；超 ack_timeout_ms → STOPPED。
     *   ARMING：总线空时逐拍下发一帧 enable(X)/enable(Y)/setzero(X)/setzero(Y)；四帧发完 → AIMING。
     *   AIMING：GetLatestCoord + GetCoordSeq；seq 进展 → VisionAim_Map(coord,cur_pulse,out)
     *           → 每 delta≠0 轴：总线空则下发相对移动，成功后 cur_pulse += delta（唯一累加点）；
     *           seq 连续无进展达 coord_timeout_ms → STOPPED（短暂停顿保持 AIMING 静默不动）。 */
void Gimbal_Stop(void);
    /* 确定性安全停：停止下发、清待发、→STOPPED；步进保持使能（保持位置力矩）。可从正常控制流调用。 */
Gimbal_State Gimbal_GetState(void);
void Gimbal_GetTelemetry(Gimbal_Telemetry_T *out);   /* out==NULL 无副作用 */
```

```c
/* app/service/gimbal/gimbal_stepbus.h —— 服务内私有：最小步进 TX 派发（Service→Driver 直连） */
typedef enum { GIMBAL_STEPBUS_AXIS_X = 0, GIMBAL_STEPBUS_AXIS_Y, GIMBAL_STEPBUS_AXIS_COUNT } GimbalStepbus_Axis;

void GimbalStepbus_Init(void);    /* 清 TX 忙镜像；底层 StepmotorUart_Init 由 system 装配（此处不重复初始化硬件） */
void GimbalStepbus_Service(void); /* 消费 TX 完成（ConsumeTxDone）；drain+discard 步进 RX（界定 FIFO，不解析——
                                   * 步进应答有意不用，视觉是唯一反馈路径） */
bool GimbalStepbus_IsIdle(void);  /* 总线可发：未忙 && StepmotorUart_IsTxIdle */
bool GimbalStepbus_TrySendRelative(GimbalStepbus_Axis axis, int32_t pulses, uint16_t speed_rpm);
    /* 总线空且 pulses≠0 → 拆 dir(±→CW/CCW)+magnitude → Emm42_BuildPositionFrame(相对模式) → TryWrite，true；
     * 忙 / pulses==0 → false 不发。speed 交 emm42 限幅，本层不夹。 */
bool GimbalStepbus_TrySendEnable(GimbalStepbus_Axis axis, bool on);   /* 总线空 → 组 enable 帧 + TryWrite，true；忙 → false */
bool GimbalStepbus_TrySendSetZero(GimbalStepbus_Axis axis);          /* 总线空 → 组 set-zero 帧 + TryWrite，true；忙 → false */
```

- **轴映射**：`GimbalStepbus_Axis` X=0/Y=1（与 `VisionAim_Axis` 同序）→ emm42 轴 id（`EMM42_AXIS_X=2`/
  `EMM42_AXIS_Y=1`）在 gimbal_stepbus.c 内部转换，gimbal.c 不见 emm42 轴号。
- **头不暴露 Driver 类型**（§3.4）：gimbal.h 不含 `Emm42_Axis_e`/`emm42.h`/`stepmotor_uart.h`；步进协议细节
  全封在 gimbal_stepbus.c。gimbal.h 仅暴露 `VisionAim_Config_T`（同层 Middleware 配置，非 Driver 类型）。
- **前置条件**：System 装配层已 `UartVision_Init`（含 VisionUart_Init）、`StepmotorUart_Init`、Clock 就绪。
  本轮不接 odometry。硬件（步进使能/机械居中=逻辑零点）由用户上板对齐；ARMING 期 set-zero 假设 arm 时机械居中。

#### 21.3.3 preserved_behavior

- `driver/**`、`middleware/**`、其余 `app/**`（含冻结的 `platform_2d/*`、`stepmotor_bus.*`）零改动；主机既有
  377 用例全过；固件行为不变（gimbal.o/gimbal_stepbus.o 进链接但零调用者——V07 同款过渡态，T01 接线）。

#### 21.3.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/\|app/scheduler/\|app/ui/\|app/system/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/service/gimbal`，`#include` 行） | 0 命中（仅 `uart_vision.h`/`vision_aim.h`/`clock.h`/`step_motor/emm42.h`/`board_uart/stepmotor_uart.h` + 自身头 + `<stdint/stdbool/string>`；Service 禁 include 任何 `app/tasks/**`＝矩阵关键项，尤其 `platform_2d/stepmotor_bus.h` 零命中） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §21.3.1 | 无 allowed_files 之外改动（尤其 `app/tasks/**`、`driver/**`、`middleware/**`、`board.syscfg`、`mspm0_runtime.c` 零改） |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥397 PASS / 0 FAIL（377 基线 + ≥20 新用例）。必含——**gimbal_stepbus**：TrySendRelative 总线空发出精确 emm42 相对位置帧（X/Y 轴 id 正确、+pulses→CW/−pulses→CCW、magnitude=\|pulses\|、speed 透传经 emm42 ×10、CopyStepmotorTx 比对）；pulses==0 不发 → false；TX 忙 → false 且不发；CompleteTx→IsIdle 翻转；enable/set-zero 帧正确；RX drain 后 GetRxOverflowCount 不增。**gimbal**：Init 静默（无 TX、无坐标）；SelectTopic 发精确 0xFF 选题帧 + →HANDSHAKING，TX 忙→false；喂确认帧+号匹配→ARMING、号不匹配不转；ack 超时→STOPPED；ARMING 逐拍发四帧 enable/zero 后→AIMING；AIMING 喂坐标帧→VisionAim_Map→相对移动下发且 cur_pulse 精确累加（=delta）；总线忙时 cur_pulse 不累加（仅成功下发才进）；死区内坐标→不下发不累加；轴程饱和→delta 被 vision_aim 截断、cur_pulse 不越 travel_limit；seq 短暂停顿保持 AIMING 静默；coord 超时→STOPPED 停下发；Gimbal_Stop 确定性→STOPPED；遥测一致 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、gimbal.o 与 gimbal_stepbus.o 经 linkInfo.xml（`Debug/2026_Diansai_linkInfo.xml`）确证进入 .out 链接 |

- **主机测试链接组成**（事实登记）：
  - test_gimbal_stepbus = 真实 `gimbal_stepbus.c` + 真实 `emm42.c` + 真实 `stepmotor_uart.c` + `fake_uart_port.c`
    + test_gimbal_stepbus.c。**不链 fake_clock**（stepbus 无 Clock 依赖）。
  - test_gimbal = 真实 `gimbal.c` + `gimbal_stepbus.c` + `uart_vision.c` + `vision_uart.c` + `emm42.c`
    + `stepmotor_uart.c` + `vision_aim.c` + `fake_uart_port.c` + `fake_clock.c` + test_gimbal.c。
    仅 gimbal.c 需 Clock（自门控+超时），`fake_uart_port` 不定义 `Clock_NowMs`，与 `fake_clock` 无符号冲突。
- **测试盲区登记**：主机测试证消费者契约（emm42 帧字节精确、状态机转移、cur_pulse 单一累加、超时停），
  **不证**真实 UART7 DMA 往返与步进电机物理运动/方向/机械限位——由「emm42 帧与器件手册逐字节核对 + 固件
  构建 + 用户上板分阶段带载验证（低速短行程先确认方向/停止，§8.1）」保证。

#### 21.3.5 契约修订记录

- **冻结初版**（提交 8354b24）。Service 直连最小派发 + odometry 前馈几何推迟＝用户 2026-07-18 双裁定（§5 Q7 延伸）；
  TDD/施工/审计/拓扑为后续独立闭环（契约先于代码，闭环铁律）。
- **审计处置（本完成提交，arch-auditor 无阻断/无重要，3 建议级）**——四条契约红线全过（无非法跨层
  include、cur_pulse 单一累加点、emm42 RPM 限幅未复夹、Init/Stop 安全态成立）；建议级处置：
  - **F1（ARMING 无超时逃逸）——采纳修复**：HANDSHAKING/AIMING 均有超时→STOPPED，ARMING 原缺（步进总线
    故障使 `IsIdle` 永不为真时会永久滞留已使能态，虽无位移命令、无失控运动，仍违 §8.1「命令过期即停」）。
    修复：ARMING 增设活性上限，**复用 `ack_timeout_ms`**（setup 期总线活性预算，不新增 config 字段、不改
    公共签名），检查置于 IsIdle 判断之前。本属超出 §21.3.2 ARMING 描述的**行为新增**，因不动任何签名/证据行/
    allowed_files，按 S05b/S06 先例并入完成提交并在此显式登记（非静默改行）。E03 相应 +1 用例
    `test_arming_timeout_stops`（新用例 24 条，总 401，仍满足 E03 「377+≥20」）。
  - **F2（`gimbal_stepbus_axis_valid` 不可达防御）——采纳删除**：唯一调用方 gimbal.c 只传字面量 X/Y 或
    `for i<COUNT(=2)` 的 0/1 下标，该校验恒真、失败分支不可达（违 §8.3 无依据防御代码）。删函数 + 三处调用，
    在 `gimbal_stepbus_axis_id` 注释登记「入参恒有效、不加不可达校验」。保留 `pulses==0`/`IsTxIdle==false`
    两个**可达且有依据**的早退（死区邻域可产生 0、总线可忙）。
  - **F3（`gimbal.c` 未用 `<string.h>`）——采纳删除**：全文无 `mem*`/`str*`（配置用结构体赋值、遥测/复位
    逐字段），删冗余 include（基线 Surgical Changes：删除自引入的未用代码）。
  - 三项处置后 E03=401 PASS/0 FAIL、E04 exit 0 gimbal.o+gimbal_stepbus.o 经 linkInfo.xml 确证进链复跑确证。

### 21.4 S05b 契约修订 1（`vision_aim` 纯 P → 位置式 PD）——冻结

> 这是对 §21.2（S05b，已 DONE）的**契约修订**，独立提交冻结（闭环铁律：契约先于代码、修订单独提交）。
> §21.2 冻结文本不改，本节为增量契约；代码/TDD/审计/拓扑为其后独立闭环。

- **task_id**: S05b-PD
- **goal**: 给 `vision_aim` 像素→脉冲映射从**纯比例 P** 升为**位置式 PD**（加微分项）。**无 I、无微分滤波**——
  两条硬约束各有单一所有者依据：
  1. **无 I**：步进 `cur_pulse` 累加（`gimbal.c` 唯一累加点）本身即积分器，静态目标已零稳态误差；控制器再加 I = 双积分器（超调/积分饱和）。
  2. **无微分滤波**：坐标已由**上位机 Kalman 滤波**（用户 2026-07-19 定案），本层禁止二重滤波（data-chain §8.2，同 IMU 内置 Kalman 上层禁二次滤波先例）；信号已干净，裸差分不放大噪声，滤波只会加相位滞后。
- **归属定案（topo-navigator 切片）**：**扩 `vision_aim` 自建 D 项，不复用 `middleware/pid`**。理由：复用 pid 的唯一好处是其微分滤波——本任务恰恰禁滤波；且 pid 会拖入 `Pid_T.out_limit`（V26 明文禁复用，含积分状态语义不等价）与积分机制（需关掉）。扩 vision_aim 则单文件收口全部几何、不引第二限幅所有者（V26 保持）、无滤波无 I。
- **D-state 归属**：比照 `s_cur_pulse[axis]` 先例（调用方持状态、vision_aim 保持纯函数）——上一拍误差 `prev_error_px[axis]` 由 **`gimbal.c` 持有**（新增 `static float s_prev_error_px[VISION_AIM_AXIS_COUNT]`），逐拍传入；**不在 vision_aim 内加 static**（会破坏纯函数契约、与潜在其他调用方共享隐式状态）。
- **接口辩护**：给定一帧（已滤波的）像素坐标 + 该轴当前累计位置 + 上一拍误差，能算出「本拍位置式 PD 该走多少脉冲」，含死区/步长/极性/轴程几何。仅此。

#### 21.4.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/middleware/vision_aim/vision_aim.h` | 修改（`VisionAim_Config_T` 增 `kd[]`；`VisionAim_Map` 增 `prev_error_x/y` 入参；注释） |
| `hc-team/middleware/vision_aim/vision_aim.c` | 修改（PD 公式：`raw=kp*e+kd*(e-prev_e)`；方向/幅值改由 raw 决定；死区/floor1/max_step/极性/轴程不变） |
| `hc-team/app/service/gimbal/gimbal.c` | 修改（持 `s_prev_error_px[]`；`s_cfg.aim` 增 `kd`；改 `VisionAim_Map` 调用点传 prev；进 AIMING 首拍种子 prev:=e；每拍 Map 后 prev:=out.error_px） |
| `tests/host/test_vision_aim.c` | 修改（增 PD 用例；kd=0 回归等价断言） |
| `tests/host/test_gimbal.c` | 修改（增 prev_e 种子/逐拍更新/成功下发才累加 用例） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写（§3 S05b 行 + 本节） |

forbidden_files：`hc-team/middleware/pid/**`（不复用不改）、`hc-team/driver/**` 全部（含 `step_motor/emm42`、`stepmotor_uart`、`uart_vision`——限幅/尺度/编解码不动）、`hc-team/middleware/**` 其余、`hc-team/app/service/**` 其余、`hc-team/app/{tasks,scheduler,system,ui}/**`、`board.syscfg`、tests/host 既有其余 `test_*.c` 与全部 `fake_*.c`。（`Debug/makefile` 无新 .o，不动；不列。）

#### 21.4.2 公共接口变更（最小面，删旧签名不留双轨）

```c
/* VisionAim_Config_T 增一字段（其余不变） */
float kd[VISION_AIM_AXIS_COUNT];   /* 微分增益 像素误差速率→脉冲(>=0)；默认 0 = 退化纯 P。
                                      无微分滤波：坐标已上位机 Kalman，二重滤波违 §8.2。 */

/* VisionAim_Map 增两入参 prev_error_x/y（gimbal 持有的上一拍 error_px）；其余入参/Result 不变 */
void VisionAim_Map(float coord_x, float coord_y,
                   int32_t cur_x_pulse, int32_t cur_y_pulse,
                   float prev_error_x, float prev_error_y,
                   VisionAim_Result_T *out);
    /* 逐轴：e=coord-center → de=e-prev_error → 死区门控(|e|<=deadband→delta 0,active false，
     *   但 out.error_px 恒=e 供调用方存 prev) → raw=kp*e+kd*de → 方向=sign(raw)、幅值=|raw| floor 1
     *   clamp max_step → 极性 sign → 轴程限幅(依 cur_pulse) → delta_pulse。纯函数、无状态、无滤波、无积分。 */
```

- **方向语义变更（有意）**：纯 P 时方向=`sign(error)`；PD 后方向=`sign(kp*e+kd*de)`——收敛邻域 D 可反号 = 阻尼刹车，属 PD 本义。幅值仍受 `max_step` 每拍封顶（=斜率限制，§8.1），D 不产生单拍大跳。
- **单一所有者声明（承 §21.2 + V26，不放宽）**：死区/比例/微分/步长限幅/极性/轴程限幅几何 = vision_aim 唯一所有者（微分是本次新增的同层同文件所有权，非跨模块）；轴累计位置状态 + 上一拍误差状态 = 调用方 gimbal；RPM 限幅/尺度 = emm42（不复夹）；坐标编解码 = uart_vision；**滤波 = 上位机 Kalman（本层零滤波）**。

#### 21.4.3 preserved_behavior

- **`kd=0` 时逐位等价旧纯 P**（kp>=0：`|raw|=|kp*e|=|e|*kp`、`sign(raw)=sign(e)`，floor/clamp/极性/轴程全同）——既有 16 个 vision_aim 用例 + 401 主机基线全过。
- `middleware/pid/**`、`driver/**`、`board.syscfg`、其余 `app/**`/`middleware/**` 零改动；无微分滤波状态、无积分状态引入；固件行为在 `kd=0` 下不变。

#### 21.4.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `hc-team/driver/\|hc-team/app/\|middleware/pid\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/middleware/vision_aim`，`#include` 行） | 0 命中（仍仅 `<stdint/stdbool/stddef>` 与自身头；尤其**未引入 pid.h**） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §21.4.1 | 无越界（尤其 `middleware/pid`、`driver/**`、`board.syscfg` 零改） |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥ 404 基线（Phase 2 首步 clean rerun 已确认）+ ≥10 新用例 = ≥414 PASS / 0 FAIL。必含——**kd=0 回归**：与旧 P 逐位等价（既有用例全过 + 显式等价断言）；**D 阻尼**：de 与 e 反号(误差在缩小)时 \|raw\|<纯 P、同号(误差在扩大)时放大；**无 I**：恒定误差连续多拍 delta 不逐拍累增(纯 P+D 恒定误差→恒定 delta)；**无滤波/无隐藏状态**：相同 (coord,cur,prev) 输入输出可复现、无 alpha 记忆；**死区**：内 delta=0 且 active=false 但 out.error_px 仍=e；**极性**：sign 作用于 raw(±1 得反号)；**轴程限幅**：依 cur_pulse 截断不变；**gimbal 线程状态**：进 AIMING 首拍 prev:=e(de=0 无首拍 D 冲击)、逐拍 prev:=out.error_px(死区拍也更新)、仅成功下发才 cur_pulse+=delta(不变)；**安全**：Init 不产生位移、Gimbal_Stop 确定性停(不变)、max_step 每拍封顶 D 不越 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、vision_aim.o + gimbal.o 经 linkInfo.xml 确证进入 .out 链接 |

- **主机测试链接组成**：test_vision_aim 仍不链任何 fake（纯算法）；test_gimbal 沿用 §21.3 既有 fake 组成（不新增 fake）。

#### 21.4.5 契约修订记录

- **冻结初版**（本提交）。Phase 1 完成：契约冻结，零生产代码。TDD（红）→ 施工 → 逐行证据 → arch-auditor → topo-updater 为其后闭环。
- **基线已复核（本修订提交）**：Phase 2 首步 `rtk proxy make -C tests/host clean+all` = **404 PASS / 0 FAIL**（较 §21.2 冻结时 401 +3，属 S05c 之后既有增量、非本任务引入）。E03 目标锚定 **404 基线**；`rtk make` 会压缩 PASS 行、计数取证必须走 `rtk proxy make`（本次踩坑登记）。
- **完成（Phase 2 + 3，代码本提交）**：E01 vision_aim 仅含自身头+`<stddef/stdbool/stdint>`、无 pid.h/Driver/App（0 命中）；E02 仅动 §21.4.1 六文件（`.ccsproject` 会话前既存未纳入）；E03 **416 PASS / 0 FAIL**＝404+12（vision_aim 10 + gimbal 2，PD 阻尼/助推/反号/无 I/无滤波/首帧播种全覆盖，kd=0 逐位等价旧 P）；E04 exit 0、0 诊断、vision_aim.o+gimbal.o 经 linkInfo.xml 确证进链。
- **arch-auditor 处置**：五项重点（依赖矩阵 / V26 单一所有者 / prev_error 归属 / motor-safety §8.1 / 无 I 依据）全过，无阻断/无重要。**1 建议级——文档处置不改代码**：D 项时间基 = 每视觉帧差分（`de=error-prev`），无固定 dt 归一，帧率波动时等效阻尼随之漂移；契约 §21.4 明选裸差分、无 dt，长停顿后大 de 由 `max_step` 兜底不失稳。按嵌入式基线不加无依据防御——若实车调参发现阻尼随帧率漂移，再在 gimbal 侧（调用方，不进 vision_aim）按 dispatch 间隔归一 de，届时以契约修订登记。

## 22. Phase 4B —— World-2 装配点亮（现役平台切换 + 首个 debug 运行条目）

> 用户 2026-07-19 本会话裁定。契约先于代码、单独提交冻结（闭环铁律）；W1/W2 各自 TDD→施工→证据→审计→拓扑为其后独立闭环。

### 22.0 裁定更新（2026-07-19，用户本会话）

1. **World 2 定为现役平台并即刻「通电」**：`main` 切到新 `scheduler.c`；World 1（`task_scheduler.c`/`task_groups.c`/`run_registry.c`/`app/tasks/**`/`menu_core`）旁路静默——**冻结不删，T01 删**（不再调用即无双 Driver 所有者，已证 `SysRun` 是旧任务体唯一执行入口，无 ISR 泵）。用户原话：「你的 World 2 写得比我好，只是功能不是我要的」。
2. **debug/test 运行条目提前于 T01 编写**：用户澄清——「原计划 T01 写 TASK 本就含 debug/test 任务组，测试任务肯定要提前写好，不可能不提前」。故 §1「T01 最后 / 新 Service 零调用者」对 **debug/test entries 不再适用**：SYS01 装配 + debug 条目现在开始；赛题 Task（薄编排）仍待赛题公布。
3. **交互模型（用户裁定，World 1 逻辑用 World 2 实现）**：初始化后＝极简空转态，`Scheduler_Run` 每拍只泵 `background_step=Menu_Tick`（HMI：OLED 刷新 + 按键中断事件，无事件不重渲染），别的后台全不开；**按键进指定条目才 `on_enter` 注册 + `on_step` 泵作用域服务**，退出 `on_exit` 停。周期归各 Service 自门控（单一所有者：速度环 10ms=`Chassis`；寻迹环 10ms 由 `LineFollow_Update` 末尾同拍级联「紧跟不落后」；位置环 20ms 归位置服务）——**不是上电直进**（上一版误解已纠正）。
4. **VOFA 通道语义（用户裁定）**：kp/ki/kd + 目标左右轮速＝**既显示(tx)又控制(cmd)**；当前(反馈)左右轮速＝**只显示**。PWM 移除（用户未要，让位给增益显示）。据此 W1 修订 tuning 速度环组。
5. **不上电≠可以不完美**：硬件约 5 天后到；在此之前软件必须**完整正确、烧进去即完美运行**——验收＝构建 + 主机测试 + 依赖/装配扫描 + 聚焦阅读；带载物理验证硬件到货后用户自理（§8.1 分阶段）。

### 22.1 W1 契约（tuning 速度环组 tx 外显修订 —— S03 契约修订 2）——冻结

- **task_id**: W1-tuning_tx_gains
- **goal**: 把 `tuning_chassis` 的 VOFA tx 组从 6 通道 {目标 L/R、反馈 L/R、PWM L/R} 改为 **10 通道** {左右 kp/ki/kd 回显(6) + 目标 L/R(2) + 反馈 L/R(2)}，实现「kp/ki/kd 与目标既显示又控制、当前只显示」。**cmd×8 不变**（LP/LI/LD/RP/RI/RD/LM/RM）。增益回显＝每拍 `RefreshTx` 从 cmd 组单向复制（应用值恒等于 cmd，因 `Apply` 每拍无条件写 Chassis）。
- **通道顺序（上位机据此配 VOFA 工程，写码时定死并注释）**：tx[0..9] = `kp_L, ki_L, kd_L, kp_R, ki_R, kd_R, target_L, target_R, feedback_L, feedback_R`。
- **S03 §9 三原则修订点（仅第 2 条 tx 来源扩面，隔离本意不变）**：tx 组＝「Chassis 遥测快照（目标/反馈）**＋本 profile 应用中的增益/目标设定值回显（cmd 单向复制）**」；cmd 组仍**永不从运行值回读**、运行值永不写 cmd（隔离本意保持）。不新增 Chassis 增益 getter（应用值＝cmd，回显 cmd 即回显应用值，避免第二读出口）。
- **单一所有者**：增益/目标写入仍只经 Chassis 公共 API；限幅/换向/超时/刹车/PWM 尺度各归既有所有者，本修订零复做，仅改「哪些量进 tx 显示」。

#### 22.1.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/tuning/tuning_chassis.c` | 修改（tx 组字段：删 pwm_L/R，增 kp/ki/kd_L/R；`RefreshTx` 增益回显自 cmd；`register_group` tx 注册顺序=通道序；`reset_safe` 同步） |
| `hc-team/app/service/tuning/tuning_chassis.h` | 修改（仅变量组注释：tx×10 新构成） |
| `tests/host/test_tuning.c` | 修改（原 pwm tx 用例改写为增益回显；增 target 双向、feedback 只显示、增益回显=cmd 用例） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写（§3 S03 行 + 本节） |

forbidden_files：`hc-team/app/service/tuning/tuning.c` / `tuning.h`（生命周期不变）、`hc-team/app/service/chassis/**`、其余 `app/service/**`、`hc-team/app/{tasks,scheduler,system,ui}/**`、`hc-team/driver/**`、`hc-team/middleware/**`、`tests/host` 既有其余 `test_*.c` 与全部 `fake_*.c`、`Debug/makefile`（无新 .o）。

#### 22.1.2 preserved_behavior

- `tuning.c/.h`、`chassis/**`、`driver/**`、`middleware/**`、其余 `app/**` 零改动；cmd 路径与应用链路不变；固件行为除 VOFA 帧内容（tx 通道集）外不变。

#### 22.1.3 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/\|app/scheduler/\|app/ui/\|app/system/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/service/tuning`，`#include` 行） | 0 命中 |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §22.1.1 | 无越界改动 |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥418 PASS / 0 FAIL（416 基线 + 净新 ≥2），必含：tx×10 布局与顺序、增益回显=cmd 值（改 cmd→下一拍 tx 相应变）、目标既进 cmd 又进 tx、反馈只进 tx 无 cmd 绑定、pwm 不再注册、cmd×8 不变、隔离性（外部改运行值不回写 cmd） |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、tuning_chassis.o 重编并经 linkInfo.xml 确证进链 |

#### 22.1.4 完成记录（Phase 2+3，代码本提交）

- TDD 红→绿：先改 `test_tuning`（tx×10 布局 + 增益回显 + 目标既显既控），在旧 tx×6 上 5 条帧解码用例转红（frame 28B≠预期 44B），再实现 `tuning_chassis` 转绿。
- E01 0 命中（tuning 仍仅 `chassis.h` + `uart_vofa.h`）；E02 仅动 §22.1.1 三文件（`.ccsproject` 会话前既存未纳入）；E03 **418 PASS / 0 FAIL**＝416 基线 + 净新 2（`test_tx_gain_echo_tracks_cmd`、`test_target_both_display_and_control`；`test_tx_frame_is_snapshot_copy` 改写为 `test_tx_frame_layout_and_echo`）；E04 exit 0、0 诊断、`tuning_chassis.o` 经 linkInfo.xml 确证进链（3 处）。
- arch-auditor 无阻断/无重要（四点：依赖矩阵合规 / 增益回显纯赋值非第二所有者 / cmd→tx 显示不破坏隔离本意 / 无越界防御）；1 建议级（`test_tuning.c:122` 过时注释「6 通道」）已改「10 通道」。
- topo-updater 同步见索引 §10。

### 22.2 W2 契约（World-2 装配点亮 —— SYS01 增量 + SpeedTune 运行条目）——冻结

- **task_id**: W2-world2_compose
- **goal**: 定义 **entry 表**（首条 `SpeedTune` → tuning 三钩子）+ **菜单分组表**（RUN 组 `"DEBUG"` 含 SpeedTune）+ 装配（`Hmi_Init`/`Chassis_Init`/`Tuning_Init` → `Scheduler_Init(g_entries, N, Menu_Tick)` → `Menu_Setup(g_groups, M)`）+ `main` 主循环 `Scheduler_Run(Clock_NowMs())`；**停用旧 `SysRun`**。World 2 成为现役启动路径；旧 World 1 静默（冻结、T01 删）。
- **entry 钩子（app_compose 内小 wrapper 适配签名）**：`speedtune_enter(void){ Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED); }`；`speedtune_step(uint32_t now){ (void)now; Tuning_Update(); }`（Tuning 是 Service，自门控走自身 Clock，10ms 单一所有者）；`speedtune_exit(void){ Tuning_ExitProfile(); }`。
- **交互不变量（承 §22.0.3）**：空转 `Scheduler_Run` 只泵 `Menu_Tick`（HMI），无活动条目；进 SpeedTune→注册 VOFA(on_enter)→10ms 泵速度环(on_step)→BACK 刹停清表(on_exit)。单活动条目不变量结构性排除双泵。
- **开机安全态（§8.1）**：`SysInit` 保留既有 `Motor_Init`+`Motor_BrakeAll`；进入 SpeedTune 前电机零输出（无活动条目＝不泵 chassis）；进入即 `Chassis_Stop`+安全 cmd（tuning EnterProfile）。
- **旧代码处置**：`sys_init.c` 保留既有 Driver + 旧 app-task Init（无害、其任务永不被泵），仅**追加** World-2 装配；不删旧 Init（T01 删）。`main.c` 删 `SysRun` 调用与 `task_scheduler.h` include。

#### 22.2.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/system/sys_init.h` | 新建（声明 `SysInit`——现仅寄居冻结的 `task_scheduler.h:127`；main 去 task_scheduler.h 后需 World-2 合规声明 site，见 §22.2.4 修订 1） |
| `hc-team/app/system/app_compose.h` | 新建（导出 entry 表/分组表 getter 或 extern + 装配入口声明） |
| `hc-team/app/system/app_compose.c` | 新建（`g_entries[]` + `g_groups[]` + 三 wrapper；持有 SpeedTune 条目定义） |
| `hc-team/app/system/sys_init.c` | 修改（+include scheduler/menu/hmi/chassis/tuning/app_compose；+Hmi/Chassis/Tuning Init + Scheduler_Init + Menu_Setup） |
| `hc-team/app/system/main.c` | 修改（删 task_scheduler.h + SysRun；+scheduler.h/clock.h；主循环 `Scheduler_Run(Clock_NowMs())`） |
| `Debug/makefile` | 登记 app_compose.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写（§3 SYS01 行 + 本节） |

forbidden_files：`hc-team/app/service/**`、`hc-team/app/scheduler/**`、`hc-team/app/ui/**`、`hc-team/middleware/**`、`hc-team/driver/**`（全部只调用不改）、`hc-team/app/tasks/**`（World 1，只是不再被调用，不改）、`tests/host/**`（装配根无可测缝，无新用例）、`board.syscfg`。

#### 22.2.2 preserved_behavior

- 所有 service/driver/middleware/World-1 文件零源码改动（仅 main 不再调 SysRun）；主机既有用例全过（W1 后基线）；World-2 各 .o 由 main→Scheduler 调用链真实进链（此前零调用者）。

#### 22.2.3 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 范围审计 | `git status` + `git diff --stat` 对照 §22.2.1 | 无越界改动 |
| E02 | 装配层依赖闸门 | PowerShell：`& .claude/hooks/arch-scan.ps1 -Mode check` | 空输出（0 新增违规；`app/system` 仅禁 DL HAL，app_compose/main/sys_init 新增 include 全是 scheduler/menu/hmi/chassis/tuning/clock 头，无 DL HAL 泄漏） |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥418 PASS / 0 FAIL（W1 后基线，无新用例——装配根无可测缝；被接线的 scheduler/menu/hmi/tuning/chassis 各自用例已覆盖） |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics；app_compose.o + scheduler.o + menu.o + menu_param.o + hmi.o + chassis.o + tuning.o + tuning_chassis.o 经 linkInfo.xml 确证进链；main.o 解析 `Scheduler_Run`/`Menu_Setup`/`Clock_NowMs`、不再引用 `SysRun` |

#### 22.2.4 契约修订记录

- **冻结初版**（提交 e76151b）。W1 先做（改基线为 418），W2 后做（基线取 W1 后值）。Phase 2/3 各自闭环。
- **修订 1（提交 1e04b10，施工前）**：allowed_files 增 `hc-team/app/system/sys_init.h`（新建）。原因：`SysInit` 声明现仅在 World-1 冻结头 `task_scheduler.h:127`；main.c 按契约删该 include 后无层合规声明 site。新建 app/system 自己的 `sys_init.h` 承载 `void SysInit(void)` 声明；`task_scheduler.h` 冻结不可改，其重复声明属无害冗余，T01 删旧文件时消除。E03 基线相应取 W1 后值 418；无新用例（装配根）。

#### 22.2.5 完成记录（Phase 2+3，代码本提交）

- World-2 装配点亮：`main → Scheduler_Run(Clock_NowMs())` 现役启动路径，旧 `SysRun` 停用；空转只泵 `Menu_Tick`(HMI)，进 SpeedTune 条目才注册 VOFA + 10ms 泵速度环。
- E01 仅动 §22.2.1 六文件（`app_compose.{c,h}`/`sys_init.h` 新建，`sys_init.c`/`main.c` 改，`Debug/makefile` 登记 `app_compose.o`；本地 `subdir_vars.mk` 不入库）；`.ccsproject` 会话前既存未纳入。
- E02 `arch-scan -Mode check` 空输出（0 新增违规）；E03 主机 **418 PASS / 0 FAIL**（W1 后基线，无新用例——装配根无可测缝）；E04 exit 0、0 诊断、`app_compose.o` 经 linkInfo.xml 确证进链（3 处），`main→Scheduler_Run/Menu_Setup/Clock_NowMs` 解析成功（旧 `SysRun` 已非启动路径）。
- 施工踩坑：`app_compose.c` 初版漏 `<stddef.h>` 致 `NULL` 未声明（连带 `sizeof` 不完整类型报错），补 include 后绿。
- arch-auditor 六轴全过、无发现：分层无 DL HAL 泄漏 / 空转只泵 HMI / boot-to-safe（SysInit `Motor_BrakeAll` + 无活动条目不泵 chassis + EnterProfile 安全 cmd）/ 旧任务静默（`clock.c:32` SysTick 仅 `s_tick_ms++`，全仓无 ISR 泵 `TaskTimeSliceManage`）/ 单活动条目排除双泵 / 无越界。命名备注：条目/分组表实名 `s_entries[]/s_groups[]`（static）。
- topo-updater 同步见索引 §10。

## 23. Phase 4B —— W3：DEBUG 组新增两个调试运行条目 + 统一 RUNNING 横幅

> 用户 2026-07-19 本会话裁定。契约先于代码、单独提交冻结（闭环铁律）；T1/T2/T3 各自 TDD→施工→证据→审计→拓扑为其后独立闭环。
> topo-navigator 切片（本会话）确认：Encoder_Update 现役唯一调用者 = `chassis.c:82`（`motion.c` 只读 GetSnapshot）；
> VOFA/Motor 现役唯一路径 = tuning/chassis（经 SpeedTune 条目）；menu RUN_ACTIVE 现文档为「不写任何显示行」。

### 23.0 裁定更新（2026-07-19，用户本会话）

1. **两个新条目都放 DEBUG 组**（与 SpeedTune 平级），menu 结构不变（仍两级）。条目名 ASCII：`EncoderTest` / `MotorDir`。
2. **统一 RUNNING 横幅归 menu 框架**：RUN_ACTIVE 期 menu 在 row0 画 `RUNNING`、清 row1..3；对**每个**运行条目（debug/test/未来）自动生效。Service 一律不碰 OLED。**修订 menu 显示所有权契约**（承 UI01 §13）：RUN_ACTIVE 不再「整屏让给 on_step」，而是「menu 占固定 RUNNING 横幅；条目自绘整屏＝未来按条目 opt-in flag，当前无条目 opt-in，故无双写」。
3. **EncoderTest 接受第二个 Encoder_Update 采样点（用户裁定，登记 V21 扩条）**：看实时脉冲必须自泵 Encoder_Update（只读 GetSnapshot 在本条目活动期无人刷新＝静止零，测不出接线正负）。单活动条目不变量保证 encoder_test 与 chassis 永不同拍——第二采样点是「多推进点、互斥缓解」的 V21 同款模式，非运行期双采样。方向修正仍唯一在 `encoder.c s_direction_sign`（EncoderTest 读已修正快照，不加第二反向）。
4. **VOFA 第二注册者（EncoderTest）与 tuning 互斥**：encoder_test 直调 uart_vofa（clear/register/run）做 tx-only 遥测（**不 bind_cmd**）。cmd 分发唯一收口仍是 tuning（EncoderTest 零 cmd，不触碰「cmd 应用归 tuning」的 S03 §9 定案）；单活动条目保证不与 tuning 同拍泵 vofa_run。
5. **MotorDir 驱动参数（用户裁定）**：两轮同向 `±200`（20% 满量程）、每相 2000ms、前→后**循环**直到 BACK。换向过零/死区/slew/100ms 超时归零全部单一所有者在 `motor.c`——MotorDir 只发 ±200 目标 + 每拍 Motor_SetOutput 刷新看门狗 + Motor_Update(elapsed)；on_exit=Motor_BrakeAll。
6. **时间注入**：两个新 Service 的 Update 取 `now_ms`（由 scheduler `on_step(now_ms)` 注入，非直读 Clock）——主机测试免链 fake_clock，确定性走相位。

### 23.1 T1 契约（menu 统一 RUNNING 横幅）——冻结

- **task_id**: W3-T1-menu_running_banner
- **goal**: menu RUN_ACTIVE 界面渲染统一 `RUNNING` 横幅（row0=`RUNNING`，row1..3 清空），复用既有 s_dirty/就绪门控（进条目置脏、渲染一次、无输入不重绘）。修订 menu 显示所有权契约注释（§23.0 第 2 条）。
- **接口辩护**（菜单能做什么）：运行条目激活时给出统一「运行中」界面反馈——这是菜单导航面的一部分（RUN_ACTIVE 是 menu 自己的界面态），非某个 Service 的能力。

#### 23.1.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/ui/menu/menu.c` | 修改（`handle_run_list` 进 RUN_ACTIVE 置脏；`Menu_Tick` 渲染门控放开 RUN_ACTIVE；`render()` 增 RUN_ACTIVE 分支画 RUNNING+清行；改 file/§9 显示所有权注释） |
| `hc-team/app/ui/menu/menu.h` | 修改（仅 `Menu_Screen` RUN_ACTIVE 注释：让屏→固定 RUNNING 横幅） |
| `tests/host/test_menu.c` | 修改（`test_run_active_menu_draws_nothing` 改写为 `test_run_active_draws_running_once`：进条目绘制一次、后续泵送不重绘；BACK 回子列表重绘不变） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写（§3 UI01 行备注 + 本节完成记录） |

forbidden_files：`hc-team/app/ui/menu/menu_param.{c,h}`、`hc-team/app/service/**`、`hc-team/app/{scheduler,system,tasks}/**`、`hc-team/driver/**`、`hc-team/middleware/**`、`tests/host` 其余全部 `test_*.c`/`fake_*.c`、`Debug/makefile`（menu.o 已登记，无新 .o）。

#### 23.1.2 preserved_behavior

- 除 RUN_ACTIVE 渲染外 menu 一切界面/转移不变；无新 `#include`（复用已在场的 `Hmi_PrintLine`）；menu_param、hmi、scheduler、所有 service/driver 零改动；固件行为仅 RUN_ACTIVE 屏由空白→RUNNING。

#### 23.1.3 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 范围审计 | `git status` + `git diff --stat` 对照 §23.1.1 | 无越界改动 |
| E02 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥418 PASS / 0 FAIL（W2 后基线；menu 用例净新 ≥0——RUN_ACTIVE 用例由「不绘制」改写为「绘制一次、无冗余重绘、BACK 回子列表重绘」） |
| E03 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、menu.o 重编并进链 |

#### 23.1.4 完成记录（Phase 2+3，代码本提交）

- TDD 红→绿：`test_run_active_menu_draws_nothing` 改写为 `test_run_active_draws_running_once`，在旧 menu（RUN_ACTIVE 零绘制）转红（`after_enter > before_enter` 失败），实现 `render_run_active`（row0=RUNNING+清 row1..3）+ 放开渲染门控（去 `!= RUN_ACTIVE`）+ `handle_run_list` 进 RUN_ACTIVE 置脏后转绿。
- E01 范围：仅动 `menu.c`/`menu.h`/`test_menu.c`（+plan）；E02 主机 **429 PASS / 0 FAIL**（W2 后 418 基线；menu 用例净新 0——RUN_ACTIVE 用例改写）；E03 固件 exit 0、0 诊断、menu.o 重编经 linkInfo 进链。
- arch-auditor：RUNNING 横幅 menu 独占，三活动 Service（tuning/encoder_test/motor_check）均不碰 OLED/Hmi，无双写。topo-updater：menu RUN_ACTIVE 边 + 显示所有权契约修订同步 `app.md §3`。

### 23.2 T2 契约（encoder_test 编码器脉冲遥测服务）——冻结

- **task_id**: W3-T2-encoder_test
- **goal**: 新建 `app/service/encoder_test/`：读 Encoder Driver + 注册 VOFA tx×4（无 cmd）的**只读诊断服务**。查左右轮编码器接线正负（正转→脉冲正增）+ 实测 100 米累计脉冲。零调用者（T3 接线），即预期状态。
- **接口辩护**（诊断能做什么）：能进入/退出一次编码器脉冲遥测（进入即挂 VOFA 组、退出清组）、能被周期推进（自泵采样 + 发帧）。仅此成为公共面。

#### 23.2.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/encoder_test/encoder_test.h` / `.c` | 新建 |
| `tests/host/test_encoder_test.c` | 新建 |
| `tests/host/Makefile` | 追加 test_encoder_test 目标/clean/.PHONY |
| `.gitignore` | 追加 test_encoder_test / .exe |
| `Debug/makefile` | 登记 encoder_test.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写（本节完成记录） |

forbidden_files：`hc-team/app/service/**` 其余、`hc-team/app/{ui,scheduler,system,tasks}/**`、`hc-team/driver/**` 其余（仅调用 encoder/uart_vofa，不改）、`hc-team/middleware/**`、`tests/host` 既有 `test_*.c`/`fake_*.c`（复用 fake_board_gpio 编码器注入 + fake_uart_port VOFA 抓取）。

#### 23.2.2 公共接口（最小面）

```c
void EncoderTest_Start(void);           /* vofa_clear_profile → 注册 tx×4（无 cmd）→ 采样基准复位；不发电机命令 */
void EncoderTest_Update(uint32_t now_ms);/* 自门控 10ms：Encoder_Update(elapsed) → 快照刷 tx → vofa_run（发上一拍帧） */
void EncoderTest_Stop(void);            /* vofa_clear_profile；无电机可停（本服务从不驱动电机） */
```

- **VOFA 通道（注册序=上位机通道序，写死并注释）**：tx[0..3] = `enc_L`(总累计脉冲,int)、`enc_R`(int)、`spd_L`(m/s,float)、`spd_R`(float)。累计脉冲用于看正负 + 100 米计数；速度给瞬时方向。**零 bind_cmd**。
- **单一所有者声明**：编码器方向修正唯一在 `encoder.c s_direction_sign`（读已修正快照，不加第二反向）；VOFA 协议/解析/缓冲归 uart_vofa Driver。本服务唯一拥有：诊断遥测变量组 + 采样/发帧节奏（第二 Encoder_Update 采样点，V21 扩条，单活动条目互斥）。
- **前置条件**：System 装配层已完成 `vofa_init()` 与 `Encoder_Init()`。

#### 23.2.3 preserved_behavior

- 所有既有 service/driver/middleware/app 零源码改动；主机既有用例全过；encoder_test.o 进链但零调用者（T3 前）。

#### 23.2.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/\|app/scheduler/\|app/ui/\|app/system/\|app/service/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/service/encoder_test`，`#include` 行） | driver 命中仅 `encoder`/`uart_vofa`；`app/service/` 唯一命中 = 自身头 `encoder_test.h`（非跨服务，修订1）；`app/tasks\|scheduler\|ui\|system` 与 DL HAL 前缀 0 命中 |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §23.2.1 | 无越界改动 |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥418+新用例 PASS / 0 FAIL，必含：Start 后零电机命令、tx×4 注册且零 cmd 绑定、正转 raw→累计脉冲正增 & 速度正、方向反转 raw→负（接线反检出）、10ms 门控单帧、Stop 清 profile 后无新帧、重进重置采样基准 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、encoder_test.o 经 linkInfo.xml 确证进链（零调用者，ORDERED_OBJS 强留） |

#### 23.2.5 完成记录（Phase 2+3，代码本提交）

- 新建 `encoder_test.{h,c}` + `test_encoder_test.c`（6 用例：4 通道帧、忠实镜像快照、累计一致同号、反向翻号、10ms 门控、Stop 清 profile）；Debug 构建接线 `subdir_vars/rules.mk`（本地）+ `Debug/makefile` ORDERED_OBJS/-include 登记。
- E01 依赖纯净（修订 1 口径）：driver 命中仅 `encoder`/`uart_vofa`，`app/service/` 唯一命中自身头，`app/tasks\|scheduler\|ui\|system`+DL HAL 0 命中；E02 范围仅 §23.2.1；E03 主机 **429 PASS**（含本服务 6 新用例）；E04 固件 exit 0、0 诊断、`encoder_test.o` 经 linkInfo.xml 进链（本提交起被 app_compose 条目调用，非零调用者）。
- arch-auditor：只读快照单向复制无第二处理（方向/单位唯一在 encoder.c）、V21 第二采样点单活动条目互斥缓解确认、无越界防御——无阻断/无重要；1 建议级（发帧时序注释「上一拍」实为当拍）已改「本拍刷新帧」。topo-updater：V21 扩条（§6）+ `app.md §3` EncoderTest 节点/边同步。

### 23.3 T3 契约（motor_check 电机方向测试服务 + app_compose 接线两条目）——冻结

- **task_id**: W3-T3-motor_check
- **goal**: 新建 `app/service/motor_check/`：两轮同向 ±200 前/后 2s 循环的电机方向测试服务（查 TB6612 AI1/AI2 是否接反）；并在 `app_compose.c` 把 `EncoderTest`+`MotorDir` 两条目接进 s_entries[] + s_debug_entries[]（连同 SpeedTune 三条目同组）。
- **接口辩护**（底盘器件能做什么）：能启动/推进/停止一次「两轮同向前后循环」方向自检。仅此成为公共面。

#### 23.3.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/motor_check/motor_check.h` / `.c` | 新建 |
| `hc-team/app/system/app_compose.c` | 修改（+include encoder_test/motor_check；+2 组三钩子 wrapper；s_entries[] 增 EncoderTest/MotorDir；s_debug_entries[] 增两下标） |
| `tests/host/test_motor_check.c` | 新建 |
| `tests/host/Makefile` | 追加 test_motor_check 目标/clean/.PHONY |
| `.gitignore` | 追加 test_motor_check / .exe |
| `Debug/makefile` | 登记 motor_check.o（app_compose.o 已登记） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写（§3 SYS01 行备注 + 本节完成记录） |

forbidden_files：`hc-team/app/service/**` 其余（含 encoder_test.c，T2 已冻不再改）、`hc-team/app/system/**` 其余（sys_init/main 不改——两条目经 app_compose 装配，装配入口不变）、`hc-team/app/{ui,scheduler,tasks}/**`、`hc-team/driver/**`（仅调用 motor，不改）、`hc-team/middleware/**`、`board.syscfg`、`tests/host` 既有 `test_*.c`/`fake_*.c`（复用 fake_motor_hw）。

#### 23.3.2 公共接口（最小面）

```c
void MotorCheck_Start(void);            /* 相位复位=FORWARD、采样基准待播种；不立即发命令（首 Update 播种） */
void MotorCheck_Update(uint32_t now_ms);/* 首拍播种基准+相位输出；每拍 Motor_SetOutput(当前相位±200)刷看门狗 + Motor_Update(elapsed)；相位到 2000ms 翻转 FORWARD↔BACKWARD 循环 */
void MotorCheck_Stop(void);             /* Motor_BrakeAll（确定性停止，§8.1） */
```

- **驱动语义**：FORWARD → 左右轮 `Motor_SetOutput(±MOTOR_CHECK_OUTPUT)`，`MOTOR_CHECK_OUTPUT=200`；相位时长 `MOTOR_CHECK_PHASE_MS=2000`；循环直到 Stop。两轮**同向同幅**（一轮反转即 AI1/AI2 接反，肉眼可辨）。
- **单一所有者声明**：换向过零/死区/slew/100ms 命令超时归零/刹车真值表全部唯一在 `motor.c`（V12）；motor_check 只发 ±200 目标 + 每拍刷新（防 100ms 超时）+ Motor_Update(elapsed) 推进状态机 + Stop 时 Motor_BrakeAll。**不复做任何限幅/换向/超时逻辑**。
- **安全（§8.1）**：Start 不立即发命令（首 Update 播种）；Stop=确定性 BrakeAll；开机安全态由 SysInit `Motor_Init`+`Motor_BrakeAll` 保证（不改装配）；无活动条目＝不泵 motor_check。
- **前置条件**：System 装配层已完成 `Motor_Init()`。

#### 23.3.3 preserved_behavior

- 既有 service/driver/middleware 零改动；sys_init/main 不改（装配入口不变，仅 app_compose 条目表扩两行）；主机既有用例全过；固件行为：DEBUG 组多两条目，进 EncoderTest/MotorDir 才注册遥测/驱动电机，空转仍只泵 Menu_Tick。

#### 23.3.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/\|app/scheduler/\|app/ui/\|app/system/\|app/service/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/service/motor_check`，`#include` 行） | driver 命中仅 `motor`；`app/service/` 唯一命中 = 自身头 `motor_check.h`（非跨服务，修订1）；`app/tasks\|scheduler\|ui\|system` 与 DL HAL 前缀 0 命中 |
| E02 | 装配层闸门 | PowerShell：`& .claude/hooks/arch-scan.ps1 -Mode check` | 空输出（app_compose 新增 include 仅 encoder_test/motor_check Service 头，无 DL HAL 泄漏；app/system 仅禁 DL HAL） |
| E03 | 范围审计 | `git status` + `git diff --stat` 对照 §23.3.1 | 无越界改动 |
| E04 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥T2 总数+新用例 PASS / 0 FAIL，必含安全项：Start 后首 Update 前零命令、FORWARD 两轮 +200、2000ms 后翻 BACKWARD 两轮 −200、再 2000ms 回 FORWARD（循环）、每拍刷新命令（Motor_SetOutput 被调）、elapsed 正确传 Motor_Update、Stop→BrakeAll、两轮同向同幅 |
| E05 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、motor_check.o + app_compose.o 重编并经 linkInfo.xml 进链；main→Scheduler→EncoderTest/MotorDir 两条目 on_enter/step/exit 可达 |

#### 23.3.5 完成记录（Phase 2+3，代码本提交）

- 新建 `motor_check.{h,c}` + `test_motor_check.c`（5 用例：Start 前零命令、FORWARD 两轮 +200 且 500ms 不掉速、2s 翻 BACKWARD −200、再 2s 循环回 FORWARD、Stop 两轮刹车）；`app_compose.c` s_entries[] 增 EncoderTest/MotorDir + 三钩子 wrapper、s_debug_entries[]={0,1,2}；Debug 构建接线（本地 subdir + ORDERED_OBJS/-include）。
- E01 依赖纯净：driver 命中仅 `motor`，`app/service/` 唯一命中自身头，上层+DL HAL 0 命中；E02 `arch-scan -Mode check` 空输出（0 新增违规——app_compose 新增 include 仅 encoder_test/motor_check Service 头）；E03 范围仅 §23.3.1；E04 主机 **429 PASS**（含本服务 5 新用例）；E05 固件 exit 0、0 诊断、`motor_check.o`+`app_compose.o` 经 linkInfo.xml 进链（main→Scheduler→两条目可达，链接零未解析符号）。
- arch-auditor：§8.1 全过——`motor.c` slew/换向过零/死区/100ms 超时/刹车零复做、Start 不发命令（首 Update 播种）、Stop 确定性 BrakeAll 经 on_exit 可达、每拍 SetOutput 刷新防超时、相位翻转仅改 ±目标；无阻断/无重要/无建议。topo-updater：`app.md §3` MotorCheck 节点 + §4 启动图条目 x1→x3 同步。

### 23.3.6 契约修订记录

- **修订 1（施工前发现，单独提交）**：T2 §23.2.4 / T3 §23.3.4 的 E01 期望列修正。原因：grep 前缀集含
  `app/service/`，而每个 Service 的 `.c` 必然自包含 `app/service/<self>/<self>.h`——自身头命中不是跨服务
  依赖违规。期望列改为「driver 命中仅白名单 + `app/service/` 唯一命中=自身头 + 上层/DL HAL 前缀 0 命中」，
  与 chassis §6.4 先例（Service 层 E01 不把 `app/service/` 计入 0 命中集）对齐。grep 命令本身不变。

### 23.4 维护/收官

- 三任务各自：TDD 红→施工→逐行复现证据→arch-auditor→topo-updater→提交（Conventional Commits）。
- topo：encoder_test 第二 Encoder_Update 采样点登记 V21 扩条；menu RUN_ACTIVE 显示所有权契约修订；app_compose 条目表扩两条；索引 §10 追加日志。

## 24. Phase 4B —— W4：DEBUG 组新增 GrayTest（12 路灰度数字量遥测）条目

> 用户指令（2026-07-19）：编写灰度测试 debug 模块，VOFA 注册显示 12 路灰度值，通道 1~12
> 从上到下顺序注册，人工肉眼确认每一路 0/1（现场再调）。此模块完成后进入循迹环调试。
> 本节为 W4 契约，**本提交冻结全部证据行**；TDD/施工/审计/拓扑为后续独立闭环（契约先于代码）。

### 24.0 裁定与范围

- **落点 = App Service，仿 `encoder_test`（tx-only VOFA 遥测，无 cmd、无电机）**：新建
  `app/service/gray_check/`，接进 `app_compose.c` DEBUG 组第 4 条目。
- **命名裁定（避对象文件名冲突）**：目录/函数前缀取 **`gray_check` / `GrayCheck_`**（非
  `gray_test`）。理由：冻结旧世界 `app/tasks/gray_test/gray_test.c` 仍入 Debug 构建
  （`Debug/makefile:34` gray_test.o），扁平对象目录下重名 `gray_test.o` 会冲突。旧文件在
  forbidden 的 `app/tasks/**`，不触碰、不复用、不参照。**菜单标签仍用 ASCII `"GrayTest"`**
  （对用户语义），dir↔label 轻度不一致沿用既有先例（`motor_check` dir → `"MotorDir"` label）。
- **无播种拍（比 encoder_test 更简）**：`Gray_ReadDarkBitmap()` 是无状态原子读，**无 elapsed
  消费者**（不同于 encoder_test 播种是为算 elapsed 喂 `Encoder_Update`）。故 gray_check 无
  seed 标志：门控基准 `base=0`，进页首拍即 `now-0≥10` 发一帧（即时反馈）。这是 simplicity-first
  的最小正确形态，头注释写明理由。
- **第二读点登记（比 V21 更弱）**：gray_check 是 `Gray_ReadDarkBitmap()` 继 `line_follow.c:183`
  之后第二个调用点。但 gray 是**无状态原子读、无累计器**——两读点即便同拍也无数据冒险（不同于
  encoder 累计 double-count 的 V21 严重度）；且 scheduler 单活动条目不变量保证与 line_follow
  永不同拍。topo 更新时登记为「第二读点、无累计冒险、单活动条目互斥」，在 V21 追加 W4 补注或新条。
- **顺带修正（导航器 2026-07-19 发现）**：`api_architecture_topology.md` §7 覆盖表仍写 Gray
  Driver「零外部调用者」，与 §5.4 及源码 `line_follow.c:183`（S02 起消费）不一致；topo-updater
  阶段一并修正（不影响本契约设计）。
- **接口辩护（诊断能做什么）**：能进入/退出一次 12 路灰度数字量遥测（进即挂 VOFA tx×12、退清组）、
  能被周期推进（自读 12 路 0/1 发上位机）。仅此成为公共面。**不做**左右重排/反相/去抖/滤波/阈值
  （器件侧已做，gray.h:18-25；左右不自证，gray.h:28-34）——仅镜像原始通道序 bit0..bit11。

### 24.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/gray_check/gray_check.h` / `.c` | 新建 |
| `hc-team/app/system/app_compose.c` | 修改（+include gray_check；+3 钩子 wrapper；s_entries[] 增 GrayTest idx3；s_debug_entries[] 扩为 {0,1,2,3}） |
| `tests/host/test_gray_check.c` | 新建 |
| `tests/host/Makefile` | 追加 test_gray_check 目标/clean/.PHONY |
| `.gitignore` | 追加 test_gray_check / test_gray_check.exe |
| `Debug/makefile` | 登记 gray_check.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |

- **Debug 本地生成物**（`hc-team/app/service/gray_check/subdir_vars.mk`+`subdir_rules.mk`）按 §4
  规则本地生成、**不入库**（照抄 encoder_test 同名文件改路径），故不列 allowed_files。
- **forbidden_files**：`hc-team/app/tasks/**`（尤其 `app/tasks/gray_test/**`）、
  `hc-team/app/{scheduler,ui}/**`、`hc-team/app/service/{chassis,line_follow,tuning,encoder_test,motor_check}/**`
  （app_compose 只 include 不改）、`hc-team/driver/**`、`hc-team/middleware/**`、
  tests/host 既有 `test_*.c` 与 `fake_*.c`（fake_gray_port 已有 `FakeGrayPort_SetDarkChannels`
  注入面、fake_uart_port 已有 VOFA TX 抓取面——不改）。

### 24.2 公共接口（最小面）+ 单一所有者

```c
void GrayCheck_Start(void);            /* vofa_clear_profile → 注册 tx×12（G1..G12 = bit0..bit11，无 cmd）→ 门控基准置 0；不发电机命令 */
void GrayCheck_Update(uint32_t now_ms);/* 自门控 10ms（now_ms 无符号减法）：Gray_ReadDarkBitmap() → 逐位镜像 12 路 0/1 → vofa_run 发本拍帧 */
void GrayCheck_Stop(void);             /* vofa_clear_profile；本服务从不驱动电机，无电机需停 */
```

- **tx 组内容**：12 个 `int` 通道，注册序 = 通道序 = `ch_i ← (Gray_ReadDarkBitmap() >> i) & 1`
  （i=0..11 → G1..G12）。深色=1、浅色=0，直接来自器件比较器电平（gray.h 器件事实）。
- **单一所有者声明**：12 路原子读唯一属 gray driver（`Gray_ReadDarkBitmap` 一次 `DL_GPIO_readPins`，
  不退回逐路分读）；本服务只 `(bitmap>>i)&1` 单向复制到 12 个 tx，**零反相/去抖/滤波/阈值/左右重排**
  （加任何一样都构成第二所有者，违 §8.2；左右修正唯一点在上层循迹权重表 `bit0_is_left`，本诊断不碰）。
  VOFA 协议/解析/缓冲/串口归 uart_vofa/vofa_uart Driver。本服务唯一拥有：诊断 tx 组存储 + 发帧节奏。
- **前置条件**（System 装配层负责）：已完成 `vofa_init()`（含 VofaUart_Init）；灰度无 Init（gray.h：
  IO 输入由 SysConfig 配好，无内部状态）。

### 24.3 preserved_behavior

- 既有 service/driver/middleware、旧 `app/**`、`sys_init`/`main` 零行为改动（装配入口不变，仅
  app_compose 条目表扩一行 + 新 service）；主机既有 429 用例全过；固件行为：DEBUG 组多一条目
  GrayTest，进条目才注册灰度 tx 遥测，空转仍只泵 Menu_Tick。

### 24.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/\|app/scheduler/\|app/ui/\|app/system/\|middleware/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/service/gray_check`，`#include` 行） | 上层+DL HAL 前缀 0 命中；另核 driver/ 命中仅 `gray`+`uart_vofa`、`app/service/` 唯一命中=自身头 `gray_check.h`（自身头非跨服务违规，§23.3.6 修订1 同口径） |
| E02 | 装配层闸门 | PowerShell：`& .claude/hooks/arch-scan.ps1 -Mode check` | 空输出（app_compose 新增 include 仅 gray_check Service 头，无 DL HAL 泄漏；app/system 仅禁 DL HAL） |
| E03 | 范围审计 | `git status` + `git diff --stat` 对照 §24.1 | 无 allowed_files 之外的改动（`.ccsproject` 会话前既存，不计） |
| E04 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥434 PASS / 0 FAIL（429 基线 + ≥5 新用例），必含：Start 后到期一帧=12 通道（52 字节）；帧忠实镜像注入位图（bit0..bit11→ch0..ch11、深色=1 浅色=0）；单通道注入只对应 ch 亮（通道序恒等，catch 左右重排/反相）；10ms 门控（未到期无帧、到期一帧）；Stop 后清 profile 无帧 |
| E05 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、gray_check.o + app_compose.o 经 linkInfo.xml 进链；main→Scheduler→GrayTest 条目 on_enter/step/exit 可达 |

### 24.5 完成记录（代码本提交）

- 新建 `gray_check.{h,c}` + `test_gray_check.c`（5 用例：Start 后首拍即一帧=12 通道 52 字节、帧忠实镜像注入位图 bit0..bit11→ch0..ch11、逐路单置通道序恒等无重排、10ms 门控、Stop 清 profile）；`app_compose.c` s_entries[] 增 GrayTest idx3 + 三钩子 wrapper、s_debug_entries[]={0,1,2,3}；Debug 构建接线（本地 subdir + ORDERED_OBJS/两处 -include/clean）。
- E01 依赖纯净：`app/tasks|scheduler|ui|system\|middleware\|DL HAL` 前缀 0 命中；driver/ 命中仅 `gray`+`uart_vofa`，`app/service/` 唯一命中自身头 `gray_check.h`（自身头非跨服务违规）。E02 `arch-scan -Mode check` 空输出（app_compose 新增 include 仅 gray_check Service 头）。E03 范围仅 §24.1（`.ccsproject` 会话前既存不计；Debug 本地生成物 gitignore 未跟踪）。E04 主机 **434 PASS 0 FAIL**（429 基线 + 5 新用例）。E05 固件 exit 0、0 诊断、`gray_check.o`+`app_compose.o` 经 linkInfo.xml 确证进链（linkInfo 时间戳=构建时刻，非增量跳过），main→Scheduler→GrayTest 三钩子可达。
- arch-auditor：五维全合规、**无阻断/无重要/无建议级发现**——依赖矩阵（仅 gray+uart_vofa Driver）、单一所有者（位图单向 `(bitmap>>i)&1` 镜像零第二处理、第二读点无累计冒险单活动条目互斥）、ISR/资源所有权（无 ISR、VOFA 链归 uart_vofa）、无依据防御（省 s_seeded 播种拍是简化非缺陷）、诊断安全（tx-only 不驱动电机）。topo-updater：`app.md §3` 新增 GrayCheck_API 类块 + 2 条出边（→Gray/→uart_vofa）、§4 启动图补 GrayTest 分支（DEBUG 组 x3→x4）；索引 §5.4 灰度数据流补第二读点、§6 V21 追加 W4 补注、§7 新增 App Service gray_check 行 + **修正 Gray Driver 行**（原「零外部调用者」滞后 → 更正为 line_follow(S02 起)+gray_check(W4 起) 两消费者）、§10 追加 W4 日志。

## 25. Phase 4B —— W5：动态调参框架（TUNE 参数组 + 片内 flash 持久化 + 循迹外环增益首参数集）——冻结

> 用户指令（2026-07-19）：循迹环调参改为**按钮动态调参**（弃 VOFA 现场脱轨风险）。在 DEBUG 一级组
> 旁**新增平级 L1 参数组 TUNE**（`MENU_GROUP_PARAM`）：进 TUNE → 参数列表 → 选参数进 EDIT 看**实际值**，
> K1 增 / K2 减 / K3 确认 / K4 返回；参数末尾放 **SAVE** 项，K3 触发**一次性写片内 flash（掉电保存）**，
> 调参过程只改 RAM（即时生效可观察），满意再存。首参数集 = 循迹外环差速 PID 三增益。此框架为**今后
> 一切动态调参的模板**。调参增减单位（step）留命名占位常量，现场再定。
> 用户三问裁定（2026-07-19 本会话）：① 菜单结构 = 平级 L1 TUNE 组（零改两级内核）；② 存 flash 时机 =
> SAVE 项 K3 才写；③ 本次范围 = 只交付调参框架（循迹「运行条目」是紧接的独立下一闭环）。
> 本节为 W5 契约，**本提交冻结全部证据行**；TDD/施工/审计/拓扑为后续独立闭环（契约先于代码）。

### 25.0 裁定与范围

- **前端既存事实（导航器 2026-07-19 核实）**：按钮动态调参**前端在 UI01 已全部建成**——`Menu_Param_T{
  name, int32_t get(void), void set(int32_t), step}`、`MENU_GROUP_PARAM`、`MENU_SCREEN_PARAM_LIST/EDIT`、
  `menu_param.c` 的 K1=`set(get()+step)`/K2=`set(get()-step)`/K3=退出编辑/K4=返回、EDIT 经 `get()` 显示
  **实际值**——皆现成。缺口只在**装配层未接线**（`app_compose.c` 仅 DEBUG 一个 L1 组，零 PARAM 组）+
  **持久化层全仓不存在**（V17：EEPROM 器件驱动 P6 已删，**不复活**；新存储走全新 Driver）+ **SAVE 触发钩子**。
- **三层落点（依赖矩阵）**：① 片内 flash 编程/擦除 = 寄存器级 → 新 **Driver** `driver/param_store`
  （DL HAL 仅 `_hw.c`，仿 gray：`gray.c`+`gray_port.h`+`gray_hw.c`+`fake_gray_port.c` 范式）；
  ② 参数持久化编排（载入→应用→存回）= 业务逻辑 → 新 **App Service** `app/service/param_tune`
  （Service→Driver 合法、Service→Service 合法）；③ 菜单接线 = 装配层 `app_compose.c` + UI 层 `menu` 小加钩子。
- **SAVE 需一处 menu 小改（用户已知会并确认形状）**：现内核 K3-on-list = 进 EDIT，无任何 K3 触发
  accessor 的路径，故「SAVE 项按 K3 即写」**必然**要一处 menu 钩子。取**最小加法**：`Menu_Param_T` 加可选
  `void (*action)(void)`；PARAM_LIST 上 K3 命中 `action!=NULL` 项 → 调 `action()` 并**停留 LIST**（不进
  EDIT）；`action==NULL` 普通参数**行为一字不变**。~6 行、向后兼容、可复用（未来 RESET/APPLY 按钮）。
  这不是嵌套结构改动（两级模型不动），是 SAVE 语义的必然代价。
- **值所有权（Model A：line_follow 单一所有者 + param_tune 无副本胶水）**：外环 PID 增益**已应用值**唯一属
  `line_follow`（`LineFollow_SetGains` 写，新增对称 `LineFollow_GetGains` 读——读自持 `s_outer_pid.cfg.kp/ki/kd`，
  cfg 是 pid.h 为调用者暴露的字段，读自持实例 cfg 非越界私有态，故**不触 middleware/pid**）。`param_tune`
  **不持增益副本**：`get`→`LineFollow_GetGains`、`set`→`LineFollow_SetGains`（即时生效）、`save`→读
  `LineFollow_GetGains`→序列化→`ParamStore_Save`、`init`→`ParamStore_Read`(有效)/默认→`LineFollow_SetGains`。
  `param_tune` **唯一拥有**：持久化编排 + **int32 milli↔float ×1000 换算**（唯一 scale 所有者）+ 默认增益常量 +
  step 占位常量。**menu/menu_param 零换算、零限幅、零值副本**（越权则违 §8.2）。**tuning/S03 先例同款**（服务持
  cmd 值经 Chassis 公共 API 单向应用）——故 param_tune 与 tuning 是两条平行调参链（按钮 vs VOFA），互不联通。
- **显示的「实际值」= 真正已应用值**：`get()`→`LineFollow_GetGains` 读 PID cfg 实值（param_tune 是该 cfg
  增益的唯一写者路径），非任何缓存副本——满足用户「看见参数的实际值」。
- **接口辩护**：① `param_store`（器件能做什么）= 能把一段字节 blob 完整性存入/读回片内 NV（magic+版本+CRC16
  框定、擦前写、读回校验），掉电保持。② `param_tune`（调参链路能做什么）= 能开机载入持久增益并应用、能就地读/
  改一组循迹增益（即时生效）、能一次性存盘。仅此成公共面。
- **单次量化/换算/限幅所有者（§8.2 复查，param_tune/menu 一律不复做）**：误差量化=track_error、差速限幅=外环
  Pid cfg out_limit、增益应用=line_follow/pid、NV 完整性=param_store、milli↔float=param_tune。
- **本次不做（范围裁定③）**：循迹「运行条目」（真正驱动小车沿线跑，World-2 现零 line_follow 调用者）**不在本契约**
  ——它驱动电机、走 motor-safety、需标定 `LineFollow_Config_T` 全字段，作紧接的独立闭环。**登记接线注意**：
  未来该运行条目 `on_enter` 须在 `LineFollow_Init`（会把外环增益归零）**之后**调 `ParamTune_Init`（或后续加的
  `ParamTune_Apply`）重推持久增益，否则 Init 的归零会盖掉持久值。本契约 `ParamTune_Init` 在 `AppCompose_Install`
  开机调用（此刻无人 Init line_follow，增益从 0 起，param_tune 载入即为实值），菜单读/改/存全程自洽。

### 25.1 三任务分解（各自独立可验收，构建序 PT1→PT2→PT3，中途构建恒绿）

| 子任务 | 新增/改动 | 单一所有者 | 主机可证 | 固件行 |
|---|---|---|---|---|
| **PT1** `driver/param_store` | `param_store.{h,c}`(逻辑+框定) + `param_store_port.h`(seam) + `param_store_hw.c`(唯一触 DL_FlashCTL) + `fake_param_store_port.c` | NV 完整性：扇区、magic+版本+CRC16、擦前写、读回校验 | blob 往返、空/坏/超容/版本不符→Read false、program 失败注入→Save false | .o 进链、0 诊断 |
| **PT2** `app/service/param_tune` + `LineFollow_GetGains` | `param_tune.{h,c}`；`line_follow.{h,c}` 仅加对称 GetGains | 持久化编排 + milli↔float ×1000 + 默认/step 常量 | 空 flash→默认应用、有效→持久应用、set 即时生效、Save 往返、scale 往返 | .o 进链、0 诊断 |
| **PT3** menu action 钩子 + TUNE 接线 | `menu.h`(+action 字段)+`menu_param.c`(K3-on-action)；`app_compose.c`(TUNE 组+参数表+SAVE+开机 ParamTune_Init)；`test_menu.c` 追加 | 无（胶水+导航派发） | K3-on-action 调回调且停 LIST、普通参数行为不变、action 项不进 EDIT 故 get 不被调 | TUNE 现身、.o 进链、0 诊断 |

### 25.2 PT1 —— `driver/param_store` 契约

#### 25.2.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/driver/param_store/param_store.h` / `.c` | 新建（框定逻辑：magic+版本+CRC16、擦前写、读回校验；可主机测试） |
| `hc-team/driver/param_store/param_store_port.h` | 新建（硬件 seam，仿 gray_port.h） |
| `hc-team/driver/param_store/param_store_hw.c` | 新建（**唯一** DL_FlashCTL：擦扇区/编程/读；不入主机链接，仿 gray_hw.c；含扇区地址/字对齐） |
| `tests/host/fake_param_store_port.c` | 新建（RAM 顶替 seam：擦=0xFF、编程写入、读回、擦/编程失败注入、Reset） |
| `tests/host/test_param_store.c` | 新建 |
| `tests/host/Makefile` | 追加 test_param_store 目标/clean/.PHONY |
| `.gitignore` | 追加 test_param_store / test_param_store.exe |
| `Debug/makefile` | 登记 param_store.o + param_store_hw.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |

- **Debug 本地生成物**（`hc-team/driver/param_store/subdir_vars.mk`+`subdir_rules.mk`）按 §4 本地生成、**不入库**（照抄 gray 同名文件改路径），不列。
- **forbidden_files**：`hc-team/app/**`、`hc-team/middleware/**`、`hc-team/driver/**` 其余全部、tests/host 既有 `test_*.c` 与既有 `fake_*.c`。

#### 25.2.2 公共接口（最小面）

```c
/* NV blob 存储：单一专用片内 flash 扇区，magic+版本+CRC16 框定。拥有完整性，调用方拥有 payload 语义。 */
bool ParamStore_Read(uint8_t *buf, uint16_t len);
    /* 有有效记录（magic/版本/CRC 正确且记录 len==len）→ 拷 payload 入 buf 返 true；否则 buf 不动返 false。 */
bool ParamStore_Save(const uint8_t *buf, uint16_t len);
    /* 擦扇区 → 编程框定记录 → 读回校验；len 超 port 容量或 port 失败或校验不符 → false。 */
```

- **框定**：`{magic(u16), format_ver(u8)=1, len(u16), payload[len], crc16}`；format_ver = **记录格式版本**（框定层），
  payload 内**语义版本由 param_tune 自持首字节**（param_store 对 payload 不可知）。空扇区(全 0xFF)→magic 不符→Read false。
- **单一所有者**：片内 flash 擦/写/读唯一属 `_hw.c`（DL_FlashCTL）；CRC16/框定唯一属 `param_store.c`；**无重试/无 wear-leveling**（单扇区、SAVE 稀发，占位不预建，§simplicity）。
- **前置**：`param_store_hw.c` 的扇区地址不与 `.out` 代码/BSL 段重叠（链接脚本核对，硬件由用户上板验证——擦/写真值非主机可证，§Phase1.6）。

#### 25.2.3 preserved_behavior

- 既有 driver/middleware/service、旧 `app/**` 零改动；主机既有 434 用例全过；固件：param_store.o+param_store_hw.o 进链但**零调用者**（PT2 前）。

#### 25.2.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/\|middleware/`（path=`hc-team/driver/param_store`，`#include` 行） | 0 命中（Driver 不含上层；DL HAL 仅 `_hw.c` 属矩阵允许，由 E02 守） |
| E02 | 装配层闸门 | PowerShell：`& .claude/hooks/arch-scan.ps1 -Mode check` | 空输出（无跨层泄漏；DL HAL 收敛在 _hw.c） |
| E03 | 范围审计 | `git status` + `git diff --stat` 对照 §25.2.1 | 无 allowed_files 之外改动（`.ccsproject` 会话前既存不计） |
| E04 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥440 PASS / 0 FAIL（434 基线 + ≥6）：空扇区→Read false；Save→Read 往返一致；program 失败注入→Save false 且随后 Read false；CRC 篡改（经 port 翻位）→Read false；超容量 len→Save false；二次 Save 覆盖（擦除生效）→Read 得新值 |
| E05 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 诊断、param_store.o + param_store_hw.o 经 linkInfo.xml 进链（零调用者） |

#### 25.2.5 完成记录（代码本提交）

- 新建 `driver/param_store/`：`param_store.h`（公共面 Read/Save + MAX_PAYLOAD=48）、`param_store.c`（magic 0x50A5 + 格式版本 1 + len + CRC16-CCITT 框定、擦前写、读回校验；static 记录/校验缓冲避 256B 栈溢出）、`param_store_port.h`（seam：capacity/erase/program/read）、`param_store_hw.c`（唯一 DL_FlashCTL：末 1KB 扇区 0x0007FC00，executeClearStatus→unprotectSector→eraseMemoryFromRAM / programMemoryFromRAM64WithECCGenerated，照搬 SDK eeprom_emulation_type_a）。
- 新建 `tests/host/fake_param_store_port.c`（RAM 顶替、NOR &= 语义、擦/写失败注入 + Poke）+ `test_param_store.c`（8 用例）。Makefile/.gitignore/Debug/makefile（ORDERED_OBJS + 两处 -include + clean）+ 本地 subdir_*.mk（不入库）。
- E01 依赖纯净：`app/|middleware/` 在 param_store 目录 0 命中（DL HAL 仅 _hw.c，矩阵允许）。E02 arch-scan -Mode check 空输出。E03 范围仅 §25.2.1（`.ccsproject` 会话前既存不计；Debug 本地生成物未跟踪）。E04 主机 **442 PASS 0 FAIL**（434 基线 + 8：空扇区拒读、往返、擦前写覆盖、program/erase 失败注入拒存、CRC 篡改拒读、超容拒存、长度不符拒读）。E05 固件 exit 0、0 诊断、`param_store.o`+`param_store_hw.o` 经 linkInfo.xml 确证进链（时间戳=构建时刻 8:51:11，.out 重链），DL_FlashCTL FromRAM 符号正常链接。零调用者（预期态，PT2 前）。
- arch-auditor/topo-updater：随 W5 三任务合并在 PT3 后一并评审同步（三任务同一契约/特性）。

### 25.3 PT2 —— `app/service/param_tune` + `LineFollow_GetGains` 契约

#### 25.3.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/param_tune/param_tune.h` / `.c` | 新建（持久化编排 + milli↔float + 默认/step 常量 + 菜单 accessor） |
| `hc-team/app/service/line_follow/line_follow.h` / `.c` | **修改（仅加）**：`LineFollow_GetGains(float*,float*,float*)`，读 `s_outer_pid.cfg.kp/ki/kd`；无其他改动 |
| `tests/host/test_param_tune.c` | 新建 |
| `tests/host/Makefile` | 追加 test_param_tune 目标/clean/.PHONY |
| `.gitignore` | 追加 test_param_tune / test_param_tune.exe |
| `Debug/makefile` | 登记 param_tune.o（ORDERED_OBJS、两处 -include、clean） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |

- **Debug 本地生成物** `hc-team/app/service/param_tune/subdir_*.mk` 本地生成不入库。
- **forbidden_files**：`hc-team/app/service/**` 其余（line_follow 仅加 GetGains、余只调不改）、`hc-team/app/{tasks,scheduler,system,ui}/**`、`hc-team/driver/**` 其余（param_store 仅调用不改）、`hc-team/middleware/**`（**含 pid**——不加 Pid_GetGains，走 cfg 直读）、tests/host 既有 `test_*.c` 与 `fake_*.c`（fake_param_store_port 由 PT1 建，本任务只用不改）。

#### 25.3.2 公共接口（最小面）

```c
void ParamTune_Init(void);       /* ParamStore_Read 有效→反序列化→LineFollow_SetGains；否则默认常量→SetGains。开机装配调用一次。 */
int32_t ParamTune_GetKp_milli(void);  /* → LineFollow_GetGains 读回 × 1000（实值，非副本） */
int32_t ParamTune_GetKi_milli(void);
int32_t ParamTune_GetKd_milli(void);
void ParamTune_SetKp_milli(int32_t v);/* → LineFollow_SetGains(v/1000, …)（保 ki/kd 现值）；即时生效 */
void ParamTune_SetKi_milli(int32_t v);
void ParamTune_SetKd_milli(int32_t v);
void ParamTune_Save(void);       /* 读 LineFollow_GetGains → 序列化 {schema_ver=1, kp/ki/kd milli} → ParamStore_Save */
```

- **blob payload**：`{schema_ver(u8)=1, kp_milli(i32), ki_milli(i32), kd_milli(i32)}`（13B）；`ParamStore_Read` 后 schema_ver≠1 → 用默认。
- **常量（命名占位，现场再定）**：`TUNE_DEFAULT_KP/KI/KD_MILLI`（初值 0 = 未标定，上车 K1 上调）、`TUNE_STEP_KP/KI/KD_MILLI`（step，初值占位如 10 = 0.010）。menu 参数表的 `step` 引用这些常量。
- **单一所有者**：已应用增益=line_follow（Set/Get）；持久化+milli↔float+默认/step=param_tune；NV 完整性=param_store。param_tune 无增益副本（Model A）。
- **`LineFollow_GetGains`**：纯加读接口，读自持 `s_outer_pid.cfg`（对称 SetGains）；不改 line_follow 任何行为、不动 middleware/pid。

#### 25.3.3 preserved_behavior

- `line_follow` 除新增 GetGains 外零行为改动；其余 service/driver/middleware、旧 `app/**` 零改动；主机既有（PT1 后）用例全过；固件：param_tune.o 进链但零调用者（PT3 前）。

#### 25.3.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/\|app/scheduler/\|app/ui/\|app/system/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/service/param_tune`，`#include` 行） | 上层+DL HAL 前缀 0 命中；另核 driver/ 命中仅 `param_store`、app/service/ 命中仅 `line_follow`（+自身头 param_tune.h） |
| E02 | 装配层闸门 | PowerShell：`& .claude/hooks/arch-scan.ps1 -Mode check` | 空输出 |
| E03 | 范围审计 | `git status` + `git diff --stat` 对照 §25.3.1 | 无越界（line_follow diff 仅 GetGains 增量） |
| E04 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥446 PASS / 0 FAIL（PT1 后 +≥6）：Init 空 flash→默认增益应用（GetGains 读回=默认）；Init 有效 flash→持久增益应用；SetKp_milli(1234)→LineFollow_GetGains 读回 kp≈1.234（scale）；set 后无需 Save 即生效；Save→重 Init 往返恢复（非默认）；GetKp_milli(set 后)==设定 milli（scale 往返无损） |
| E05 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 诊断、param_tune.o 经 linkInfo.xml 进链（零调用者） |

#### 25.3.5 完成记录（代码本提交）

- 新建 `app/service/param_tune/`：Model A 无副本胶水——get→LineFollow_GetGains、set→LineFollow_SetGains（保另两增益）、save→读回增益序列化 ParamStore_Save、init→读盘(schema_ver=1)/默认→SetGains。blob 13B（schema_ver + kp/ki/kd milli LE）。milli↔float ×1000 四舍五入唯一在此。TUNE_STEP_*_MILLI 占位常量导出供 PT3 菜单表引用；默认增益 0（上车再调）。
- `line_follow.{h,c}` 加对称 `LineFollow_GetGains`（读自持 `s_outer_pid.cfg`，additive，+15 行，零行为改动）。
- 新建 `test_param_tune.c`（6 用例）。Makefile（线性 line_follow 依赖集 + param_store + fake_param_store_port）/.gitignore/Debug/makefile（ORDERED_OBJS + 两处 -include + clean）+ 本地 subdir_*.mk（不入库）。
- E01 依赖纯净：param_tune 目录 include 仅 param_tune.h + line_follow.h（Service→Service）+ param_store.h（Service→Driver）+ stdint；上层/DL HAL 前缀 0 命中。E02 arch-scan 空。E03 范围仅 §25.3.1（line_follow diff 纯 GetGains 增量；`.ccsproject` 不计）。E04 主机 **448 PASS 0 FAIL**（442 基线 + 6：空 flash 默认、set 即时抵达 line_follow、Save→Init 往返恢复、未存不持久、milli↔float 正负往返、单增益保另两）。E05 固件 exit 0、0 诊断、`param_tune.o` 经 linkInfo.xml 确证进链（.out 重链 21:00:01）。零调用者（预期态，PT3 前）。

### 25.4 PT3 —— menu action 钩子 + `app_compose` TUNE 接线 契约

#### 25.4.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/ui/menu/menu.h` | 修改（`Menu_Param_T` 加可选 `void (*action)(void)` 字段 + 文档；余不动） |
| `hc-team/app/ui/menu/menu_param.c` | 修改（PARAM_LIST K3：`action!=NULL`→调 `action()` 停 LIST；否则原路进 EDIT） |
| `hc-team/app/system/app_compose.c` | 修改（+include param_tune；TUNE 组 `Menu_Param_T` 表[LF Kp/Ki/Kd + SAVE(action=ParamTune_Save)]；s_groups[] 加 `{"TUNE", MENU_GROUP_PARAM, NULL,0, s_tune_params, N}`；`AppCompose_Install` 加 `ParamTune_Init()`） |
| `tests/host/test_menu.c` | 修改（追加 action-item 用例） |
| `Debug/makefile` | 按需（menu/menu_param/app_compose 已登记；无新 .o，通常不改） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |

- **forbidden_files**：`hc-team/app/service/**`（param_tune 仅调用不改、余不碰）、`hc-team/app/{tasks,scheduler}/**`、`hc-team/app/ui/menu/menu.c`（导航内核两级模型**不动**——action 派发落在 menu_param.c 的 PARAM_LIST 分支）、`hc-team/driver/**`、`hc-team/middleware/**`、tests/host 其余 `test_*.c` 与 `fake_*.c`。
- **说明**：`menu.c` 是否需动取决于 K3-on-list 的分派是否全在 menu_param.c——现 `MenuParam_Handle` 已独立处理 PARAM_LIST 的 ENTER，故 action 派发**只在 menu_param.c**，menu.c 保持 forbidden。施工发现必须动 menu.c 时按闭环铁律**单独提交修订本契约**再改。

#### 25.4.2 公共接口变更（最小面）

```c
typedef struct {
    const char *name;
    int32_t   (*get)(void);
    void      (*set)(int32_t v);
    int32_t     step;
    void      (*action)(void);   /* 新增可选：非 NULL 时 PARAM_LIST 上 K3 调它并停 LIST（get/set/step 忽略）；
                                    NULL = 原语义（K3 进 EDIT，UP/DOWN 调 set(get()±step)）。 */
} Menu_Param_T;
```

- **SAVE 项** = `{ "SAVE", NULL, NULL, 0, ParamTune_Save }`（action 非空 → K3 即存、不进 EDIT；get/set 为 NULL 且永不被调）。
- **NULL 安全**：action 项不进 EDIT，`render_edit`/UP/DOWN 的 `get()/set()` 路径对其永不触达；`render_list` 只用 `name`。既有普通参数（action==NULL）**分支与行为一字不变**。

#### 25.4.3 preserved_behavior

- `menu.c` 导航内核、hmi、既有 service/driver/middleware 零改动；既有 menu 用例全过（普通参数路径不变）；固件：main→Scheduler 空转仍只泵 Menu_Tick，GROUP_LIST 由 DEBUG 单组变 DEBUG+TUNE 两组，进 TUNE 才动 param_tune；仍只经 Hmi_API（V14 不复活直调）。

#### 25.4.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `driver/\|middleware/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/ui/menu`，`#include` 行） | 0 命中（menu 仍只经 hmi/scheduler，action 是函数指针不新增 include） |
| E02 | 装配层闸门 | PowerShell：`& .claude/hooks/arch-scan.ps1 -Mode check` | 空输出（app_compose 新增 include 仅 param_tune Service 头，无 DL HAL） |
| E03 | 范围审计 | `git status` + `git diff --stat` 对照 §25.4.1 | 无越界（menu.c 无 diff、内核未动） |
| E04 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥PT2+3 PASS / 0 FAIL：PARAM_LIST 上 K3 命中 action 项→调用 action 回调（计数+1）且界面停 PARAM_LIST（不进 EDIT）；action==NULL 普通项 K3→进 PARAM_EDIT（既有行为回归）；action 项全程不调 get()（NULL-safe 观测） |
| E05 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 诊断、app_compose.o+menu.o+menu_param.o+param_tune.o+param_store.o 经 linkInfo.xml 进链；GROUP_LIST 含 DEBUG+TUNE，PARAM 路径可达 ParamTune_* 与 SAVE action |

#### 25.4.5 完成记录（代码本提交）

- `menu.h`：`Menu_Param_T` 加可选 `void (*action)(void)`（第 5 字段），文档区分普通项/动作项。
- `menu_param.c`：PARAM_LIST 上 K3 命中 `action!=NULL` → 调 `action()` 停留列表（不进 EDIT）；`action==NULL` 原路进 EDIT。**menu.c 导航内核零改动**（K3-on-list 分派本就独立在 menu_param.c）。
- `app_compose.c`：+include param_tune；`s_tune_params[]`（LF Kp/Ki/Kd 引用 TUNE_STEP_*_MILLI + SAVE 项 action=ParamTune_Save）；`s_groups[]` 增平级 `{"TUNE", MENU_GROUP_PARAM, …}`；`AppCompose_Install` 末加 `ParamTune_Init()` 开机载入持久增益。
- `test_menu.c`：k_params 补第 5 字段 NULL；加动作项 3 用例（调用且停列表、可重复、普通项仍进 EDIT）。
- E01 依赖纯净：`driver/|middleware/|DL HAL` 在 app/ui/menu 0 命中（action 是函数指针，不新增 include）。E02 arch-scan 空（app_compose 新增仅 param_tune Service 头）。E03 范围仅 §25.4.1，**menu.c diff 为空**（内核未动；`.ccsproject` 不计）。E04 主机 **451 PASS 0 FAIL**（448 基线 + 3；menu 全 21 用例过，普通参数路径回归无损）。E05 固件 exit 0、0 诊断/0 警告、app_compose.o+menu.o+menu_param.o+param_tune.o+param_store.o 经 linkInfo.xml 进链（.out 重链 21:06:32），GROUP_LIST 含 DEBUG+TUNE。
- **W5 三任务全绿**：调参框架落地——板载 TUNE 组按钮调循迹外环增益、掉电 flash 保存。真实 flash 擦/写为硬件边界待用户上板验证。arch-auditor/topo-updater 见下。

### 25.5 待决/风险登记（拓扑同步时落地）

- **V17**（EEPROM 器件驱动 P6 已删，closed）：param_store 是**全新片内 flash Driver**，不复用/不复活 `driver/eeprom/`。
- **V14**（UI 直调 Driver，S04/UI01 已供替代面，open 待 T01）：本次 menu 改动仍全程经 Hmi_API，不新增绕过。
- **未来接线注意（非违规，登记）**：循迹「运行条目」闭环时，其 `on_enter` 须在 `LineFollow_Init`（归零外环增益）**之后**调 `ParamTune_Init`/`Apply` 重推持久增益，否则 Init 归零盖掉持久值。
- **第二写者核查**：`LineFollow_SetGains` 在本世界唯一调用者 = param_tune（Grep 证实）；无第二增益写者，Model A 单一所有者成立。

### 25.6 arch-auditor 评审处置（2026-07-19，PT3 后覆盖 W5 三提交）

依赖矩阵（A）、单一所有者（B）、无依据防御/simplicity（C）、ISR/资源所有权（D）、接线正确性（E）
**五项全 PASS，无阻断级、无重要级发现**。三条建议级处置：

1. **[建议·已登记为风险，不本轮改]** 参数扇区 `0x0007FC00` 未在链接脚本 carve（`Debug/device_linker.cmd`
   `FLASH origin=0x0 length=0x80000`，`.text` 等无差别 `> FLASH`）。当前镜像 ≪508KB 不触发；但代码若跨
   `0x7FC00`，链接器会静默把代码放进该扇区，`ParamTune_Save` 的 erase 会擦活代码→上电 HardFault、**无编译期
   报错**。**处置**：`device_linker.cmd` **非 git 跟踪**（Debug/ 仅 makefile 入库，该文件是 CCS/SysConfig 生成物），
   本 agent 编辑它既不持久也可能被重生成覆盖——故**不擅改**。正解须落在可跟踪的硬件配置：在 `MEMORY` 单列
   `PARAM_FLASH: origin=0x0007FC00 length=0x400` 且 `FLASH length=0x7FC00`，使重叠在链接期即报错。**移交用户**
   作为硬件配置侧一次性跟进（属 board 配置权，非 W5 Driver 代码问题）。hw.c:6-11 注释已自陈此假设。
2. **[建议·登记为已知局限，留后续]** `ParamTune_Save` 丢弃 `ParamStore_Save` 返回值（param_tune.c:119）+ 菜单
   动作项签名 `void(*)(void)` 无返回通道 → flash 编程失败（erase 成功、program 失败）时扇区已空，Save 静默
   「成功」，下次上电 `ParamTune_Init` 读空扇区→默认增益（0/0/0）→已标定增益丢失。**非数据损坏**（`test_program_
   fail_rejects` 已证失败后不读半写脏记录），是**可观测性缺口**。加反馈须给菜单动作项一条最小回显通道（如
   SAVE 后 OLED "SAVE OK/FAIL"），属独立 App 健壮性特性，超 W5 范围——**登记，建议后续立项**（不本轮过建）。
3. **[建议·保留]** `param_store.c:99-102` 容量检查在当前常量（MAX_PAYLOAD=48，total≤56 < 端口 1024）恒不触发。
   保留为跨契约低成本断言（成本极低，抬高 MAX_PAYLOAD 或接更小扇区端口时即有意义）；不删、不本轮改。审计
   已明示「低优先、供主 agent 裁量、非阻断」。

**结论**：W5 三提交分层与单一所有者链路与声明一致，评审放行。建议 1、2 移交用户/后续立项，不阻断收官。

## 26. W6 契约（LineFollow 循迹运行条目接入 DEBUG 组）——冻结

- **task_id**: RE01-line_follow_entry
- **goal**: `app_compose.c` 的 `s_entries[]` 新增第 5 条运行条目「LineFollow」并挂进 DEBUG 组
  （`s_debug_entries` idx4）。菜单进入该条目 →
  - `on_enter`：`LineFollow_Init(&s_lf_cfg)`（存配置、外环增益归零）→ `ParamTune_Init()`
    （重推 SAVE 的持久增益，**关闭 V28 接线注意**，否则会用零增益跑一拍）→
    `(void)LineFollow_Start()`（配置有效则 TRACKING；无效安全保持 IDLE 不动底盘）；
  - `on_step`：`LineFollow_Update()`（10ms 自门控，TRACKING/RECOVERING 期末尾恒推
    `Chassis_Update()` 沿线跑；`now_ms` 不用，服务自持 Clock 门控——与 speedtune_step 同构）；
  - `on_exit`：`LineFollow_Stop()`（→IDLE + `Chassis_Stop()` 确定性停车）。
  - 配置 `s_lf_cfg` = 保守 UNCALIBRATED 占位常量（低速起步、有界超时、`element_enable_mask=0`，
    几何用头文件建议式 `recovery≈2.7×pitch`）；外环 PID 增益**不在此表**（走 TUNE 组 / param_tune
    持久化），故占位配置与现场调参无冲突。用户 2026-07-19 三裁定：**保守占位+醒目标注 / DEBUG 组第 5 条 / 只跑不接遥测**。

- **Architecture**：
  - Abstraction：一条可从板载菜单进入的循迹运行条目——进页起步循迹、退页安全停车，
    配合 TUNE 组构成「选它跑 → 改增益 SAVE → 再选它跑 → 看效果」的现场调参闭环。
  - Hidden state：文件静态 `s_lf_cfg` 配置常量 + 三个钩子适配函数，全落 `app_compose.c`（装配根）。
  - Owner layer：App 装配层（`app/system`）——运行条目接线，**非新 Service**。钩子仅把
    `Scheduler_Entry_T` 签名适配到既成 `LineFollow_*` / `ParamTune_*` Service，与 `speedtune_*` 适配 `Tuning_*` 同构。
  - Allowed dependency direction：`app/system` → `app/service`（`line_follow.h`、`param_tune.h` 均 Service 头；System 是装配根，§3.6 允许）。

### 26.1 allowed_files（无 glob）

allowed_files：
- `hc-team/app/system/app_compose.c`（修改：+include line_follow.h；3 个 linefollow_* 钩子 + `s_lf_cfg` 常量；`s_entries[]` 加第 5 条；`s_debug_entries[]` 加 idx4）
- `agent/phase4_app_rewrite/plan_app_first_order.md`（本契约 §26 + 状态表 SYS01 行追加 + 交付说明）
- `agent/api_architecture_topology.md`（§6 V28「接线注意」关闭一份 + §10 更新日志）
- `agent/topology/app.md`（若 `s_entries[]`/DEBUG 组条目图在此文件——topo-updater 收工判定）

forbidden_files：
- `hc-team/app/service/**`（`line_follow` / `param_tune` / `tuning` 等仅调用不改）
- `hc-team/app/{tasks,scheduler,ui}/**`（`scheduler.h` 仅 include 不改）
- `hc-team/app/system/sys_init.c`、`hc-team/app/system/main.c`、`hc-team/app/system/app_compose.h`（装配顺序与前置不变，零触碰）
- `hc-team/driver/**`、`hc-team/middleware/**`
- `Debug/makefile`、`hc-team/app/system/subdir_*.mk`（无新 .o，不改）
- `tests/host/**`（`app_compose.c` 不入主机编译，无新用例）

### 26.2 单一所有者（不复做）

- **外环增益**：`line_follow`（Set/Get），`param_tune` 重推是唯一持久化推入路径——本条目 `on_enter`
  只调 `ParamTune_Init()`，不自持/不复算增益。
- **Chassis_Update 泵送**：`line_follow` 在 TRACKING/RECOVERING 独占；scheduler **单活动条目不变量**
  保证本条目与 tuning/motion 不同时驱动（**V21 对本条目关闭一份**）。
- **差速限幅/换向/超时/斜率**：全归下游 `line_follow` 外环 PID cfg / `speed_plan` / `lost_line` /
  `motor.c`，本装配层零复做（§8.1 既有保护不重复实现）。
- **配置标定量**：占位 UNCALIBRATED 常量唯一在 `app_compose.c`；现场经 TUNE 组调 PID 增益。

### 26.3 preserved_behavior

- 既有 4 条 DEBUG 条目（SpeedTune/EncoderTest/MotorDir/GrayTest）+ TUNE 参数组 + 开机 `ParamTune_Init` 行为零改动。
- `main→Scheduler_Run` 空转仍只泵 `Menu_Tick`；进 LineFollow 条目才注册/驱动，退页刹停。
- scheduler / menu / hmi / 所有 service / driver / middleware 零改动；`sys_init` 装配序不变。

### 26.4 证据行（≤6，恰 1 条固件构建行）

| ID | 项 | 命令 | 期望 |
|---|---|---|---|
| E01 | 范围审计 | `git status` + `git diff --stat` 对照 §26.1 | 无 allowed_files 之外改动（尤其零触碰 sys_init.c/main.c/app_compose.h/service/driver/middleware） |
| E02 | 装配层闸门 | PowerShell：`& .claude/hooks/arch-scan.ps1 -Mode check` | 空输出（app_compose 新增 include 仅 `line_follow.h` Service 头，无 DL HAL/Driver/Middleware） |
| E03 | 接线序 + V28 关闭 | Grep `app_compose.c` linefollow_enter 体 | 序为 `LineFollow_Init(&s_lf_cfg)` → `ParamTune_Init()` → `LineFollow_Start()`（Init 归零后重推持久增益，非零增益跑一拍） |
| E04 | 安全停 + 保守配置 | Grep `app_compose.c`（linefollow_exit + `s_lf_cfg`） | on_exit = `LineFollow_Stop()`（→IDLE+Chassis_Stop）；`s_lf_cfg` 含 UNCALIBRATED 标注、`straight_speed_mps`≤0.30 且 `min_speed`≤straight、`lost_timeout_ms`>0 有界、`element_enable_mask=0` |
| E05 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | =451 PASS / 0 FAIL（app_compose 不入主机编译，0 新用例，证无回归） |
| E06 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 诊断、`app_compose.o` 经 linkInfo.xml 进链，`LineFollow_Init/Start/Update/Stop` 与 `ParamTune_Init` 可达 |

### 26.5 Stop conditions

- `LineFollow_*` 或 `ParamTune_*` 公共面与本契约声称不符（签名/前置）→ 停止报告。
- 需改 `sys_init` 装配序或 `Chassis_Init` 位置才能起步 → 停止报告（前置应已成立）。
- 需在装配层复做增益/限幅/超时/换向 → 停止报告（单一所有者违规）。
- baseline drift：BUILD 起测 host ≠ 451 PASS → 停止，先改契约。

### 26.6 契约修订记录

- 冻结（4aab90b）：任务范围与 6 证据行按用户 2026-07-19 三裁定确定；基线 451 PASS 实测锁定。
- 验收（代码 b907003）：6 行全过——E01 仅 app_compose.c 在范围（.ccsproject 会话前既存未纳入）/ E02 arch-scan 空输出 exit 0 / E03 linefollow_enter 序 `LineFollow_Init(&s_lf_cfg)`→`ParamTune_Init()`→`(void)LineFollow_Start()`（V28 关闭）/ E04 linefollow_exit=`LineFollow_Stop()`、s_lf_cfg straight 0.25≤0.30 且 min 0.12≤straight、lost_timeout 1500>0、element_enable_mask=0、UNCALIBRATED 标注在场 / E05 451 PASS 0 FAIL / E06 exit 0、0 诊断、app_compose.o 经 linkInfo.xml 进链、LineFollow_Init/Start/Update/Stop+ParamTune_Init `.text` 可达。arch-auditor 六红线全过无发现放行。

## 27. M04 + S06c 契约（move_profile 距离梯形剖面 Middleware + motion 定长直行原语）——冻结

- **task_id**: MS01-profiled_straight
- **goal**: motion 新增定长直行原语 `Motion_StartProfiledStraight(distance_mm, heading_hold)`，
  纵向基速由**新 Middleware `move_profile`** 产出的「按距离参数化梯形剖面」前馈驱动
  （加速-匀速-减速，减速段收敛到 0），横向沿用既有航向保持 PID；旧 `MOTION_STRAIGHT`
  恒速原语一字不改。用户 2026-07-19 裁定：Q1 扩 motion 不扩 chassis；口径①加新原语不动旧；
  口径②剖面放新 Middleware；**纵向控制律选「按距离梯形剖面·无纵向 PID」**（剖面本身即位置闭环，
  第二个纵向速度所有者会踩 §8.2；近终点静摩擦失速风险按 TURN 同款处理，交标定+调用者超时，不加下限）。

- **Architecture**：
  - Abstraction（器件/算法能做什么）：
    - `move_profile`：给定「已行进距离 + 目标总距离 + 加减速标定」，算出这一拍该给多快的前馈基速——
      「定长运动的速度剖面」这一数据变换的唯一所有者。纯函数、无状态、无采样。
    - motion 新原语：走一段指定距离，起步平滑加速、中段匀速、末段平滑减速停准（可选航向保持走直）。
  - Hidden state：`move_profile` **无隐藏状态**——剖面是 (dist_done, target, cfg) 的纯函数，
    每拍以实测距离重算（这正是它「自带位置反馈」的原因，区别于 speed_plan 的有状态斜坡）；
    motion 侧新增私有态仅复用既有 `s_ref_*`/`s_target`/`s_progress`（新原语与直行同构），profile cfg 由 `s_cfg` 扁平字段本地组装。
  - Owner layer：`move_profile` = Middleware（纯算法）；新原语 = App Service（motion 内）。
  - Allowed dependency direction：`app/service/motion` → `middleware/move_profile`（Service→Middleware，§4 允许）；
    `move_profile` → 仅 `<math.h>`/`<stddef.h>`，**不含**任何 Driver/App/其他 Middleware 头。

### 27.1 allowed_files（无 glob）

allowed_files：
- `hc-team/middleware/move_profile/move_profile.h`（新建：`MoveProfile_Config_T` + `MoveProfile_Speed()` 纯函数）
- `hc-team/middleware/move_profile/move_profile.c`（新建：距离参数化梯形剖面实现）
- `hc-team/app/service/motion/motion.h`（修改：+`MOTION_PROFILED_STRAIGHT` 态、+4 个 `profile_*` 扁平 cfg 字段、+`Motion_StartProfiledStraight` 声明）
- `hc-team/app/service/motion/motion.c`（修改：+include move_profile.h、+`Motion_StartProfiledStraight`、+`motion_step_profiled_straight`、Update switch + Telemetry active 各加一分支）
- `tests/host/test_move_profile.c`（新建：剖面单测）
- `tests/host/test_motion.c`（修改：+定长直行用例）
- `tests/host/Makefile`（修改：+`MOVE_PROFILE_SRC` 变量、`test_move_profile`/`run_move_profile` 目标、`all`/`.PHONY`/`clean` 登记；`test_motion` 与 `test_route` 链接行 +`$(MOVE_PROFILE_SRC)`）
- `Debug/makefile`（修改：link objs 加 `move_profile.o`、两条 `-include .../move_profile/subdir_*.mk`、clean 列表补 `.o`/`.d`——仓库唯一跟踪的构建接线文件，§3 line 104）
- `agent/phase4_app_rewrite/plan_app_first_order.md`（本契约 §27 + 状态表 M04/S06c 行 + 交付说明）
- `agent/api_architecture_topology.md`（§6 V21 推进点 +1 记、新模块登记、§10 更新日志）
- `agent/topology/driver.md` 或 `agent/topology/app.md`（Middleware/Service 类图——topo-updater 收工判定该文件归属）

forbidden_files（仅列关键，其余全仓库不动）：
- `hc-team/app/service/chassis/**`（`chassis.h` 仅 include 不改；速度环/无目标限幅空缺不在本任务补）
- `hc-team/middleware/odometry/**`、`hc-team/middleware/pid/**`、`hc-team/middleware/speed_plan/**`（仅调用/参照，脉冲→mm、限幅、基速调制所有者不动）
- `hc-team/driver/**`（motion 对 encoder/imu 的既有只读快照调用不新增）
- `hc-team/app/service/route/**`（motion 上游调用者，本任务不接线；`route.c` 零触碰）
- 本地生成物 `Debug/hc-team/middleware/move_profile/subdir_*.mk`（须本地补以让固件增量构建生效，但**不入库**，§3 line 104；非 allowed committed 集）

### 27.2 公共接口（最小面）

```c
/* move_profile.h —— 距离参数化梯形速度剖面（Middleware 纯函数，无状态） */
typedef struct {
    float cruise_mps;   /* 匀速段速度上限 v_cruise，>0 */
    float start_mps;    /* 加速段起始速度（脱静摩擦起步速），0<=start<=cruise */
    float accel_mps2;   /* 加速度 a_acc (m/s^2)，>0 */
    float decel_mps2;   /* 减速度 a_dec (m/s^2)，>0 */
} MoveProfile_Config_T;

/* 返回本拍前馈基速(m/s)：v = clamp(min(cruise,
 *   sqrt(start^2 + 2*accel*s_m), sqrt(2*decel*rem_m)), 0, cruise)，
 * 其中 s_m=dist_done_mm/1000、rem_m=(target_mm-dist_done_mm)/1000（mm→m 仅量纲对齐，
 * 非第二个脉冲→距离换算所有者）。cfg==NULL / target_mm<=0 / dist_done>=target → 0（caller 负责停）。 */
float MoveProfile_Speed(const MoveProfile_Config_T *cfg,
                        float dist_done_mm, float target_mm);
```

```c
/* motion.h 增量 */
typedef enum { MOTION_IDLE=0, MOTION_STRAIGHT, MOTION_TURN, MOTION_ARC, MOTION_DONE,
               MOTION_PROFILED_STRAIGHT /* 追加于末尾，既有值不重排 */ } Motion_State;
/* Motion_Config_T 追加 4 字段（扁平，头不暴露 MoveProfile_Config_T 类型，同 odometry/pid cfg 扁平化先例）：*/
float profile_cruise_mps;   /* 定长直行剖面匀速上限，>0 */
float profile_start_mps;    /* 剖面起步速，0<=start<=cruise */
float profile_accel_mps2;   /* 剖面加速度 m/s^2，>0 */
float profile_decel_mps2;   /* 剖面减速度 m/s^2，>0 */
/* 新原语：起步平滑加速、末段减速停准；heading_hold=true 按 IMU 航向纠偏走直。distance_mm<=0 → false。*/
bool Motion_StartProfiledStraight(float distance_mm, bool heading_hold);
```

### 27.3 数据链与单一所有者声明（§8.2，本任务核心）

- 数据链：`Odometry_GetPose → s_pose` → `dist = sqrt(dx²+dy²)`（起点欧氏位移，**所有者仍 motion.c**，
  与既有 STRAIGHT 同一算法，非第二距离源）→ `MoveProfile_Speed(dist, target)`[前馈基速]
  → `base ∓ 航向PID corr` → `Chassis_SetTargetMps` → `Chassis_Update`。
- **纵向剖面（距离→前馈速度）唯一所有者 = `move_profile`**（全新变换，与 speed_plan「横向误差→巡航基速斜坡」输入域不同，非复刻/非竞争所有者）。
- 脉冲→mm 唯一 `Odometry_Config_T.mm_per_pulse`；move_profile 内 mm→m 仅量纲对齐，**不新增第三个距离换算所有者**（V22 不动）。
- 差速限幅/换向过零/命令超时/slew/刹车真值表 = `motor.c` 经 chassis；航向保持差速限幅 = `s_hold_pid` cfg（`hold_diff_limit_mps`）——**本任务零复做**。
- 前馈基速自限于 [0, cruise_mps]，天然回避 `Chassis_SetTargetMps` 无目标限幅的已知空缺（不依赖 chassis 兜底）。
- **无纵向 PID**（用户裁定）：剖面即位置闭环；不新增第二个从距离信号算速度的所有者。

### 27.4 preserved_behavior

- `MOTION_STRAIGHT`（`Motion_StartStraight` + `motion_step_straight`，恒速 `straight_speed_mps`）、`MOTION_TURN`、`MOTION_ARC` 行为与既有公共面**一字不改**。
- IMU 排空唯一点仍 `Motion_Update` 内 `Imu_Update()`（V23 不新增）；里程计 total 差值一次性消费不变。
- 既有 6 个生命周期 API 签名与语义不变；telemetry `target/progress` 对新态语义 = 距离 mm（与 STRAIGHT 同）。
- **终段行为 nuance（arch-auditor 建议级，文档处置）**：过零换向安全所有者与恒速直行同款（motor.c 经 chassis），非新安全缺口；但剖面直行终段 `base` 随剩余距离衰减，若此时仍有航向偏差、`corr` 幅值超过已衰减的 `base`，两轮可呈差速微转、净前进趋缓，最后几 mm 迟迟不满足 `dist>=target`，DONE 依赖调用者超时兜底——与「近终点静摩擦失速交超时/标定」同类终段现象叠加，**非新失效模型**。故到位判据的**代码路径**同 `motion_step_straight`，但**终段行为不严格等价**（后续 Task 层勿假设二者终段一致）。若现场终段打转，属 `hold_diff_limit_mps` 相对剖面终段 base 偏大的调参项，不加纵向下限。

### 27.5 证据行（≤6，恰 1 条固件构建行）

| ID | 项 | 命令 | 期望 |
|---|---|---|---|
| E01 | 范围审计 | `git status` + `git diff --stat` 对照 §27.1 | 无 allowed_files 之外改动（本地 `Debug/.../move_profile/subdir_*.mk` 不入库；`.ccsproject` 会话前既存不纳入） |
| E02 | move_profile 依赖纯净 | Grep `hc-team/middleware/move_profile/move_profile.[ch]` 命中 `#include` | 仅 `<math.h>`/`<stddef.h>`——无 `driver/`、`app/`、其他 `middleware/` 头（Middleware 纯算法零跨层） |
| E03 | motion 跨层扫描 | Grep `hc-team/app/service/motion/motion.c` 新增 `#include` | 新增仅 `middleware/move_profile/move_profile.h`（Service→Middleware 允许）；无 DL HAL/`ti/` 直含 |
| E04 | 主机全套（回归+计数） | PowerShell：`rtk proxy make -C tests/host all` | 0 FAIL；total = 451 基线 + move_profile 10 + motion profiled 6 = **467 PASS**（分项验收锁定，偏差走 §27.7 修订） |
| E05 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 诊断；`move_profile.c`+`motion.c` 均重编（非增量空转）；`move_profile.o`（linkInfo `<input_file>`）+ `motion.o`（链接对象）经 `Debug/2026_Diansai_linkInfo.xml` 确证进链、`.out` 重链 `link_errors=0`。**新符号 `MoveProfile_Speed`/`Motion_StartProfiledStraight` 因零调用者（route/T01 未接线，§裁定2 预期态）经 `-ffunction-sections` DCE 不入映像，可达性待 T01 接线成立——不作 `.text` 可达断言**（E05 冻结时的「.text 可达」为过度声称，见 §27.7 修订1） |

### 27.6 Stop conditions

- 若发现须在 chassis 或 odometry 内加位置环/前向线距读点才能实现 → 停止报告（应闭在 motion 既有欧氏位移上）。
- 若剖面需要隐藏状态（斜坡史/积分）才正确 → 停止报告（与「纯函数·无纵向 PID」裁定冲突，须先改契约）。
- 若须新增第二个脉冲→距离换算、第二个 Chassis_Update 之外的驱动点、第二个 Imu_Update 排空点 → 停止报告（§8.2/V21/V23）。
- baseline drift：BUILD 起测 host ≠ 451 PASS → 停止，先改契约。

### 27.7 契约修订记录

- 冻结（b98cf18）：任务范围、接口、5 证据行按用户 2026-07-19 裁定（Q1 扩 motion / 口径①新原语 / 口径②新 Middleware / 纵向按距离剖面无 PID）确定；基线 451 PASS（§26 验收）锁定，BUILD 起复核漂移；计划测试计数 move_profile 10 + motion profiled 6 = +16。
- 修订1（64fe6d5，代码前）：BUILD 中发现 E05 原文「`MoveProfile_Speed`/`Motion_StartProfiledStraight` `.text` 可达」为过度声称——新原语零调用者是 §裁定2 明示的预期态，`-ffunction-sections` 下新函数为死代码被 GC，不入映像；可达性须待 T01 接线后成立。E05 postcondition 改为断言两 .o 进链接输入 + linkInfo `<input_file>` + `.out` 重链 `link_errors=0`，并显式记录新符号因零调用者 DCE、可达性递延。计数与其余行不变。
- 现场调参手册：`docs/底盘调参/定长梯形剖面直行_位置环调参手册.md`（参数清单/标定顺序/公式/对症表/终段提示；MS02 起已接线板载操作）。底盘调参三层手册（速度环手填/循迹外环/位置环）+ 总览收拢于 `docs/底盘调参/`（含 `README.md` 级联总览与上板顺序）。
- 验收（代码 536883f）：5 行全过——E01 仅 8 个 allowed 文件在范围（`.ccsproject` 会话前既存未纳入、`Debug/**/*.mk`+`.o` gitignore 本地生成物未纳入）、零触碰 chassis/odometry/pid/speed_plan/driver/route / E02 move_profile.[ch] 仅 `<math.h>`/`<stddef.h>`（零跨层）/ E03 motion.c 唯一新增 include=`middleware/move_profile/move_profile.h`（Service→Middleware）无 DL HAL / E04 host 467 PASS 0 FAIL＝451 基线+move_profile 10+motion profiled 6 / E05 exit 0、0 诊断、move_profile.c+motion.c 均重编、move_profile.o(`<input_file>` fl-45)+motion.o 经 linkInfo.xml 进链、`.out` 重链 link_errors=0、新符号零调用者 DCE 可达性递延 T01。arch-auditor 契约逐条成立、无阻断/无重要、1 建议级（base∓corr 终段行为 nuance）文档处置入 §27.4，明确非新安全缺口（过零换向所有者仍 motor.c）。topo-updater 同步见 §10。

## 28. MS02 契约（ProfiledStraight 运行条目 + DRIVE 参数组按钮调参持久化）——冻结

- **task_id**: MS02-drive_tune_wiring
- **goal**: 把定长梯形剖面直行封装上板，与循迹环同款闭环：(a) DEBUG 组新增运行条目
  `ProfiledStraight`，进页跑一段定长直行（默认 1000mm）、退页确定性停车；(b) 新一级参数组
  `DRIVE`（进入显示 PARAMS 界面）按钮调 motion 剖面 4 参数 + 测试距离，K3 `SAVE` 断电存片内 flash。
  用户 2026-07-20 裁定：测试距离按钮可调默认 1000mm、新开独立一级组 DRIVE；`heading_hold=false`
  （开环直行测纵向剖面）；schema_ver 1→2 向后不兼容（旧 13B blob 因长度不符被忽略→用默认，一次性）。

- **Architecture**：
  - Abstraction：
    - motion 新增运行时读写口 `Motion_SetProfileParams`/`Motion_GetProfileParams`——「已应用剖面参数」
      的即时读写（供按钮调参），与 `LineFollow_Set/GetGains` 同款 Model A。
    - `ProfiledStraight` 运行条目：选它跑 → 改 DRIVE 参数 SAVE → 再选它跑 → 看效果（不用改代码烧录）。
  - Hidden state：motion 的 `s_cfg.profile_*` 成为「已应用剖面参数」唯一存储（Motion_Init 不再是唯一写者，
    但仍是**同一所有者 motion**，只是多一条写路径，非新所有者）；param_tune 扩为持久化编排两组值 +
    自持测试距离 `s_dist_mm`（唯一 param_tune 持值项——测试设定量无 Service 所有者）；app_compose 新增
    `s_ms_cfg`/`s_drive_params[]`/`ProfiledStraight` 三钩子。
  - Owner layer：motion=Service、param_tune=Service、app_compose=App 装配根、menu=UI（零改）、param_store=Driver（零改）。
  - Allowed dependency direction：param_tune(Service)→motion(Service) 同层受控（同其已有→line_follow）；
    app_compose(装配根)→motion/param_tune(Service)；均 §4 允许。

### 28.1 allowed_files（无 glob）

allowed_files：
- `hc-team/app/service/motion/motion.h` + `.c`（+`Motion_SetProfileParams`/`Motion_GetProfileParams` 运行时读写口；写 `s_cfg.profile_*`）
- `hc-team/app/service/param_tune/param_tune.h` + `.c`（blob schema_ver 1→2、扩 payload 至 33B、+motion 剖面 4 参数 get/set/step（委派 Motion）+ 自持测试距离 Dist get/set、Init/Save 扩、默认常量）
- `hc-team/app/system/app_compose.c`（+include motion.h；+`s_ms_cfg`；+3 个 profiledstraight_* 钩子；`s_entries[]` 加 idx5；`s_debug_entries[]` 加 5；+`s_drive_params[]`；`s_groups[]` 加 DRIVE 组）
- `tests/host/test_motion.c`（+SetProfileParams/GetProfileParams 用例）
- `tests/host/test_param_tune.c`（+schema2 往返/默认/距离/剖面委派用例）
- `tests/host/Makefile`（test_param_tune 链接集 +motion 链：MOTION/MOVE_PROFILE/ODOMETRY/HEADING/IMU/board_uart×4/fake_uart_port）
- `Debug/makefile`（无新 .c，仅在 app_compose/motion/param_tune 已在链——**若无新 .o 则本文件零改**）
- `agent/phase4_app_rewrite/plan_app_first_order.md`（本契约 §28 + 状态表 + 交付）
- `agent/api_architecture_topology.md`（§6 V21/V28 补注、§7 覆盖、§10 日志）
- `agent/topology/app.md`（motion 激活边 + param_tune→motion 边 + DRIVE 组）

forbidden_files（关键）：
- `hc-team/middleware/**`（move_profile/odometry/pid 仅调用不改）
- `hc-team/driver/param_store/**`（单扇区单记录不改——扩 blob 在 param_tune payload 层，不碰 Driver）
- `hc-team/app/ui/menu/**`（Menu_Param_T/Menu_Group_T 机制不改，仅装配层填表）
- `hc-team/app/service/line_follow/**`、`hc-team/app/service/chassis/**`（仅调用）
- `hc-team/app/system/{main.c,sys_init.c,app_compose.h}`（装配序不变）

### 28.2 公共接口（最小面）

```c
/* motion.h 增量：剖面参数运行时读写（Model A，已应用值唯一属 motion 的 s_cfg.profile_*） */
void Motion_SetProfileParams(float cruise_mps, float start_mps,
                             float accel_mps2, float decel_mps2);   /* 即时生效，写 s_cfg.profile_* */
void Motion_GetProfileParams(float *cruise_mps, float *start_mps,
                             float *accel_mps2, float *decel_mps2); /* 指针均非空；读已应用值唯一出口 */

/* Motion_Config_T 增量（修订1/2，arch-auditor §8.1 双看门狗，所有者=motion）：
   profile_timeout_ticks：PROFILED_STRAIGHT 运行拍数上限（防「移动但永不达标」，如 mm_per_pulse 严重失标→物理过冲）；
   profile_stall_ticks（修订2，用户 TB6612 关切）：命令在动(base>0)但编码器无进展的连续拍数上限——
     快速切停（防堵转：起步速偏低卡住/编码器脱线/撞障时，增量式速度环积分顶满 PWM 灌堵转电流进 TB6612）。
   均超时→Chassis_Stop+DONE；0=禁用。stall 应设短（~0.8s）保护驱动，timeout 长（~15s）兜底。 */
uint32_t profile_timeout_ticks;
uint32_t profile_stall_ticks;

/* param_tune.h 增量：DRIVE 组 get/set（milli 口径）+ 测试距离（mm）+ 步长常量 */
#define TUNE_STEP_CRUISE_MILLI 10   /* 占位，现场再定 */
#define TUNE_STEP_START_MILLI  10
#define TUNE_STEP_ACCEL_MILLI  10
#define TUNE_STEP_DECEL_MILLI  10
#define TUNE_STEP_DIST_MM      50
int32_t ParamTune_GetCruise_milli(void);  void ParamTune_SetCruise_milli(int32_t v);  /* 委派 Motion_Get/SetProfileParams */
int32_t ParamTune_GetStart_milli(void);   void ParamTune_SetStart_milli(int32_t v);
int32_t ParamTune_GetAccel_milli(void);   void ParamTune_SetAccel_milli(int32_t v);
int32_t ParamTune_GetDecel_milli(void);   void ParamTune_SetDecel_milli(int32_t v);
int32_t ParamTune_GetDist_mm(void);       void ParamTune_SetDist_mm(int32_t v);       /* param_tune 自持 s_dist_mm（唯一持值项） */
/* ParamTune_Init/Save 扩为读写 schema_ver=2 的 33B blob（LF 增益 + 剖面 4 参数 + 距离） */
```

blob v2 布局（33B，小端）：[0]ver=2、[1..12]kp/ki/kd milli、[13..16]cruise、[17..20]start、[21..24]accel、[25..28]decel milli、[29..32]dist_mm。旧 v1(13B) 因 `ParamStore_Read(len=33)` 长度不符→false→全默认（一次性丢旧 LF 增益，dev 板可接受，重调 SAVE 即恢复）。

### 28.3 数据链与单一所有者（§8.2）

- 剖面参数链：按钮→menu_param→`ParamTune_Set*_milli`（milli↔float ×1000，唯一换算所有者）→`Motion_SetProfileParams`（写 `s_cfg.profile_*`，**已应用值唯一属 motion**）→profiled straight 读用；SAVE→`Motion_GetProfileParams` 读回序列化→`ParamStore_Save`。
- 测试距离链：按钮→`ParamTune_Set/GetDist_mm`（param_tune 自持 `s_dist_mm`，唯一 param_tune 持值项，测试设定量无 Service 家）→运行条目 on_enter 读 `ParamTune_GetDist_mm()` 传 `Motion_StartProfiledStraight(dist, false)`。
- 不复做：脉冲→mm/剖面数学归 move_profile/odometry；限幅/换向/超时/刹车归 motor.c；NV 完整性归 param_store。param_tune 只加 payload 字段，不碰 Driver 框定。
- V28 接线：`profiledstraight_enter` 序 = `Motion_Init(&s_ms_cfg)`(归零/重置) → `ParamTune_Init()`(重推持久剖面参数+距离) → `Motion_StartProfiledStraight(ParamTune_GetDist_mm(), false)`。
- V21：motion 由零调用者→经 scheduler 条目激活；单活动条目不变量下与 SpeedTune/LineFollow 互斥，**不新增第 5 个 Chassis_Update 推进点**（仍是 motion 模块级单推进）。
- V22：剖面 setter 只写 `s_cfg.profile_*`，不新增 mm_per_pulse/heading_sign 第二所有者、不复算 move_profile 内部。

### 28.4 preserved_behavior

- 既有 5 条 DEBUG 条目 + TUNE 组（LF 增益）+ 开机 ParamTune_Init 行为：LF 增益链保持；schema 升级后旧 blob 一次性失效走默认（LF 增益回 0 默认，需重调 SAVE）——已在 §28.2 声明，属预期。
- motion 既有 API（Init/StartStraight/StartProfiledStraight/Turn/Arc/Update/Stop/Get*）签名语义不变；`Motion_Init` 仍重置全 cfg，新 setter 是**额外**写路径（同一所有者）。
- menu/param_store/chassis/line_follow/move_profile/odometry 零改动；装配序不变。
- **§8.1 防跑飞看门狗（修订1）**：PROFILED_STRAIGHT 唯一停车判据 `dist>=target` 在编码器脱线（dist≈0）时不触发、
  base=start 非零会一直冲——arch-auditor 建议级发现，本轮首次上板可达。加运行拍数上限 `profiled_timeout_ticks`
  （超时→Chassis_Stop+DONE，所有者=motion，主机可测）。属**安全看门狗**（防跑飞），区别于 TURN 先例交调用者的
  **收敛完成超时**（调参策略）——后者仍归 T01 赛题层。仅 PROFILED_STRAIGHT 加（本轮唯一上板路径）；
  STRAIGHT/TURN/ARC 未接线，其完成超时保持交调用者（既有设计不动）。`mm_per_pulse` 占位须上板标定（否则过冲）。
- **§8.1 堵转看门狗（修订2，TB6612 保护）**：调出速度环增益后，起步速偏低致堵转→增量式 PID 积分顶满 PWM→堵转电流。
  加 `profile_stall_ticks`：命令在动但编码器无进展连续 N 拍→快速 Chassis_Stop+DONE（~0.8s，比 max-runtime 15s 快得多，
  专为保护驱动芯片）。与 profile_timeout_ticks 并存（stall 防堵转/脱线/撞障、timeout 防物理过冲）。主防线=Start 设够+标 mm_per_pulse。

### 28.5 证据行（≤6，恰 1 条固件构建行）

| ID | 项 | 命令 | 期望 |
|---|---|---|---|
| E01 | 范围审计 | `git status` + `git diff --stat` 对照 §28.1 | 无 allowed_files 之外改动（`.ccsproject`/Debug 本地生成物不纳入） |
| E02 | 跨层扫描 | Grep motion.c/param_tune.c/app_compose.c 新增 `#include` + `& .claude/hooks/arch-scan.ps1 -Mode check` | motion.c 无新增 include；param_tune.c +`motion.h`（Service→Service）；app_compose.c +`motion.h`（装配→Service）；arch-scan 空输出、无 DL HAL |
| E03 | 主机全套（回归+计数） | PowerShell：`rtk proxy make -C tests/host all` | 0 FAIL；total = 467 基线 + motion setter 用例 + param_tune schema2 用例（分项验收锁定） |
| E04 | 接线序 + V28 | Grep `app_compose.c` profiledstraight_enter 体 | 序为 `Motion_Init(&s_ms_cfg)` → `ParamTune_Init()` → `Motion_StartProfiledStraight(ParamTune_GetDist_mm(), false)`（Init 归零后重推持久值） |
| E05 | 持久化往返 + 向后 | test_param_tune 断言（含 schema2 存→读→应用、空存→默认、旧 13B→默认） | 往返值全恢复；旧/空记录退默认；距离与剖面参数经 Motion 委派正确读回 |
| E06 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 诊断；`app_compose.o`+`motion.o`+`move_profile.o` 经 linkInfo.xml 进链；**`Motion_StartProfiledStraight`/`MoveProfile_Speed` 现经运行条目可达（`.text` 在映像，零调用者状态解除）** |

### 28.6 Stop conditions

- 若发现单扇区 param_store 无法容两组值（33B > 48 上限，或需多记录）→ 停止报告（应 33B ≤ 48 成立）。
- 若剖面 setter 需绕过 motion 直接改 move_profile 内部 → 停止（§8.2）。
- 若须改 menu/param_store Driver 才能加组/存值 → 停止（应纯装配层填表 + payload 扩字段）。
- baseline drift：BUILD 起测 host ≠ 467 PASS → 停止，先改契约。

### 28.7 契约修订记录

- 冻结（4990b09）：范围/接口/6 证据行按用户 2026-07-20 裁定（距离按钮可调默认 1000mm / 新开 DRIVE 组 / heading_hold=false / schema 升 2 旧 blob 一次性失效）确定；基线 467 PASS（§27 验收）锁定，BUILD 起复核漂移。
- 主体验收（代码 7c891cb）：6 行全过——E01 8 文件在范围 / E02 param_tune+app_compose 仅 +motion.h（Service）、motion.c 无新增 include、arch-scan exit 0 / E03 host 475 PASS 0 FAIL＝467+motion 3+param_tune 5 / E04 profiledstraight_enter 序 Motion_Init(&s_ms_cfg)→ParamTune_Init()→StartProfiledStraight(GetDist_mm,false) / E05 test_param_tune schema2 往返+默认+旧 13B 忽略退默认 / E06 exit 0、0 诊断、app_compose.o+motion.o+move_profile.o+param_tune.o 进链、Motion_StartProfiledStraight/MoveProfile_Speed/Motion_SetProfileParams/ParamTune_GetDist_mm 现进 .map 可达（零调用者解除）。
- 修订1（代码后，arch-auditor 处置）：arch-auditor 6 声明 5 成立、1 建议级——PROFILED_STRAIGHT 缺 §8.1 反馈超时停止，编码器脱线 dist≈0→base=start 一直冲，本轮首次上板可达。裁定为**安全看门狗**（非 TURN 式收敛完成超时），归 motion：+`Motion_Config_T.profile_timeout_ticks`（0=禁用）+ motion_step_profiled_straight 拍数上限超时→Chassis_Stop+DONE；s_ms_cfg 设实际界（1500≈15s）；test_motion +看门狗用例。代码 f333333。
- 修订2（用户 TB6612 关切，代码后）：用户问「起步卡住会不会烧 TB6612」。核实：chassis 默认增益 0（`chassis.c:27`）、sys_init 从不设增益→未调速度环时 PWM=0 不堵转；**一旦调出速度环增益，起步速偏低致堵转 → 增量式 PID 积分顶满 out_limit=±1000（`chassis.c:29`）→ 堵转电流灌 TB6612**，而修订1 的 15s max-runtime 看门狗对驱动芯片太慢。加**无进展/堵转看门狗** `profile_stall_ticks`：命令在动(base>0)但编码器无进展连续 N 拍→快速 Chassis_Stop+DONE（~0.8s，所有者 motion）。与 max-runtime 并存捕获不同故障（stall=堵转/脱线/撞障；timeout=物理过冲）。s_ms_cfg profile_stall_ticks=80(0.8s)、profile_timeout_ticks=1500。test_motion +堵转停/移动不误触发/禁用不干扰用例。**主防线仍是把 Start 设到真实脱离静摩擦速以上（别堵转）+ 上板前标 mm_per_pulse**；驱动侧硬件兜底=TB6612 热关断。安全看门狗添加是 §28 允许 motion 文件内修订，单独提交。

## 29. W7 契约（gray_check 现场标定助手：OLED 自绘面板 + menu 条目自绘 opt-in）——冻结

> 用户需求（2026-07-21 本会话）：不同光线条件下灰度阈值不同，想在 debug 组加「现场调灰度阈值」
> 条目（flash+key，仿循迹环 TUNE）。器件事实核查（原厂 PDF 40 页全文复核 + `docs/12路灰度传感器
> 配置指南.md` + gray.h）：**NCHD1 阈值 = 板上电位器给定的比较电压 V-com（手册 p.16/p.23），
> 全手册无软件阈值/一键标定/串口配置任何途径**；固件读到的位图已是比较器二值化结果，无阈值可调，
> 固件造「软件阈值」即第二所有者（违 §8.2，gray.h 刻意不做清单）。已按 AGENTS.md 停止并报告，
> 用户裁定（2026-07-21）：**选方案 A——固件不造阈值，只做标定校验助手**（方案 B 模拟改造被否，
> 与「硬件改动最小化/5V+串阻」定案冲突）。故本任务无 flash 项、无 TUNE 参数组——没有参数可存。

### 29.0 裁定与范围

- **GrayTest 条目升级为现场标定助手**：进条目后 OLED 实时显示 12 路深色位图 + 进条目以来的
  粘滞深色位图 + 逐路跳变计数——把手册 p.23 标定终判「上下微抖灯仍不变」从盯 12 个小灯变成
  一屏可量化读数；同时 OLED 显示的是 **MCU 实际读到的电平**（含接线/串阻链路），板上 LED 只反映
  模组侧，二者差异本身就是接线故障证据。VOFA tx×12 遥测原样保留（有 PC 时两面可用）。
- **menu 显示所有权契约修订 2（承 §23.0 第 2 条预留）**：启用「条目自绘整屏 opt-in flag」——
  新 API `Menu_SetEntrySelfDraw(entry_index)`（装配层在 Menu_Setup 后调用登记）。被标记条目
  RUN_ACTIVE 期 menu **零绘制**（不画 RUNNING、不清行）；条目服务负责首绘覆盖全部 4 行（盖掉
  残留列表内容）。单写者不变量保持：任意时刻 OLED 写者唯一（menu，或唯一活动的 self-draw 条目
  服务——单活动条目不变量 + 同拍序「Menu_Tick 先行、on_step 在后」结构性排除交叠）。
  未标记条目行为与 T1 逐字节一致（RUNNING 横幅照旧）。Menu_Setup 复位标记集。
- **gray_check 新增 hmi 依赖 = Service→Service 同层受控**（先例：line_follow→chassis）。
  只用显示面 **`Hmi_PrintLine`**；**不碰 `Hmi_PollInput`**（语义输入唯一消费者仍是 menu，
  BACK 退出路径不变、不新增按键路由）。统计清零手势 = 重进条目（BACK→ENTER 即 Start 归零）。
- **采样单一所有者不变**：统计与 tx 镜像共用既有 10ms 门控内的**同一次** `Gray_ReadDarkBitmap()`
  ——不加第二读点、不加去抖/滤波/阈值（黑白判定唯一所有者=硬件电位器）。跳变统计需 prev 播种：
  首拍只记不比（否则进条目时压线路会被误计 12 路跳变）——这是统计正确性所需，非 W4「无播种拍」
  裁定的回退（那条针对门控基准，保持 base=0 首拍即发/即绘）。
- **无新公共 getter**：sticky/toggle/绘制缓存/门控基准全私有于 gray_check（公共面签名零变化）。
- **OLED 面板（100ms 自门控 + 行差分重绘，避免冗余 I2C；`Hmi_PrintLine` 失败则该行缓存不更新、
  下个周期重试）**，4 行 ASCII（16 列）：
  - row0 `L:············`（12 格：`#`=深色/1、`.`=浅色/0；左=bit0=G1——仅通道序，不声明车上左右）
  - row1 `S:············` 粘滞深色（进条目以来 OR 累积）
  - row2 `T:············` 逐路跳变计数（`.`=0、`1`..`9`、`*`=≥10；计数器饱和不回绕）
  - row3 `X:%03X N:%02u` 位图十六进制（bit0=LSB，位序实测直读）+ 当前深色路数

### 29.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/gray_check/gray_check.h` / `.c` | 修改（头注释扩标定助手抽象；.c 增统计+面板渲染；签名不变） |
| `hc-team/app/ui/menu/menu.h` / `menu.c` | 修改（+`Menu_SetEntrySelfDraw`；`render_run_active` 对标记条目零绘制；显示所有权契约注释修订 2） |
| `hc-team/app/system/app_compose.c` | 修改（Menu_Setup 后标记 GrayTest idx3 self-draw + 注释同步） |
| `tests/host/test_gray_check.c` | 修改（+面板用例；既有 5 用例不动） |
| `tests/host/test_menu.c` | 修改（+self-draw 零绘制用例；既有 RUNNING 用例不动） |
| `tests/host/fake_hmi.c` | 新建（`Hmi_PrintLine` 捕获：行文本 + 逐行绘制计数 + 可设 ready；仅 test_gray_check 链接） |
| `tests/host/Makefile` | 修改（test_gray_check 目标 + fake_hmi.c） |
| `docs/12路灰度传感器配置指南.md` | 修改（追加 §9 现场标定流程 SOP：初始化全量标定 + 光线变化快速复检，含面板图例） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写（本节完成记录） |

- **forbidden_files**：`hc-team/app/service/hmi/**`（只调不改）、`hc-team/app/service/**` 其余、
  `hc-team/app/{scheduler,tasks}/**`、`hc-team/app/ui/menu/menu_param.{c,h}`、`hc-team/app/ui/oled/**`、
  `hc-team/driver/**`、`hc-team/middleware/**`、`board.syscfg`、`Debug/makefile`（无新 .o：
  gray_check.o/menu.o/app_compose.o 均已登记）、tests/host 其余 `test_*.c`/`fake_*.c`。

### 29.2 公共接口（最小面）+ 单一所有者

```c
/* gray_check：签名零变化（Start/Update/Stop），行为增量全在 .c 内私有。 */
/* menu 新增： */
void Menu_SetEntrySelfDraw(uint8_t entry_index); /* 标记 scheduler 条目 RUN_ACTIVE 期自绘整屏；
                                                    menu 对其零绘制。index≥32 忽略（mask 位宽）。
                                                    Menu_Setup 复位全部标记。 */
```

- **单一所有者声明**：黑白判定=硬件电位器（唯一阈值所有者，固件零阈值）；12 路原子读=gray driver；
  sticky/toggle 统计与面板格式化=gray_check 唯一拥有；OLED 行写语义/就绪门控=hmi；
  RUN_ACTIVE 显示权=menu，或经 opt-in 让渡给唯一活动的 self-draw 条目。
- **前置条件**：装配层已完成 `vofa_init()` 与 `Hmi_Init()`+底层 OLED/Key 初始化（World-2 既有序）。

### 29.3 preserved_behavior

- gray_check VOFA 帧内容/节奏/注册序逐字节不变（既有 5 用例原样过）；
- 未标记条目的 menu 全部界面/转移/RUNNING 横幅与 T1 一致；SpeedTune/EncoderTest/MotorDir/
  LineFollow/ProfiledStraight 五条目行为不变；
- 空转态仍只泵 Menu_Tick；scheduler/hmi/driver/middleware 零源码改动；参数组（TUNE/DRIVE）不变。

### 29.4 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/tasks/\|app/scheduler/\|app/ui/\|app/system/\|middleware/\|ti_msp_dl_config\|ti/driverlib`（path=`hc-team/app/service/gray_check`，`#include` 行） | 上层+middleware+DL HAL 前缀 0 命中；另核 driver/ 命中仅 `gray`+`uart_vofa`、`app/service/` 命中仅自身头 + `hmi/hmi.h`（Service→Service 同层受控，本契约白名单） |
| E02 | 装配层闸门 | PowerShell：`& .claude/hooks/arch-scan.ps1 -Mode check` | 空输出（menu/app_compose 新增依赖均层内合法，无 DL HAL 泄漏） |
| E03 | 范围审计 | `git status` + `git diff --stat` 对照 §29.1 | 无 allowed_files 之外改动（`.ccsproject` 会话前既存不计） |
| E04 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | **502 基线（2026-07-21 实测）+ ≥7 新用例，0 FAIL**。必含：首绘 4 行全绘且 L/S/T/X 内容正确；粘滞累积（深色消失后 S 保持）；跳变计数（n 次翻转→digit n、≥10→`*`、首拍不误计）；行差分（内容不变的行不重绘）；100ms 面板门控（位图变了但未到期不重绘）；重进条目统计清零+缓存失效全重绘；menu：标记条目 RUN_ACTIVE 零绘制（I2C 传输数不增）、未标记条目 RUNNING 照旧 |
| E05 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、`gray_check.o`+`menu.o`+`app_compose.o` 重编经 linkInfo.xml 进链；`Menu_SetEntrySelfDraw`/`Hmi_PrintLine`（自 gray_check）可达 |

### 29.5 Stop conditions

- 若面板需要 hmi 新接口（如按格绘制/局部刷新）→ 停止（应 `Hmi_PrintLine` 整行覆写够用）；
- 若 self-draw 需要 menu 向条目转发按键 → 停止（清零=重进条目，不扩输入面）；
- 若统计需要第二次 `Gray_ReadDarkBitmap()` 或任何去抖/滤波 → 停止（§8.2 单一所有者）；
- baseline drift：BUILD 起测 host ≠ 502 PASS → 停止，先改契约。

### 29.6 契约修订记录

- 冻结（2d9ff76）：范围/接口/5 证据行按用户 2026-07-21 裁定（方案 A：标定助手，无 flash/无软件阈值）
  确定；基线 502 PASS 0 FAIL（2026-07-21 实测，36 套件全绿）锁定。

### 29.7 完成记录（代码本提交）

- TDD 红→绿：test_menu self-draw 用例在旧 menu 链接期红（`Menu_SetEntrySelfDraw` 缺符号）；
  test_gray_check 6 面板用例在旧 gray_check 断言红（行文本全空）。实现后两目标全绿、零警告。
- 施工：menu `s_self_draw_mask`（Setup 复位、`Menu_SetEntrySelfDraw` 登记、`render_run_active`
  对标记条目零绘制）；gray_check 统计（sticky OR/逐路跳变饱和计数/prev 播种拍）+ 100ms 面板门控
  + 行差分缓存（PrintLine 失败不更新缓存下周期重试）+ Start 清零/缓存失效全重绘；app_compose
  标记 GrayTest idx3；新建 fake_hmi.c（只顶显示面 `Hmi_PrintLine`——缺 PollInput 符号即链接期
  证明消费面未扩大）；docs 配置指南 +§9 现场标定 SOP。
- E01 依赖纯净：gray_check include 面 = 自身头 + `hmi/hmi.h`（同层受控白名单）+ driver `gray`/
  `uart_vofa` + libc；上层/middleware/DL HAL 0 命中。E02 arch-scan check 空输出。E03 范围仅
  §29.1（`.ccsproject`/`test_emm42.exe` 会话前既存不计）。E04 主机 **509 PASS 0 FAIL**
  （502 基线 + 6 gray 面板用例 + 1 menu self-draw 用例），36 套件全绿。E05 固件 exit 0、
  0 diagnostics、`gray_check.o`+`menu.o`+`app_compose.o` 重编，linkInfo.xml 时间戳=构建时刻，
  `Menu_SetEntrySelfDraw`/`GrayCheck_Update`/`Hmi_PrintLine` 进链 6 符号命中。
- arch-auditor（c97c94d 后）：六核查点全过、无阻断/无重要；3 建议级全部采纳修复——
  ① app_compose 硬编码 3u → `APP_ENTRY_IDX_GRAYTEST` 命名索引（防条目插入静默漂移）；
  ② menu.c 位宽字面量 32 两处 → `MENU_SELF_DRAW_MASK_BITS` 共用常量；
  ③ PrintLine 失败重试路径补用例 `test_panel_retry_after_not_ready`（激活 FakeHmi_SetReady
  死代码，§8.3 条件③闭合）。修复后主机 **510 PASS 0 FAIL**、固件 exit 0 / 0 diagnostics 复跑通过。

## 30. W8 契约（GimbalTune 云台位置环静态调参条目——VOFA 命令模式）——冻结

- **task_id**: W8-gimbaltune
- **goal**: DEBUG 组新增第 7 运行条目 `GimbalTune`：云台（二维平台）位置环 PD 静态调参链路，
  照 SpeedTune（S03/W1）模板走 **VOFA 命令模式**——进页零出力、上位机命令改 kp/kd/死区/步长、
  JustFloat 波形看误差/增量/绝对目标/状态、退页确定性安全停。补齐拓扑 V26 附注与记忆登记的
  两个能力缺口：`vision_aim` 无运行时 setter、无云台 VOFA profile。
- **通道裁定（为何走 VOFA 而非 TUNE 参数组）**：位置环调参的验收对象是**波形形态**
  （阶跃收敛速度/超调/振荡/死区抖动），OLED 按钮无波形不可用；TUNE/param_tune 面向比赛日
  现场快调「已定型参数」并持久化。静态调参＝板凳期定型，**终值回填 `app_compose` `s_gt_cfg`
  编译期默认**，不入 flash blob——不新增 param_tune 字段，无双所有者。与「动态调参走 TUNE 组」
  裁定不冲突：分工是 板凳定型=VOFA（要波形）、赛日微调=TUNE（要按钮+掉电保存），云台 PD
  今后若需赛日微调再以契约修订入 TUNE（届时唯一写者从 VOFA 切换为 param_tune，不并存）。
- **接口辩护（能力解释）**：
  - `Gimbal_SetAimTuning`——「云台能在运行时更新瞄准 PD 调参子集（kp/kd/deadband/max_step，
    逐轴）」；调参需求已真实存在，非预支。center/sign/travel_limit **不在其内**（几何/极性/行程
    是装配事实，运行中改=事故面）。
  - `Gimbal_ReselectTopic`——「云台能在安全停后重发最近一次选题重新握手」；调参会话中坐标
    超时→STOPPED 后不丢 VOFA 会话恢复（否则须退页重进，cmd 组被安全复位、调参值全丢）。
  - `Gimbal_Telemetry_T` 扩 `last_error_px[2]`/`last_delta_pulse[2]`——「云台能报告最近一拍
    瞄准误差与输出增量」；调参波形两条主通道；gimbal 已算值（`res.error_px`/`res.delta_pulse`）
    的只读缓存，**零第二次计算**（同 axis_active 先例）。
- **安全机制核心事实（冻结依据，源码 vision_aim.c:94-102 实读）**：floor-1 语义（越死区最小走
  1 脉冲）⇒ **kp=0 不是零出力**——|error|>deadband 时 raw=0 也 floor 到 ±1 脉冲/帧恒向爬行。
  进页安全初值靠 **DB=10000px**（整幅图像在死区内 → delta 恒 0），零增益只是辅助。
  cmd 清洗（§7 外部输入边界，唯一清洗点=tuning_gimbal Apply）：kp/kd/DB 负值→0、MS<1→1（对齐
  vision_aim 前置条件 max_step>=1）、MS float→int32 截断。
- **数据链（§8.2 逐值确认）**：既有链全不动——像素坐标→(gimbal seq 时效)→`VisionAim_Map`
  （PD 数学/死区/floor/步长/极性/轴程唯一所有者）→delta→`cur_pulse` 唯一累加点→stepbus 0xAA。
  新增链：`VOFA cmd → TuningGimbal_Apply（清洗唯一点）→ Gimbal_SetAimTuning（拷入 s_cfg.aim
  四调参字段）→ VisionAim_Init（应用唯一点）`。tx 组：增益/DB/MS=cmd 单向回显，
  err/delta/cur/state=`Gimbal_GetTelemetry` 快照单向副本；无反向路径。
- **motor-safety（§8.1，逐项）**：进页 DB=10000 确定性零出力；退页 `Tuning_ExitProfile` →
  `Gimbal_Stop`（STOPPED 停止下发，步进保持使能力矩=云台安全态）；坐标/握手超时既有 STOPPED
  兜底不动；MS floor 1 + `travel_limit_pulse` 轴程软限位仍唯一在 vision_aim；emm42 RPM≤100
  不复夹；**sign 极性不进 VOFA cmd**（防运行中误反转制造正反馈跑飞）——极性首验 SOP（低 kp、
  小 MS、看 err 收敛方向，发散立即 BACK 退页，错则改 `s_gt_cfg.aim.sign`）随 docs 交付。
- **cmd 组（7）**：`XP`/`XD`/`YP`/`YD`（逐轴 kp/kd）、`DB`（双轴共享死区 px）、`MS`（双轴共享
  步长脉冲）、`GO`（≥0.5 边沿一次性消费 → `Gimbal_ReselectTopic`，应用后本模块清 0，属 cmd
  所有者内部消费非反向写）。安全初值：增益全 0、DB=10000、MS=1、GO=0。逐轴死区/步长的
  非对称终值可直接改 `s_gt_cfg` 编译期字段（API 保持逐轴数组，cmd 层共享是会话便利）。
- **tx 组（13，通道序）**：[0]err_x [1]err_y [2]delta_x [3]delta_y [4]cur_x [5]cur_y
  [6]state [7]XP [8]XD [9]YP [10]YD [11]DB [12]MS。
- **生命周期（app_compose，顺序约束是契约的一部分）**：
  enter = `Gimbal_Init(&s_gt_cfg)` → `Tuning_EnterProfile(TUNING_PROFILE_GIMBAL_AIM)`
  （vofa 清表/排空积压 RX → 安全 cmd → 注册 → `Gimbal_Stop` + 立即应用安全 cmd）→
  `Gimbal_SelectTopic(GIMBALTUNE_TOPIC_MAIN/SUB 装配层占位常量)`；**SelectTopic 必须在
  EnterProfile 之后**（Enter 内 Gimbal_Stop 会杀握手）。step = `Tuning_Update()`（10ms 门控
  vofa_run→Apply→RefreshTx，末尾恒 `Gimbal_Update` 自门控泵）。exit = `Tuning_ExitProfile()`。
  `s_gt_cfg` 全字段 UNCALIBRATED 占位注释（center 按视觉分辨率、sign 待极性首验、travel_limit
  按机械行程、step_speed_rpm 保守 30、coord_timeout 500ms、ack_timeout 1000ms）。

### 30.1 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/app/service/gimbal/gimbal.h` / `.c` | 修改（SetAimTuning/ReselectTopic/遥测扩两字段——S05c §21.3 契约修订 1） |
| `hc-team/app/service/tuning/tuning.h` / `.c` | 修改（enum 增 `TUNING_PROFILE_GIMBAL_AIM` + profile 分派——S03 §9 契约修订 3） |
| `hc-team/app/service/tuning/tuning_gimbal.h` / `.c` | 新建（服务内私有子模块，五函数模式同 tuning_chassis） |
| `hc-team/app/system/app_compose.c` | 修改（条目 idx6 GimbalTune + `s_gt_cfg` + 占位题号常量 + 头注释） |
| `tests/host/test_gimbal.c` | 追加用例（setter 生效/清洗/ReselectTopic/遥测新字段） |
| `tests/host/test_tuning_gimbal.c` | 新建 |
| `tests/host/Makefile` | 追加 test_tuning_gimbal 目标/run/clean + SRC 变量 |
| `.gitignore` | 追加 test_tuning_gimbal 产物 |
| `Debug/makefile` | 登记 `tuning_gimbal.o`（ORDERED_OBJS；tuning 目录 -include 已有） |
| `docs/云台位置环调参指南.md` | 新建（VOFA 配置 + 调参 SOP + 通道/命令表） |
| `agent/phase4_app_rewrite/plan_app_first_order.md` | 状态回写 |

forbidden_files：`hc-team/middleware/vision_aim/**`（PD 数学不动，V26）、`hc-team/driver/**`
（emm42/stepmotor_uart/uart_vision/uart_vofa/clock 只调用）、`hc-team/app/service/param_tune/**`
与 `hc-team/driver/param_store/**`（通道裁定明确不动）、`hc-team/app/service/gimbal/gimbal_stepbus.*`
（无需改）、`hc-team/app/tasks/**`（冻结存量）、`hc-team/app/{scheduler,ui}/**`、`board.syscfg`、
tests/host 既有其余 `test_*.c` 与全部 `fake_*.c`（fake_uart_port 已有 Vision RX 注入 + VOFA
RX/TX 钩子，够用）。

### 30.2 preserved_behavior

- 既有 6 条目（SpeedTune/EncoderTest/MotorDir/GrayTest/LineFollow/ProfiledStraight）行为不变；
  `APP_ENTRY_IDX_GRAYTEST=3` 不变（新条目追加在尾部 idx6）。
- CHASSIS_SPEED profile 行为逐位不变（tuning.c 分派重构不得改其 Enter/Update/Exit 时序）。
- gimbal 既有状态机/超时/累加点/首帧播种逐位不变；`Gimbal_SetAimTuning` 不触碰运行状态
  （不清 prev_error/不改 state——调参换挡不打断收敛环）。
- vision_aim 源码零改动。

### 30.3 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `#include` 行（path=`hc-team/app/service/tuning` + `hc-team/app/service/gimbal`） | tuning_gimbal.c 只见 `tuning_gimbal.h`/`gimbal.h`/`uart_vofa.h`；gimbal.c 无新增 include；两目录 `app/tasks/\|app/scheduler/\|app/ui/\|ti_msp_dl_config\|ti/driverlib\|param_tune\|param_store` 0 命中 |
| E02 | 装配层闸门 | PowerShell：`& .claude/hooks/arch-scan.ps1 -Mode check` | 空输出 |
| E03 | 范围审计 | `git status` + `git diff --stat` 对照 §30.1 | 无 allowed_files 之外改动（`.ccsproject`/`tests/host/test_emm42.exe` 会话前既存不计） |
| E04 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | **510 基线（W7 §29.7）+ ≥10 新用例，0 FAIL**。必含：Enter 后大误差坐标零出力（DB=10000：cur_pulse 恒 0、下发绝对目标恒 (0,0)——绝对帧幂等重发是既有 AIMING 行为，非出力）；负增益/负 DB 清洗为 0；MS<1 floor 1；setter 生效（kp 改变 → delta 按新值）；setter 不清 prev_error（换挡不断环）；ReselectTopic 未选过题→false、选过→重握手；GO 单次消费（一次 Apply 只触发一次）；Exit 后 STOPPED 无 TX；帧=13 通道；遥测 last_error_px/last_delta_pulse 死区拍 error 有值 delta=0 |
| E05 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、`tuning_gimbal.o` 新进链 + `gimbal.o`/`tuning.o`/`app_compose.o` 重编经 linkInfo.xml 确证；`Gimbal_SetAimTuning`/`Gimbal_ReselectTopic`/`TuningGimbal_Apply` 可达 |

### 30.4 Stop conditions

- 若调参需要第二处 PD 计算、误差重算或 vision_aim 修改 → 停止（V26 单一所有者）；
- 若需要 param_tune/flash 持久化 → 停止（通道裁定重议）；
- 若 GO 需要队列/重试/多次重发机制 → 停止（单次边沿够用，失败下拍再按）；
- 若 tuning.c 分派需要函数指针表/注册框架 → 停止（两个 profile，switch 够用，§8.3）；
- baseline drift：BUILD 起测 host ≠ 510 PASS → 停止，先改契约。

### 30.5 契约修订记录

- 冻结（2399390）：范围/接口/安全机制（DB=10000 进页零出力，floor-1 爬行事实）/5 证据行确定。
- 修订 1（0a8c463，施工前）：E04「零出力」断言更正——gimbal AIMING 拍对每帧新坐标**无条件重发
  双轴绝对目标帧**（幂等自愈，gimbal.c:246 实读），故零出力的可观察定义是「cur_pulse 恒 0、
  绝对目标恒 (0,0)」，不是「零步进 TX」。原措辞「零步进 TX」与既有代码事实冲突。
- 修订 2（本提交，arch-auditor 建议级采纳）：MS 清洗补上界——原条款只有「MS<1→1」，
  超大值（如 3e9）在主机侧 float→int32 越界转换属未定义行为（x86 产出 INT32_MIN → max_step
  变负违反 vision_aim 前置条件），板上 ARM 饱和到 INT32_MAX 则实质解除 §8.1 每拍步长封顶。
  新条款：**MS 清洗域 [1,10000]**（上界 10000 覆盖一切合理机构，恒在 int32 定义域），
  清洗仍唯一在 Apply。E04 清洗用例相应扩：MS 超大值 → 回显 10000。

### 30.6 完成记录（代码本提交）

- TDD 红→绿：test_gimbal 4 新用例在旧头编译期红（`Gimbal_AimTuning_T`/`last_delta_pulse`
  缺类型/字段，exit 2 实录）；实现后全绿零警告。
- 施工：gimbal（`Gimbal_SetAimTuning` 只换 aim 四调参字段经 VisionAim_Init 唯一应用点、
  不触碰 prev_error/seeded/state；`Gimbal_ReselectTopic` 依 `s_has_topic`+既有 pending 题号；
  遥测扩 `last_error_px/last_delta_pulse` 只读缓存）；tuning.c switch 二 profile 分派（无函数
  指针框架）；tuning_gimbal（cmd×7 XP/XD/YP/YD/DB/MS/GO、tx×13、安全初值 DB=10000/MS=1、
  清洗唯一点 Apply + `s_applied` 回显、GO 先清后触发单次消费）；app_compose GimbalTune idx6
  （Init→EnterProfile→SelectTopic 契约顺序、`s_gt_cfg` UNCALIBRATED 占位、占位题号 1/0）。
- E01 依赖纯净：tuning 目录 include 面=自身两头+chassis.h/gimbal.h（同层受控）+clock/uart_vofa
  （Service→Driver 合法）；tuning_gimbal.c 恰 tuning_gimbal.h/gimbal.h/uart_vofa.h 三条；
  gimbal.c include 面零新增；禁止前缀（tasks/scheduler/ui/DL HAL/param_tune/param_store）0 命中。
- E02 arch-scan check 空输出。E03 范围=§30.1 全集（`.ccsproject`/`tests/host/test_emm42.exe`
  会话前既存不计）。E04 主机 **520 PASS 0 FAIL**＝510 基线＋10 新用例（gimbal 4：setter 生效/
  不清 prev/Reselect 重握手/遥测死区拍；tuning_gimbal 6：Enter 零出力+13 通道/cmd 应用/清洗/
  GO 单次消费可推进 ARMING/Exit 静默/重进重置安全），44 套件全绿。E05 固件 exit 0、0 警告 0
  诊断、`tuning_gimbal.o` 进链接命令行+linkInfo `<input_file>`，`Gimbal_SetAimTuning`/
  `Gimbal_ReselectTopic`/`TuningGimbal_Apply` .text 落位，linkInfo 时间戳=构建时刻。
- 交付含 `docs/云台位置环调参指南.md`（通道/命令表 + 6 步 SOP：噪声→极性首验→MS→kp→kd→DB
  →回填 `s_gt_cfg`+异常速查）。本地生成物 `Debug/.../tuning/subdir_vars.mk` 已补六列表（不入库，
  P9.T1 口径）。硬件联调（真视觉/真步进）用户自理。
