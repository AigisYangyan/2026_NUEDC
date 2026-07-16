# P2.1：编码器测量口径基线

> 日期：2026-07-13  
> 状态：文档基线已建立，硬件口径待确认（八项全部未实测）
> 来源：当前工作区 `hc-team/driver/encoder/encoder.c/.h`、`hc-team/driver/mspm0_runtime/mspm0_runtime.c`、`project/mspm0/board.syscfg`

---

## 1. 硬件连接与信号来源

| 项目 | 左轮 | 右轮 |
|---|---|---|
| A 相引脚 | PA7 | PB14 |
| B 相引脚 | PB20 | PB0 |
| GPIO 中断配置 | RISE_FALL | RISE_FALL |
| 中断组 | GROUP1 (GPIOA/GPIOB) | GROUP1 (GPIOA/GPIOB) |

来自 `board.syscfg`：
- `GPIO_GRP_MOTOR_INTERRUPT_PIN_L_A` = PA7
- `GPIO_GRP_MOTOR_INTERRUPT_PIN_L_B` = PB20
- `GPIO_GRP_MOTOR_INTERRUPT_PIN_R_A` = PB14
- `GPIO_GRP_MOTOR_INTERRUPT_PIN_R_B` = PB0

---

## 2. Runtime 正交判向公式

`mspm0_runtime.c` 中 `runtime_handle_encoder_irqs()` 使用如下规则：

| 轮 | A 相边沿 | B 相边沿 |
|---|---|---|
| 左轮 | `A != B ? +1 : -1` | `A == B ? +1 : -1` |
| 右轮 | `A == B ? +1 : -1` | `A != B ? +1 : -1` |

**观察**：左右轮使用不同的正交判向公式。这是当前代码事实，P2 必须在此事实基础上只保留 Encoder Driver 中的一次安装方向修正，不得再额外反向。

---

## 3. Encoder 方向修正

`encoder.c` 中 `g_tEncoderChannelCfg`：

| 轮 | 对应电机 | direction_sign |
|---|---|---|
| 左轮 | MOTOR_LEFT | -1 |
| 右轮 | MOTOR_RIGHT | +1 |

速度计算公式：
```c
motor->speed = Encoder_CalcSpeed(delta) * (float)cfg->direction_sign;
```

`Motor_T.encoder_sign` 字段（在 `motor.h` 中声明）**仅初始化、未读取**，属于冗余方向表达，应在 P2.3 中删除。

**唯一方向修正点声明**：P2 之后，车辆前进方向修正只允许在 `encoder.c` 的 `direction_sign` 中保留一次；Runtime 正交判向公式保持现状，不再修改。

---

## 4. PPR、轮径与速度换算

| 参数 | 当前值 | 来源/说明 |
|---|---|---|
| PPR (`ENCODER_INT_PARAM_PPR`) | 1560 | 代码默认值；未确认是电机轴脉冲、输出轴脉冲还是四倍频后计数 |
| 轮径 | 68.6 mm | 代码默认值 |
| 轮周长 | π × 68.6 ≈ 215.51 mm | 由轮径计算 |
| 摩擦修正系数 `miu` | 1.0 | 代码默认值；未通过实际测量标定 |
| 默认采样周期 | 10 ms | 由调用任务周期决定；旧 `Encoder_UpdateSample()` 内部使用固定参数 |

速度换算公式（旧实现）：
```c
speed_factor = wheel_circ_mm * miu / (sample_period_sec * ppr * 1000.0f);
speed_mps = pulse_count * speed_factor;
```

其中 `pulse_count` 是本次与上次总计数的差值。

**风险**：PPR=1560 未确认是否已包含四倍频。若编码器本身是 390 PPR × 4 倍频，则当前值可能正确；若编码器已是 1560 PPR 且再四倍频，则当前值会偏小 4 倍。

---

## 5. 单位与口径

| 量 | 单位 | 备注 |
|---|---|---|
| raw_total | 有符号脉冲计数 | Runtime 维护 |
| total_pulses | 有符号脉冲计数 | 经一次 direction_sign 修正 |
| delta_pulses | 有符号脉冲计数 | 本次 - 上次 |
| speed_mps | m/s | Encoder 输出 |
| PID target/feedback | m/s | 当前 SpeedLoop/Task1 使用 |
| `ENCODER_PARAM_MAX_TARGET_SPEED` | 1200 | 代码注释称“mm/s”，但当前闭环实际使用 m/s；属于危险双口径，P2 后应统一为 m/s 并删除该参数或明确其用途 |

---

## 6. 行为对照（旧实现）

在固定条件下计算旧 `Encoder_CalcSpeed()` 输出，作为新实现行为对照：

| 条件 | 计算 |
|---|---|
| PPR=1560, 轮周长=215.51 mm, miu=1.0, 周期=10 ms | speed_factor = 215.51 / (0.01 × 1560 × 1000) ≈ 0.013815 m/s 每脉冲 |
| 10 个脉冲 / 10 ms | speed ≈ 0.13815 m/s |
| 100 个脉冲 / 10 ms | speed ≈ 1.3815 m/s |
| 100 个脉冲 / 20 ms | speed ≈ 0.6907 m/s |

新实现必须使用调用者传入的真实 `elapsed_ms`，不得固定 10 ms。

---

## 7. 未实测项（必须在 P2 硬件验收前补齐）

- [ ] 左轮 raw_total 在手动正转时的符号
- [ ] 左轮 raw_total 在手动反转时的符号
- [ ] 右轮 raw_total 在手动正转时的符号
- [ ] 右轮 raw_total 在手动反转时的符号
- [ ] PPR 真实定义确认（电机轴 / 输出轴 / 四倍频）
- [ ] 轮径/周长实测值
- [ ] `miu` 标定依据
- [ ] 固定圈数手转后累计脉冲与 PPR 对应关系

---

## 8. P2 数据处理链目标

```text
Encoder GPIO AB 相
 -> Shared IRQ 使用当前正交公式累计 raw_total
 -> 原子读取 left/right raw snapshot
 -> Encoder 仅一次安装方向修正 (direction_sign)
 -> delta = current_total - previous_total (无符号模运算处理回绕)
 -> 使用真实 elapsed_ms、PPR、轮周长、miu 换算 speed_mps
 -> EncoderSnapshot 按值交给 Service
 -> Service 将 feedback_mps 传给 PID
 -> Service 将 PID 输出交给 Motor
```

**禁止**：在 Runtime 和 Encoder 同时修正车辆前进方向；禁止在多个层重复缩放/滤波/限幅。
