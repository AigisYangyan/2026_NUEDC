# 计划：P8B —— IMU 链路提速至 230400 + 500 Hz

状态：`ACCEPTED`（2026-07-17 自闭环施工 + 验收）
日期：2026-07-17
流程：单 agent 自闭环（`.agents/skills/embedded-closed-loop`）。**本契约在写任何生产代码前提交**。
前置：P8（`plan_p8_imu_rewrite.md`，`ACCEPTED`，提交 `5b7e15a`）。

## 0. 用户裁定（2026-07-17）

用户选定 **230400 波特率 + 500 Hz 输出速率**，理由（用户原话要点）：
「后续或许可以同时给底盘和云台数据，让云台算法更稳定」「中断资源其实还比较充裕，整个底盘外设就那些」。

### 0.1 裁定审查：结论采纳，但**理由须更正后入账**

用户同时表述了「底盘 500Hz 精准度更高」。**这一条不成立，不得作为本次施工的依据。**

- **输出速率与精度在本器件中解耦。** 模块内部采样率 50 kHz、Kalman 常驻运行；RRATE 只决定
  **对外汇报频率**，不改变任何精度指标（分辨率 0.0001°、航向精度 0.2°、零漂 ±1°/s、
  零偏稳定性 2.03°/h 在 10 Hz 与 1000 Hz 下完全相同）。
- **RRATE 改变的是数据龄**：200 Hz 最坏 5 ms → 500 Hz 最坏 2 ms。
- **对底盘无实益**：控制环 100 Hz（10 ms/拍），环路周期本身的滞后即 10 ms，
  远大于 5 ms 与 2 ms 之差；500 Hz 喂 100 Hz 环，5 帧丢 4 帧。

**采纳本裁定的真实依据 = 云台前馈延迟**（用户自己提到的方向，本表补全其量化）：

| 车身转速 | 200 Hz（5 ms 龄）指向误差 | 500 Hz（2 ms 龄）指向误差 |
|---|---|---|
| 180 °/s | **0.90°** | **0.36°** |
| 90 °/s | 0.45° | 0.18° |

模块自身航向精度 **0.2°**。故 200 Hz 下**陈旧误差(0.90°)是传感器误差(0.2°)的 4.5 倍，为主导项**；
500 Hz 压到 0.36°，与传感器精度同量级。**1000 Hz（0.18°）已跌破噪声底，属追噪声** → 不采用。

> **500 Hz 是本器件在本应用下的甜点，此结论有量化依据，非偏好。**

### 0.2 中断负担核算（证实用户判断）

IMU RX 为**逐字节中断，无 DMA**。Cortex-M0+ @80 MHz，按每字节约 100 周期估：

| 输出速率 | 中断/秒 | CPU 占用 | 线路占用 @230400 | FIFO(128B) 可缓冲 |
|---|---|---|---|---|
| 200 Hz | 2 000 | 0.25% | 8.7% | 64 ms |
| **500 Hz** | **5 000** | **0.63%** | **21.7%** | **25.6 ms** |
| 1000 Hz | 10 000 | 1.25% | 43.4% | 12.8 ms |

**用户判断成立**：500 Hz 仅耗 0.63% CPU。FIFO 128 字节在 500 Hz 下仍有 25.6 ms 余量，
远宽于 10 ms 任务周期 → **不动 FIFO 容量**（避免无依据的改动）。

## 0.3 验收记录（2026-07-17）

契约冻结于 `92e11f5`，早于任何生产代码改动。

| ID | 结果 | 观察到的后置条件 |
|---|---|---|
| E01 | PASS | clean 固件构建 exit 0；`: (warning\|error\|remark)` 命中 **0** |
| E02 | PASS | 主机 9 套件 **101 PASS / 0 FAIL**（P8 基线 98 + 新增 3） |
| E03 | PASS | `Debug/ti_msp_dl_config.h:223` `UART_IMU_BAUD_RATE` = **230400** |
| E04 | PASS | `imu_uart.c` 中 `115200` **0 命中** —— TX 超时算式已按 230400 重算，无旧常量残留 |
| E05 | PASS | `IMU_OUTPUT_RATE_1000\|0x0Eu` 在 `driver/imu/` **0 命中** —— 未暴露 1000 Hz |
| E06 | PASS | 生成物 diff 仅 `UART_IMU` 波特率相关 6 行；`QEI\|PWM_DRIVE` 漂移 **0** |

**停止条件均未触发**：既有枚举 `IMU_OUTPUT_RATE_10/50/100/200_HZ` 的寄存器映射
（0x06/0x08/0x09/0x0B）逐一复核未变，500 Hz(0x0D) 追加在**末尾**；
施工前主机基线复跑 98/0，无漂移。

**新增用例 3 项**中有一项是**针对本次改动的回归防护**：
`test_existing_rate_codes_unchanged()` 把 200 Hz→0x0B 的映射钉死 ——
若日后有人把新档插进枚举中间，既有取值会静默漂移到别的寄存器编码上，该用例必然失败。

## 1. 变更清单

| 文件 | 变更 | 依据 |
|---|---|---|
| `board.syscfg` | `UART_IMU.targetBaudRate` 115200 → **230400** | §0 裁定 |
| `hc-team/driver/imu/imu.h` | 枚举新增 `IMU_OUTPUT_RATE_500_HZ` | §0 裁定 |
| `hc-team/driver/imu/imu.c` | `rate_code[]` 新增 `0x0D`（datasheet RRATE 表：500 Hz） | 同上 |
| `hc-team/driver/board_uart/imu_uart.c` | TX 有界轮询的每字节超时按 230400 重算；FIFO 容量注释按 500 Hz 重算 | 波特率变更的连带事实 |
| `tests/host/test_imu.c` | 新增 500 Hz 编码用例 | 测试先行 |
| `docs/IMU陀螺仪配置指南.md` | 推荐配置改为 230400 + 500 Hz，并写入 §0.1 的「速率≠精度」更正与量化依据 | 用户须据此操作上位机 |
| `agent/api_architecture_topology.md` | `ImuUart_API --> DL_HAL` 边的波特率标注 115200 → 230400 | §14 |

**不做**：
- **不暴露 `IMU_OUTPUT_RATE_1000_HZ`** —— §0.1 已证其跌破噪声底，无真实需求（AGENTS.md §8.3/§15.3）。
- **不动 FIFO 容量**（§0.2 已证 25.6 ms 余量充足）。
- **不实现波特率设置接口** —— datasheet BAUD 表 `0x0006` 同时标注 115200 与 230400（P8 §1.3 矛盾 B），
  230400 编码无可靠记载。**波特率由用户用厂商上位机设置**，固件只被动匹配。

## 2. 三个设计问题

本次不新增抽象、不改变分层，仅调整**参数**与**枚举的一个取值**。
`Imu_SetOutputRate()` 的抽象（「设置器件自主输出速率」）与隐藏（RRATE 寄存器编码）均不变。
枚举新增项仍满足 AGENTS.md §15.3 判据：**500 Hz 是器件真实具备的能力档位**。

## 3. 范围

- `allowed_files`：
  - `agent/phase2_driver_rewrite/plan_p8b_imu_230400_500hz.md`
  - `agent/api_architecture_topology.md`
  - `agent/phase2_driver_rewrite/plan_driver_first_order.md`
  - `board.syscfg`
  - `docs/IMU陀螺仪配置指南.md`
  - `hc-team/driver/imu/imu.h`
  - `hc-team/driver/imu/imu.c`
  - `hc-team/driver/board_uart/imu_uart.c`
  - `tests/host/test_imu.c`
- `forbidden_files`：`hc-team/app/`（全部）、`hc-team/driver/encoder/`、`hc-team/driver/motor/`、
  `hc-team/driver/step_motor/`、`hc-team/middleware/`、`Debug/makefile`、`tests/host/Makefile`
- `preserved_behavior`：
  - `board.syscfg` 除 `UART_IMU.targetBaudRate` 外**一律不动**，尤其 `QEI_*` / `PWM_DRIVE_*`。
  - `Imu_*` 全部函数签名不变；`Imu_Snapshot_t` / `Imu_Diag_t` 布局不变。
  - `IMU_OUTPUT_RATE_10/50/100/200_HZ` 的既有枚举值与对应寄存器编码**不得改动**
    （新增项必须追加在末尾，否则既有取值语义漂移）。

## 4. 证据行（冻结）

| ID | 命令 | 期望 | 后置条件（可观察） |
|---|---|---|---|
| E01 | `rtk make -C Debug all`（clean 后） | exit 0 | 诊断计数 0。口径锚定 `: (warning\|error\|remark)` |
| E02 | `make.bat -C tests/host all`（PowerShell 工具执行） | exit 0 | **≥ 99 PASS / 0 FAIL**（P8 基线 98 + ≥1 新增）。口径锚定 `^\s*PASS:` |
| E03 | `grep -n "UART_IMU_BAUD_RATE" Debug/ti_msp_dl_config.h` | — | 宏为 **230400** |
| E04 | `grep -n "115200" hc-team/driver/board_uart/imu_uart.c` | — | **输出为空** —— 旧波特率常量未残留在 TX 超时算式中 |
| E05 | `grep -rn "IMU_OUTPUT_RATE_1000\|0x0Eu" hc-team/driver/imu/` | — | **输出为空** —— 未暴露 1000 Hz |
| E06 | SysConfig 重新生成后 `diff` 生成物 | — | 差异**仅** `UART_IMU` 波特率相关行；`QEI\|PWM_DRIVE` 命中 **0** |

E 行预算 6/6。无硬件行。

## 5. 停止条件

- `board.syscfg` 改动波及 `QEI_*` / `PWM_DRIVE_*` → 停止（电机链路，§8.1）。
- 既有枚举值 `IMU_OUTPUT_RATE_10/50/100/200_HZ` 的数值或寄存器映射发生任何变化 → 停止。
- 主机基线施工前复跑不是 98/0 → 基线漂移，停止并修订。

## 6. 移交用户（本次无法验证）

1. **必须用厂商上位机把模块设成 230400 + 500 Hz**，否则固件收到的全是乱码。
   固件**不能**代劳改波特率（§1「不做」第 3 条）。
   顺序无所谓，但**两边必须都改完**才能通信。
2. 判据：`Imu_GetDiag()` 中 `frame_count` 正常增长且 `checksum_error_count` 不涨 → 匹配成功；
   反之 → 两边速率不一致。
3. 实测输出速率 = `frame_count` 1 秒增量 ÷ 2，应约等于 **500**。
4. **模块必须装平**：Z 轴须垂直于地面。歪装会把 pitch/roll 分量混入 yaw，上坡或侧倾时航向角漂移。
5. 云台前馈**只能做前馈，不要做成第二个反馈环**（后者会把 IMU 噪声与延迟引入云台环，
   正是用户担心的「多一个变量导致变差」）。车静止时 IMU 的 ±0.2° 噪声会注入云台指令，
   缓解办法是车身角速度低于阈值时不加前馈 —— 属 Service 层设计，按 `AGENTS.md` §15 当前不做。
