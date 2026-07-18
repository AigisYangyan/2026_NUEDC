# 严格计划表：Driver 层先行（上层整体重置前）

状态：**生效中**
建立：2026-07-17
依据：用户裁定 2026-07-17 —— **App 上层（Service / Task / Scheduler / UI）后续整体重置重写；
当前阶段只做 Driver 层下层接口，不管上层调用者。** 规范条款见 `AGENTS.md` §15。

> **本表是 Driver 层的唯一进度权威。** 开工前必读，完工后必更（与 `api_architecture_topology.md` §14 同级强制）。
> 架构现状的权威仍是 `agent/api_architecture_topology.md`；本表只管**做到哪了、还差什么、被谁卡住**。

---

## 1. 裁定对施工的三条硬性影响

1. **Driver 零调用者 = 预期状态。** 不写「待接入」「未验证」这类未结项，不因此推迟施工。
2. **禁止在 App Task 里直接调 Driver 来"制造调用者"。** 那是复制 V07/V03 违规，`AGENTS.md` §11 第 1 条禁止。
   接线只能等上层重置时按 §3.4 建 Service。
3. **禁止为假想的上层预建接口。** 判据（`AGENTS.md` §15.3）：
   > 每个公共接口都必须能用「**器件能做什么**」解释；只能用「上层可能要什么」解释的接口，删掉。

---

## 2. Driver 层逐项状态

图例：`DONE` 已验收 / `GAP` 缺口未做 / `DEBT` 存量债 / `HOLD` 用户裁定暂缓

| # | 模块 | 目录 | 状态 | 派工 | 剩余事项 |
|---|---|---|---|---|---|
| D01 | clock | `driver/clock/` | `DONE` | P1 / P1F | — |
| D02 | mspm0_runtime | `driver/mspm0_runtime/` | `DONE` | P1 / P1F / P5 / P8 | — |
| D03 | board | `driver/board/` | `DONE` | P5 | — |
| D04 | encoder | `driver/encoder/` | `DONE` | P2 / P2F | ⚠ **方向须新板实测**（见 §4.1） |
| D05 | motor | `driver/motor/` | `DONE` | P3 | — |
| D06 | key | `driver/key/` | `DONE` | P4 | — |
| D07 | oled | `driver/oled/` | `DONE` | P6 | — |
| D08 | board_uart（4 角色） | `driver/board_uart/` | `DONE` | P5 / P8 | — |
| D09 | step_motor / emm42 | `driver/step_motor/` | `DONE` | P5 / P7 / **P9** | 可补主机测试（V18 修复后已具备条件） |
| D10 | uart_vofa | `driver/uart_vofa/` | `DONE` | P5 / P9 | ⚠ PA0/PA1 待硬件组引出；`u8` 命名空间污染登记 V19 |
| D11 | imu（单轴 Z） | `driver/imu/` | `DONE` | **P8 / P8B** | ⚠ **上位机配置 230400+500Hz + 装平 + 正方向实测**（见 §4.2） |
| **D12** | **gray（12 路灰度）** | `driver/gray/` | **`DONE`** | **P9.T1** | ⚠ **供电电压确认 + 位序实测**（见 §4.4） |
| D13 | board_gpio → runtime 过渡边 | `driver/board_gpio/` | `DEBT` | 未立 | 见 §5.1 |
| D14 | BSL ENTRY 监听器 | `driver/bsl_entry/` | `DONE` | 未立 | 契约 `5e3cf95` / 实现 `c84bf3c`，见 §5.2 |

### 结论（2026-07-17 更新）

> **Driver 层已收官。** 用户于 2026-07-17 解除灰度暂缓裁定，D12 随即由 P9.T1 补齐 ——
> 「用户解除灰度裁定之时，就是 Driver 层收官之时」这句话已经兑现。
> D13 是小体量债，不阻塞任何功能；D14 已于 2026-07-18 收口（见 §5.2）。
>
> **瓶颈已经不在 Driver 层，而在 §4 的硬件实测项** —— 其中 **H1 灰度供电电压会烧 IO**。
> 总汇报见 **`docs/driver层总汇报.md`**。
>
> **2026-07-17 验收封包完成**：主机测试 109 PASS / 0 FAIL，固件 clean 重建 0 诊断，
> arch-auditor 独立复审依赖矩阵/ISR/单一所有者/电机安全零违规（5 条文档性 findings 已处置）。
> 封包报告与**拿到硬件后的行动顺序（H1→H4→H3→H2→电机分阶段首验）**见
> **`docs/driver层验收封包报告.md`**。

---

## 3. D12 —— 12 路灰度（已完成）

**状态：`DONE`（P9.T1，契约 `b421682`，代码 `b423593`）**

> 用户 2026-07-17 消息「这是我新买的灰度模块…开战新的 driverplan，将它移植 driver 层」
> **解除了同日早些时候的暂缓裁定**。D12 随即施工完毕。
>
> 器件：武汉无名创新 NCHD1（迹）。配置手册：**`docs/12路灰度传感器配置指南.md`**。
> 公共接口收敛为单个 `Gray_ReadDarkBitmap()`；12 路一次原子读取兑现了 board.syscfg 用 PB8 换来的性质。
> **V03 残留点未动**（§15.2 禁止在 App Task 里制造调用者），其关闭时机是上层重置删除 `track_follow.c` 之时。

### 查实的现状（2026-07-17）

- **`driver/gray/` 根本不存在。** Driver 层没有灰度模块。
- 12 路灰度采样目前写在 **`app/tasks/track_follow/track_follow.c`**：
  - `track_follow.c:26` 直接 `#include "ti_msp_dl_config.h"`
  - `track_follow.c:61` 直接调 `DL_GPIO_readPins(GPIO_LINE_SENSOR_PORT, ...)`
- 这是 **V03（App 直接调用 SysConfig / NVIC / DL HAL）** 的存量违规，也是 V03 至今
  `partially closed` 而非 `closed` 的**唯一残留点**（P5 R03 已清零 vision_bus / stepmotor_bus / uart_stress）。

### 解除裁定后的施工要点（预置，不代表已派工）

1. 新建 `driver/gray/`，把 GPIO 读取与 `TRACK_SENSOR_COUNT=12` 收进 Driver。
2. **保住 12 路同端口原子采样** —— 这是 `plan_qei_gray_pinmux.md` 特意用 PB8 而非 PA7 换来的性质，
   一次 `DL_GPIO_readPins` 读一个端口拿全 12 位。拆成多次读会引入路间时间偏斜。
3. **`Calculate_Track_Error()` 不属于 Driver** —— 那是循迹算法，按 §3.3 归 Middleware。
   Driver 只出「12 路原始/去抖后的位图」，不出误差。
4. 单一所有者：去抖 / 反相 / 阈值只能有一个层做，不得 Driver 和 Middleware 各做一次（§8.2）。
5. `TrackN` 是可写全局（V13），随上层重置处理，**不在 D12 范围内**。

---

## 4. 移交用户的硬件实测项（Driver 侧无法自证）

依 `AGENTS.md` §15.4：器件级验证由用户用**厂商上位机 / 实物**直接完成，不经 App 链路。

> **2026-07-17 网表对照更新**（`docs/网表对照结论.md`，硬件组拓展板网表逐网核对）：
> **H1 坐实**——灰度确接 5V 网，等硬件组改 3.3V 或串电阻；**H2 电气链钉死**——连接器
> 1–12 脚与 IN1–IN12 直通零交叉，安装定义已给（G1/P1 = 车左），只剩装后一分钟实测；
> **新增 M1**——U33/U34 插座上电机与编码器左右命名交叉（U33={左电机,右编码器}、
> U34={右电机,左编码器}），速度环会左右串，等用户拍板单点修正方案（推荐：定义 U33=左轮，
> syscfg 对调 QEI `$name`，未拍板前固件不动）；**新增 N1/N2/N3**——K4 按键断线（PA14 未连到
> 按键连接器）、PA0/PA1 开漏疑缺上拉（VOFA TX 推不出高电平）、IMU TX 电平待确认，均移交硬件组；
> 原「PA0/PA1 待引出」**销项**（本板已引出）。

### 4.1 编码器方向（承接 `plan_qei_gray_pinmux.md` §7.1）

- `encoder.c:41` `s_direction_sign[] = {-1, 1}` 是**旧板 AB 接线**的标定值。
- 新板编码器（2026-07-17 网表定案 + QEI `$name` 对调后）：**左 = PB10(A)/PB11(B) = TIMG8 = U33 插座；
  右 = PB7(A)/PB9(B) = TIMG9 = U34 插座**（驾驶员视角，见 `docs/给硬件组的修改方案.md` §4）。
  若硬件组按 A→PB10/PB7、B→PB11/PB9 正确接线，预期改为 `{1, 1}`。
- 验证：手推左轮前进，`Encoder_GetSnapshot()` 左轮速度应为**正**。
- ⚠ **修正点只有一个**（`s_direction_sign`）。加第二个反转开关会与它抵消。
- **未实测前不得改**（§8.1：禁止猜方向）。

### 4.2 IMU（承接 `done/plan_p8_imu_rewrite.md` §9、`done/plan_p8b_imu_230400_500hz.md` §6）

配置指南已出：**`docs/IMU陀螺仪配置指南.md`**（面向厂商上位机）。要点：

| 项 | 动作 |
|---|---|
| **波特率** | 出厂 115200 → **必须用上位机改成 230400**（P8B 裁定；固件 syscfg 已配 230400，两边不一致就是乱码） |
| **输出速率** | 出厂 10 Hz → **改 500 Hz**（依据：云台前馈延迟。10 Hz 下 100 Hz 控制环 90% 的 tick 读到陈旧值，内环直接失效） |
| 自动零偏校准 | **必做**，车放平静止 ~20 s |
| **装平** | Z 轴须垂直地面。歪装会把 pitch/roll 分量混入 yaw，上坡/侧倾时航向角漂 |
| 正方向 | 车逆时针转，看 `yaw_deg` 增或减 → 告诉我，符号修正**只做一处** |

> **注意「速率≠精度」**：RRATE 只改数据新鲜度，不改任何精度指标（器件内部采样 50 kHz、Kalman 常驻）。
> Yaw 漂/不准的解药是零偏校准，不是提速率。详见 `docs/IMU陀螺仪配置指南.md` §1。

### 4.3 其他

- 核心板晶振书面确认（PA3/PA5/PA6 是否实焊）。
- PA0/PA1（VOFA）待硬件组引出；引出前 VOFA 实物不可用，固件已就绪。

### 4.4 灰度（承接 `plan_p9_gray_driver.md` §6）

配置指南已出：**`docs/12路灰度传感器配置指南.md`**。要点：

| 项 | 动作 |
|---|---|
| **供电电压** | ★ **唯一硬件风险项**。5V 供电时模组输出高约 **4.0V**（手册 p.31）。须确认 MSPM0G3519 的 PB 脚是否耐 5V；不耐则**必须**改 3.3V 供电或每路串 300–1000Ω，否则烧 IO |
| **位序左右** | 黑线移到阵列最左，读 `Gray_ReadDarkBitmap()`，看 bit0 还是 bit11 置位。厂商约定 P1=最右，本仓 syscfg 注释写 IN1=最左，**二者矛盾且固件无法自证** |
| 电位器 | 12 路逐路调（手册 p.23 三步法）：高度固定顺拧到底 / 浅色逆拧待灯亮起 / 求稳继续微拧少许 / 移至深色绿灯即灭 / 上下微抖灯仍不变 |
| 安装高度 | 灯罩底部离地 **10–30mm** |

> ⚠ **修正点只有一个**：位序方向的修正只能落在未来 Middleware 的权重表里，
> **不得**在 Driver 里加第二个反转开关（同 `s_direction_sign` 教训，§8.2）。

---

## 5. Driver 层小体量债（不阻塞，可随时派工）

### 5.1 D13 —— `board_gpio` → `runtime` 过渡边

拓扑记为 `BoardGpio_API --> Runtime_API : transitional raw counts and key edge bitmap`。
编码器已改硬件 QEI 后，这条边的「raw counts」部分是否还有存在必要，需要一次巡查再定。
**不要在没读代码前就假设它该删。**

### 5.2 D14 —— BSL ENTRY 监听器 `DONE`（2026-07-18，契约 `5e3cf95` / 实现 `c84bf3c`）

新建 `hc-team/driver/bsl_entry/`：`bsl_entry.h`（公共面 `BslEntry_IsrOnByte(uint8_t)` ISR 契约符号 +
边界 seam `BslEntry_InvokeBsl(void)`）+ `bsl_entry.c`（判触发字节 0x22 → 调 `BslEntry_InvokeBsl`，
单一所有者）+ `bsl_entry_invoke.c`（target-only，`invokeBSLAsm` 内联汇编：擦 SRAM 绕行 BSL_ERR_01
勘误 + RESETLEVEL/RESETCMD，永不返回；主机测试用 `fake_bsl_invoke.c` 替身计数）。
`mspm0_runtime.c` 新增 `UART_BSL_ENTRY_INST_IRQHandler`（= UART0 向量）委派
`runtime_handle_uart_irq(UART_BSL_ENTRY_INST, BslEntry_IsrOnByte)`；`board.c:Board_EnableInterrupts`
新增 `NVIC_EnableIRQ(UART_BSL_ENTRY_INST_INT_IRQN)`；`board.syscfg` 同步
`UART_BSL_ENTRY.enabledInterrupts=["RX"]`+`rxFifoThreshold=ONE_ENTRY`。

触发时机 = ISR 内直接跳 BSL，对 V09「ISR 只做最小搬运/置位」的显式豁免（契约冻结，正当性 = 跳转即
复位、永不返回、无返回栈、无共享态竞争），登记为拓扑 §6 `V27`。

四证据行：E01 全仓依赖扫描 0 命中越层引用；E02 变更范围仅命中 allowed_files；
E03 主机测试 **404 PASS / 0 FAIL**（401 基线 + 3 新增）；E04 固件 clean 构建 exit 0，
两 `.o` 经 `Debug/2026_Diansai_linkInfo.xml` 确证进链，`UART0_IRQHandler` 向量绑定
`runtime_handle_uart_irq` 分发确认。

参考实现：SDK `bsl_software_invoke_app_demo_uart/main.c:197`（判 `0x22`）+ `invokeBSLAsm()`
（擦 SRAM + `DL_SYSCTL_RESET_BOOTLOADER_ENTRY`，含 BSL_ERR_01 勘误绕行）。

### 5.3 遗留空目录

`hc-team/driver/eeprom/` 是空目录（P6 已删 `at24cxx.*`，git 不跟踪空目录）。无害，清理与否随意。

---

## 6. 上层重置（裁定解除后才启动，**当前不得施工**）

记录在此仅为**不丢失**，不代表可以开工。届时对应关闭的违规登记：

| 违规 | 内容 |
|---|---|
| V03 | `track_follow.c` 直接调 DL HAL。**注意：D12 已建成（P9.T1）但 V03 未关** —— 关闭时机是上层重置**删除** `track_follow.c` 之时，不是 Driver 建成之时（§15.2 禁止改 App Task 去接 Driver） |
| V07 | TaskGroups / SpeedLoop / Task1 直接编排 Driver 与 PID |
| V10 | `app/service/` 无有效源 API（当前 `git ls-files` 命中 **0**） |
| V13 | Scheduler / PID / TrackFollow 暴露可写全局（`g_eSysFlagManage`、`g_PID_instances`、`TrackN`） |
| V14 | UI 直接调用 Key/OLED Driver，且 UI 头暴露 Key 类型 |
| V15 | VOFA Scheduler 直接依赖 VOFA Driver、PID、TrackFollow |

⚠ **现有 `app/**` 代码不是重置时的范例**（`AGENTS.md` §15.6）—— 它就是上面这堆违规本身。

---

## 7. 维护规则

1. 每完成一个 Driver 派工，**必须**更新 §2 表格对应行的状态与派工号。
2. 新发现的 Driver 层缺口/债，**必须**登记进 §2 表格，并在 §3 或 §5 展开。
3. 用户裁定变化（暂缓 / 解除）**必须**当场更新 §2 状态列与 §3 抬头。
4. 本表与代码冲突时**以代码为准**：先把本表改成真实状态，再继续设计（同 §14 拓扑规则）。
