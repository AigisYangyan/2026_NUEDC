# 2026_Diansai — 每轮必读的最小不变量

本文件只承载每一轮都必须在场的硬约束与接线指引。完整规范在根 `AGENTS.md`（唯一架构权威，
含 §1–§15 全文）；本文件与 AGENTS.md 冲突时以 AGENTS.md 为准，并须报告冲突以便修正本文件。

## 工程事实

- MSPM0G3519（LQFP-100），SDK 2.11.00.07，ticlang 4.0.4；由 `NUEDC`/G3507 移植，
  配方见 `agent/MIGRATION_G3507_TO_G3519.md`。
- 唯一硬件配置源：仓库根 `board.syscfg`。编码器 = TIMG8/TIMG9 硬件 QEI（GROUP1 IRQ 仅按键）；
  步进总线 = UART7；灰度 = 12 路（NCHD1）；MPU6050/I2C_IMU 已移除。
- 固件构建：`rtk make -C Debug all`。主机测试：`rtk make -C tests/host all`。
  **make 必须经 PowerShell 工具执行**——Bash 里调 `make.bat` 会静默空转并假报 exit 0（已证实的取证陷阱）。
- `rg` 不在 PowerShell PATH；源码扫描一律用 Grep 工具。

## 依赖矩阵（AGENTS.md §4，无例外）

| 调用方 | DL HAL | Driver | Middleware | Service | Task/Scheduler/UI |
|---|---|---|---|---|---|
| Driver | 允许 | 同层受控 | **禁止** | **禁止** | **禁止** |
| Middleware | **禁止** | **禁止** | 同层受控 | **禁止** | **禁止** |
| App Service | **禁止** | 允许 | 允许 | 同层受控 | **禁止** |
| App Task/Scheduler/UI | **禁止** | **禁止** | **禁止** | 允许 | 同层受控 |

无例外判定：Middleware 包含 Driver/DL HAL＝**失败**；Driver 包含或调用 App＝**失败**
（`extern`/弱符号/函数指针间接引用同样禁止）；Task 直接包含 Driver/Middleware/DL HAL＝**失败**。
禁止 Driver→上层注册回调（§5）；ISR 只做中断读清、最小搬运、计数、置位；
禁止跨模块读写另一模块全局变量；同一数据变换只允许一个所有者。

## 当前裁定（AGENTS.md §15，2026-07-17，未解除）

App 上层（Service/Task/Scheduler/UI）将整体重置；当前只做 Driver 层下层接口。

1. **Driver 零调用者是预期状态，不是缺陷**——不得据此推迟/缩减 Driver 工作，也不得记为遗留问题。
2. 不得为「让它有人调」而在 App Task 直调 Driver（那是 V07/V03 违规模式的复制）。
3. 不得为假想上层预建接口：每个公共接口必须能用「器件能做什么」解释，否则删掉。
4. Driver 验收 = 主机测试 + 构建 + 依赖扫描；硬件验证由用户用厂商上位机自理。
5. 本裁定不放宽任何架构边界，只改变「何时接上层」。
6. 现有 `app/tasks/**` 是待重置的存量债（V07/V03），不得作为新代码参照。

解除条件：用户明确宣布上层重置开始。

## 停止并报告（AGENTS.md §12）

出现以下任一情况，停止实现，报告冲突点 + 违反的层级规则 + 至少一个保持单向依赖的替代方案：
业务逻辑要直接操作寄存器/DL HAL；Middleware 要直接读 Driver 或控硬件；Driver 要调上层或上层回调；
模块要改另一模块内部状态/全局变量；公共头要暴露寄存器/私有缓冲/状态机布局；
需求只能靠循环依赖、无界等待或绕过校验完成；性能改动无基线与测量方法；
电机代码缺少可确认的频率/方向/限幅/换向/安全停止条件；数据进入本模块前的处理阶段无法确认。

## 每轮工作流（agent 框架接线）

拓扑按层分文件（章节编号不重排）：`agent/api_architecture_topology.md` = 索引与唯一入口
（§1、§5 数据流、§6 风险登记、§7 覆盖清单、§8/§9 检查、§10 更新日志）；
`agent/topology/driver.md` = §2 Driver 类图；`agent/topology/app.md` = §3/§4 App 类图与调度图。

- **编码前**：用 `topo-navigator` 子 agent 取目标模块的拓扑切片（§14.1）。
  不要把拓扑文件整读进主上下文——那是子 agent 存在的理由。
- **编码中**：涉及电机/舵机/步进/TB6612/H 桥/PWM → 先加载 skill `motor-safety`（§8.1 全文）；
  改采样/传感器/编码器/PID/滤波/限幅/单位换算链 → 先加载 skill `data-chain`（§8.2 全文）；
  有界任务的决策→施工→验收全流程遵循 skill `embedded-closed-loop`（契约先于代码提交冻结）。
- **编码后**：`arch-auditor` 子 agent 做分层与链路评审（§9.8）；
  `topo-updater` 子 agent 同步拓扑并在索引 §10 追加日志（§14.3/4——即使无变化也要记「已复核，无拓扑变化」）。
- **机械闸门**（hook，不依赖记忆）：`arch-guard.ps1`（PostToolUse）对新增跨层 `#include` 当轮报警；
  `topo-guard.ps1`（Stop）在「动了 `hc-team/**/*.c|h` 但没动拓扑」或有基线外违规时 exit 2 拦截收工。
  38 条存量违规冻结于 `.claude/hooks/arch-baseline.txt`（全部在 `app/**`），只报基线外新增。
- **提交**：Conventional Commits（`feat|fix|refactor|docs|test|chore|perf|ci`）。
  拓扑更新是完成定义的一部分（§14）；代码已改而拓扑未复核 = 任务未完成。

## 严格计划表

`agent/phase2_driver_rewrite/plan_driver_first_order.md` —— Driver 层逐项状态与阻塞源，
开工前必读，完工后必更。
