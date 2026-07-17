# P9 —— 12 路灰度 Driver 移植（D12，Driver 层最后一块缺口）

冻结时间：2026-07-17
协议：`.agents/skills/embedded-closed-loop`（DECIDE → BUILD → ACCEPT，单 agent 全占，git 充当第二方）

---

## 0. 用户裁定

2026-07-17 用户消息原文（节选）：

> 「这是我新买的 灰度模块 +datasheet+ 示例代码, 开战新的 driverplan, 将它移植 driver 层,
> 记住按照 AGENTMD 和流程来移植」

**灰度的暂缓裁定（2026-07-17「暂不编写，后续会修改」）由本条消息解除。**
`plan_driver_first_order.md` §2 的 D12 状态由 `GAP`+`HOLD` 改为本计划派工中。

同一条消息还要求：删除两份参考工程、巡查整理 Driver 层、出总汇报。
那三项**不在本 T1 范围内**，另立 T2（见 `plan_p9_driver_audit.md`）。

---

## 1. 器件事实（来自厂商手册，非推断）

器件：**武汉无名创新 NCHD1（迹）**，12 路线性阵列（NC-HD12 扩展板）。
来源：`hc-team/12路灰度传感器检测20240331（STM32F103C8T6）/灰度传感器用户使用手册（迹系列20230119).pdf`

| # | 事实 | 手册出处 |
|---|---|---|
| F1 | 出厂默认**数字输出**（DEF 处焊零欧电阻 R6，等效短接 OUT-IO）。模拟输出需拆 R6 改焊 | p.6 / p.19 / p.20 |
| F2 | **深色背景 → 输出高电平 1**（OUT 指示灯灭）；浅色背景 → 低电平 0（灯亮） | p.8 / p.17 |
| F3 | 输出经运放比较器产生，比较电压由板载电位器 R3 分压给定；ADC 端有 C2(100pF) 滤波 | p.15 / p.16 |
| F4 | **单片机对应 IO 必须初始化为输入模式**，否则 MCU 电平会反过来影响模块输出 | p.21 / p.32 |
| F5 | 检测「基本不占用处理器额外资源，能对赛道情况实现超高频率的检测」——纯电平读取，无时序、无握手 | p.11 |
| F6 | 5V 供电时**输出高电平约 4.0V**；手册明确警告不耐 5V 的 MCU 须串 300–1000Ω 或改用 3.3V 供电 | p.31 |
| F7 | 探测距离（灯罩底部离地）5–60mm，**推荐 10–30mm** | p.7 |
| F8 | 厂商位序约定：`bit1..bit16` = 阵列**最右端 → 最左端**，P1 = 最右 | p.34 |
| F9 | 10mm 线宽下，正常情况最多 2 路同时压线；不丢线时至少 1 路为高 | p.38 |

### 1.1 与本仓现状的**矛盾**（必须记录，不得静默选边）

- **C1（位序）**：F8 说 P1 = **最右**；而本仓 `board.syscfg:93` 注释写「12 路灰度,物理排列**从左到右** IN1 -> IN12」，
  `track_follow.h:33` 写「bit0=IN1(**最左**)」。二者不可能同时为真。
  **哪个物理位置接到 `PIN_IN1`（PB27）是硬件接线事实，固件无法自证。**
  → 本 Driver **不声明左右**，只声明「bit i = `PIN_IN(i+1)` 引脚当前为深色」。左右由用户实测（§6）。
  → 依 `AGENTS.md` §8.1 同类精神（禁止猜方向），此处禁止猜位序。

- **C2（供电电压）**：F6 与 MSPM0G3519 的 IO 耐压未经核实。**这是移交用户的硬件确认项（§6），不是固件可解的问题。**

---

## 2. 现状证据（施工前实测）

- **`hc-team/driver/gray/` 不存在** —— Driver 层无灰度模块（`git ls-files hc-team/driver/` 命中 0）。
- 12 路采样现位于 **`hc-team/app/tasks/track_follow/track_follow.c`**：
  - `track_follow.c:26` 直接 `#include "ti_msp_dl_config.h"`
  - `track_follow.c:59-65` **for 循环里逐路调 `DL_GPIO_readPins`，共 12 次独立读取**
- 这是 **V03** 存量违规，也是 V03 至今 `partially closed` 而非 `closed` 的唯一残留点。
- 12 路引脚（`Debug/ti_msp_dl_config.h:303-340`）全部在 **GPIOB**：
  IN1=PB27 IN2=PB12 IN3=PB13 IN4=PB8 IN5=PB20 IN6=PB26 IN7=PB17 IN8=PB19 IN9=PB21 IN10=PB14 IN11=PB0 IN12=PB24
- `board.syscfg:94-99` 的注释已写明：12 路同端口是**特意用 PB8 换来的性质**，为的是一次 `DL_GPIO_readPins` 读全 12 路。
  **而现有 App 代码根本没兑现这个性质**（12 次分读 = 路间时间偏斜）。本次移植的实质收益之一就是把它兑现。

---

## 3. 设计裁定

### 3.1 抽象是什么

> **器件能做什么：12 路各输出一个数字电平，深色 = 高。除此以外没有任何能力。**

依 `AGENTS.md` §15.3 判据（每个公共接口都必须能用「器件能做什么」解释），公共接口收敛为**一个函数**：

```c
#define GRAY_CHANNEL_COUNT 12u
uint16_t Gray_ReadDarkBitmap(void);
```

### 3.2 明确**不做**的事，以及不做的理由

| 不做 | 理由 |
|---|---|
| `Gray_Init()` | 器件要求的唯一初始化动作是「IO 配成输入」（F4），**SysConfig 已经做了**。Driver 无内部状态可初始化。加空 Init 是为对称而对称，§8.3 禁止 |
| 去抖 / 滤波 | 器件自带 C2 滤波 + 比较器电位器裕度（F3），且电位器调节步骤本身就是在留迟滞裕度（p.23）。固件再滤一次 = §8.2 单一所有者冲突 |
| 反相 / 阈值 | 同上，单一所有者 |
| 「丢线」判断（全 0） | 那是**循迹算法**对位图的解读，不是器件事实。归 Middleware（§3.3） |
| 误差量化（`gray_status[]` / 权重表 / -11..+11 映射） | 示例工程的 `switch(gray_state.state)` 是循迹算法，**不属于 Driver**（`plan_driver_first_order.md` §3.3 已预置此裁定） |
| 停止线检测（示例的 `0x00F0` 计数） | 同上，赛道特征识别属 Middleware |
| 历史缓存（示例的 `gray_status_backup[2][20]`） | 同上 |
| `Gray_GetDiag()` | 器件无错误可计。校验/超时/溢出在纯电平读取里不存在（F5） |
| 模拟量 / ADC 接口 | 模组出厂为数字（F1），且本板 12 脚接的是普通 GPIO 不是 ADC 通道。为拆 R6 的假想场景预建接口 = §15.3 禁止 |
| 声明左右 | 见 C1。硬件事实未知，禁止猜 |

### 3.3 分层与可测性

采用 **motor 既有范式**（`motor.c` 逻辑 + `motor_hw.c` 真实 HAL + `fake_motor_hw.c` 主机假件）：

```
gray.h        公共能力接口（1 个函数 + 1 个常量）
gray_port.h   HAL 边界：gray_port_read() / gray_port_channel_mask()
gray.c        端口原始值 -> 通道位图 的散射逻辑（可主机测试，零 TI 依赖）
gray_hw.c     唯一允许碰 DL HAL 的文件；引脚一律用 syscfg 生成宏，禁止手抄引脚号
```

**关键性质：`Gray_ReadDarkBitmap()` 对 `gray_port_read()` 的调用恰好 1 次。**
这就是 `board.syscfg` 用 PB8 换来的 12 路原子采样，由主机测试钉死（E03 用例 6）。

依赖方向：`Gray_API --> GrayPort_API --> DL_HAL`。**本任务不新增任何上层调用者**（§15.2）。

---

## 4. 契约

## P9.T1 12 路灰度 Driver 移植

Status: **ACCEPTED**（2026-07-17；契约 `b421682`，代码 `b423593`；6 行全过）
Goal: `hc-team/driver/gray/` 提供 `Gray_ReadDarkBitmap()`，一次端口读取产出 12 位深色位图；TI HAL 仅存在于 `gray_hw.c`；主机测试覆盖散射映射与单次读取性质；固件构建绿。

Evidence:
- `hc-team/app/tasks/track_follow/track_follow.c:26` 直接 `#include "ti_msp_dl_config.h"`（V03 残留）
- `hc-team/app/tasks/track_follow/track_follow.c:59-65` 12 次分读 `DL_GPIO_readPins`，路间存在时间偏斜
- `git ls-files hc-team/driver/` 中 `gray` 命中 0 —— Driver 层无灰度模块

Architecture:
- Abstraction: 12 路数字灰度电平的一次原子读取；深色 = 1
- Hidden state: 无（器件为纯组合输入，Driver 无状态）。引脚掩码表为 `gray_hw.c` 私有
- Owner layer: Driver
- Allowed dependency direction: `Gray_API -> GrayPort_API -> DL_HAL`

Scope:
- allowed_files:
  - `hc-team/driver/gray/gray.h`（新建）
  - `hc-team/driver/gray/gray.c`（新建）
  - `hc-team/driver/gray/gray_port.h`（新建）
  - `hc-team/driver/gray/gray_hw.c`（新建）
  - `tests/host/fake_gray_port.c`（新建）
  - `tests/host/test_gray.c`（新建）
  - `tests/host/Makefile`
  - `Debug/makefile`
  - `.gitignore`
  - `agent/phase2_driver_rewrite/plan_p9_gray_driver.md`
  - `agent/api_architecture_topology.md`
  - `agent/phase2_driver_rewrite/plan_driver_first_order.md`
- forbidden_files:
  - `board.syscfg`
  - `hc-team/app/tasks/track_follow/track_follow.c`
  - `hc-team/app/tasks/track_follow/track_follow.h`
  - `hc-team/app/tasks/gray_test/gray_test.c`
  - `hc-team/app/tasks/task1/task1.c`
  - `hc-team/app/system/sys_init.c`
- preserved_behavior:
  - `track_follow.c` 现有行为**一字不改**。它随上层重置删除，不在本任务范围（§15.2：禁止在 App Task 里直接调 Driver 制造调用者）
  - 既有 101 项主机测试全部保持通过
  - `board.syscfg` 零改动 —— 12 路引脚配置已就位，本任务只消费不修改

Preconditions:
- 用户 2026-07-17 消息解除灰度暂缓裁定
- `board.syscfg:102-135` 已把 12 路配为 GPIOB 输入，且 `GPIO_LINE_SENSOR_PORT=(GPIOB)` 组级宏存在
- 主机基线 101 PASS / 0 FAIL（P8B 收官值）

Steps:
1. 先写 `tests/host/test_gray.c`，在 `gray.c` 不存在时必须编译失败（真实反证）。
2. 最小实现：`gray_port.h` / `gray.c` / `gray_hw.c` / `gray.h`。
3. 只重构本任务引入的代码。
4. 验证通过后更新拓扑与严格计划表。

Verification:

- **E01** command: `make.bat -C Debug clean all`（PowerShell 工具启动）
  expected_exit: `0`
  postcondition: 输出含 `gray.o` 与 `gray_hw.o` 的实际编译行，且 `.out` 实测重链接
  negative_check: `: (warning|error|remark)` 命中 0；不得出现 "up to date" 空转冒充构建

- **E02** command: `grep -nE 'ti_msp_dl_config|ti/driverlib|DL_' hc-team/driver/gray/gray.c hc-team/driver/gray/gray.h hc-team/driver/gray/gray_port.h`
  expected_exit: `1`（grep 无命中）
  postcondition: 命中 **0** —— TI HAL 被关在 `gray_hw.c` 里
  negative_check: 任一命中即 REJECT

- **E03** command: `make.bat -C tests/host clean all`（PowerShell 工具启动）
  expected_exit: `0`
  postcondition: `^\s*PASS:` 计数 **≥ 109**（101 基线 + ≥8 项新增 gray 用例），`^\s*FAIL:` 计数 **0**
  negative_check: 101 项基线不得有任何回归

- **E04** command: `grep -oE 'DL_GPIO_readPins|DL_GPIO_PIN_' hc-team/driver/gray/gray_hw.c | sort | uniq -c`
  expected_exit: `0`
  postcondition: `DL_GPIO_readPins` 恰好 **1** 次（12 路单次原子采样兑现）；`DL_GPIO_PIN_` 恰好 **0** 次（引脚一律经 syscfg 生成宏，无手抄引脚号）
  negative_check: `DL_GPIO_readPins` ≥2 即 REJECT（时间偏斜复发）；`DL_GPIO_PIN_` ≥1 即 REJECT（手抄引脚号会与 syscfg 脱钩）

- **E05** command: `git status --porcelain --untracked-files=all hc-team/app board.syscfg`
  expected_exit: `0`
  postcondition: 输出为**空** —— 未制造上层调用者（§15.2），未改配置源
  negative_check: 任何一行输出即 REJECT
  > 口径说明：必须带 `--untracked-files=all`。2026-07-17 事故根因正是 `--untracked-files=no` 排除了要查的对象（量具错误第四次）。

- **E06** command: `git status --porcelain --untracked-files=all`
  expected_exit: `0`
  postcondition: 变更/新增文件全部落在 allowed_files 内；两份参考工程仍为未跟踪（`??`）且**未被暂存**
  negative_check: 任何 allowed_files 之外的暂存项即 REJECT

Stop conditions:
- 若 12 路出现跨端口 → 单次原子读取不成立，停工报告（当前不成立，12 路全在 GPIOB）
- 若发现 `GPIO_LINE_SENSOR_PORT` 组级宏消失 → 停工
- 若需要改 `board.syscfg` 才能编译 → 超范围，停工重新决策
- 若需要动 `track_follow.c` 才能编译 → 违反 §15.2，停工

---

## 5. 拓扑影响（预告，以验收实测为准）

- 新增类 `Gray_API`（`+Gray_ReadDarkBitmap()`、`GRAY_CHANNEL_COUNT`）
- 新增类 `GrayPort_API`（`gray_port_read()`、`gray_port_channel_mask()`）
- 新增边 `Gray_API --> GrayPort_API`、`GrayPort_API --> DL_HAL : GPIO_LINE_SENSOR = GPIOB 12 路单次读取`
- **V03 保持 `partially closed`** —— `track_follow.c` 未动，残留点仍在。
  V03 的关闭时机是**上层重置删除 `track_follow.c` 之时**，不是 D12 建成之时。
  （`plan_driver_first_order.md` §6 表述「与 D12 同批解决」易被误读为 D12 完工即关闭，本行为准。）
- D12 由 `GAP`+`HOLD` 改为 `DONE`

---

## 6. 移交用户的硬件实测项（Driver 侧无法自证，依 §15.4）

| # | 项 | 动作 | 后果 |
|---|---|---|---|
| H1 | **供电电压** | 确认 12 路模组供电是 3.3V 还是 5V。若 5V，须核实 MSPM0G3519 对应 PB 脚是否耐 5V；不耐则**必须**改 3.3V 供电或每路串 300–1000Ω | 手册 p.31：5V 供电时输出高约 **4.0V**。灌进不耐压的脚 = 烧 IO。**这是本次唯一的硬件风险项** |
| H2 | **位序左右** | 车压在线上，人为把黑线移到阵列**最左**，读 `Gray_ReadDarkBitmap()`，看是 bit0 还是 bit11 置位 | 厂商约定 P1=最右（F8），本仓注释写 IN1=最左（C1），二者矛盾。**未实测前，任何左右权重表都是猜的** |
| H3 | 电位器 | 按手册 p.23/p.24 口诀逐路调：高度固定顺拧到底 / 浅色逆拧待灯亮起 / 求稳继续微拧少许 / 移至深色绿灯即灭 / 上下微抖灯仍不变 | 未调好 = 位图全 0 或全 1，与固件无关 |
| H4 | 安装高度 | 灯罩底部离地 **10–30mm**（F7） | 超范围 = 误判 |

> **修正点只有一个**：位序方向（H2）的修正只能落在未来 Middleware 的权重表里，
> **不得**在 Driver 里加第二个反转开关。同 `encoder.c:41` `s_direction_sign[]` 的教训（§8.2）。

---

## 7. 施工与验收

**Status: ACCEPTED**（2026-07-17）

| 行 | 结果 |
|---|---|
| E01 | clean 固件构建 exit **0**、诊断 **0**；`gray.c`/`gray_hw.c` 实测经 tiarmclang 编译，`.out` 实测重链接，"up to date" 空转 **0** |
| E02 | `gray.c`/`gray.h`/`gray_port.h` 中 `ti_msp_dl_config|ti/driverlib|DL_` 命中 **0** |
| E03 | 主机 **109 PASS / 0 FAIL**（101 基线 + 8 新增，零回归） |
| E04 | `gray_hw.c`：`DL_GPIO_readPins` 恰好 **1**、`DL_GPIO_PIN_` **0** |
| E05 | `hc-team/app` + `board.syscfg` 状态**空** —— 未制造调用者、未动配置源 |
| E06 | 变更文件全在 allowed_files；两份参考工程仍未跟踪且未暂存 |

**变异验证（证明用例有牙）**：
- 把散射改成恒等映射（`raw & (1u<<channel)`）→ 4 条用例失败
- 把端口读取挪进循环（复刻 track_follow 的 12 次分读）→ 调用计数用例失败

**观察项**：`Gray_ReadDarkBitmap` 未出现在 `.map` 中 —— 因零调用者被 `--gc-sections` 剔除。
已用对照证明这是链接器行为而非缺陷：同样零调用者的 `Imu_Update` 在 map 中为 0，
而有调用者的 `Imu_Init` 为 3。`tiarmnm gray.o` 实测含 `T Gray_ReadDarkBitmap`。
属 §15.1 预期状态。
