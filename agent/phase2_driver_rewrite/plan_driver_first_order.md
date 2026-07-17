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
| D09 | step_motor / emm42 | `driver/step_motor/` | `DONE` | P5 / P7 | — |
| D10 | uart_vofa | `driver/uart_vofa/` | `DONE` | P5 | ⚠ PA0/PA1 待硬件组引出 |
| D11 | imu（单轴 Z） | `driver/imu/` | `DONE` | **P8 / P8B** | ⚠ **上位机配置 230400+500Hz + 装平 + 正方向实测**（见 §4.2） |
| **D12** | **gray（12 路灰度）** | **不存在** | **`GAP` + `HOLD`** | **未立** | **见 §3 —— Driver 层最后一块缺口** |
| D13 | board_gpio → runtime 过渡边 | `driver/board_gpio/` | `DEBT` | 未立 | 见 §5.1 |
| D14 | BSL ENTRY 监听器 | 未定 | `GAP` | 未立 | 见 §5.2 |

### 结论

> **Driver 层实质只剩 D12（灰度）一块缺口，而它正处于用户暂缓裁定下。**
> D13/D14 是小体量债，不阻塞任何功能。
> 也就是说：**用户解除灰度裁定之时，就是 Driver 层收官之时。**

---

## 3. D12 —— 12 路灰度（唯一实质缺口）

**状态：`HOLD`（用户 2026-07-17 裁定「灰度暂不编写，后续会修改」，该裁定至今有效）**

> ⚠ 同日用户解除的是 **IMU** 的暂缓裁定，**灰度的没有解除**。动 D12 前必须先向用户确认。

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

### 4.1 编码器方向（承接 `plan_qei_gray_pinmux.md` §7.1）

- `encoder.c:41` `s_direction_sign[] = {-1, 1}` 是**旧板 AB 接线**的标定值。
- 新板编码器在 PB7/PB9（左）、PB10/PB11（右）。若硬件组按 A→PB7/PB10、B→PB9/PB11 正确接线，
  应改为 `{1, 1}`。
- 验证：手推左轮前进，`Encoder_GetSnapshot()` 左轮速度应为**正**。
- ⚠ **修正点只有一个**（`s_direction_sign`）。加第二个反转开关会与它抵消。
- **未实测前不得改**（§8.1：禁止猜方向）。

### 4.2 IMU（承接 `plan_p8_imu_rewrite.md` §9）

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

---

## 5. Driver 层小体量债（不阻塞，可随时派工）

### 5.1 D13 —— `board_gpio` → `runtime` 过渡边

拓扑记为 `BoardGpio_API --> Runtime_API : transitional raw counts and key edge bitmap`。
编码器已改硬件 QEI 后，这条边的「raw counts」部分是否还有存在必要，需要一次巡查再定。
**不要在没读代码前就假设它该删。**

### 5.2 D14 —— BSL ENTRY 监听器

`UART_BSL_ENTRY`（UART0/9600/无 DMA）已配好但**无消费者**：ENTRY 字节 `0x22` 的监听与软件跳 BSL 未落地。
在此之前**软件跳 BSL 不可用**，只能硬件 BSL invoke 引脚 + 复位。
参考实现：SDK `bsl_software_invoke_app_demo_uart/main.c:197`（判 `0x22`）+ `invokeBSLAsm()`
（擦 SRAM + `DL_SYSCTL_RESET_BOOTLOADER_ENTRY`，含 BSL_ERR_01 勘误绕行）。

### 5.3 遗留空目录

`hc-team/driver/eeprom/` 是空目录（P6 已删 `at24cxx.*`，git 不跟踪空目录）。无害，清理与否随意。

---

## 6. 上层重置（裁定解除后才启动，**当前不得施工**）

记录在此仅为**不丢失**，不代表可以开工。届时对应关闭的违规登记：

| 违规 | 内容 |
|---|---|
| V03 | `track_follow.c` 直接调 DL HAL（**与 D12 同批解决**） |
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
