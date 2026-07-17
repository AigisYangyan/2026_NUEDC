---
name: data-chain
description: AGENTS.md §8.2 防止重复数据处理全文 + 本工程数据链所有权事实。编辑 hc-team/driver/encoder/**、driver/gray/**、driver/imu/**、middleware/pid/** 或任何涉及采样、传感器、编码器、PID、滤波、限幅、反向、归一化、单位换算的代码前必须加载。Use before modifying any sampling, sensor, encoder, PID, filtering, scaling, or unit-conversion code.
---

# 防止重复数据处理（AGENTS.md §8.2）

> **同步义务**：本文正文第一节逐字来自根 `AGENTS.md` §8.2（唯一权威）。修改 AGENTS.md §8.2
> 必须同步本文件，反之亦然；两处不一致时以 AGENTS.md 为准并立即修正本文件。

## §8.2 全文

修改采样、传感器、编码器、Middleware、PID 或控制输出前，必须阅读上下游 API 的实际实现和全部调用点，不能只看头文件。必须先记录该数据的处理链：

```text
原始来源 -> Driver 采样/校准 -> Service 单位与语义转换
         -> Middleware 算法 -> Service 输出转换 -> Driver 执行
```

对链路中的每个值必须确认：

- 单位和数值尺度，例如脉冲、转数、RPM、弧度、角度、毫米、秒或毫秒。
- 正负方向、坐标系、零点和机械安装方向。
- 数据是瞬时值、累计值、增量值还是已经计算的速度。
- 是否已经反向、归一化、限幅、去抖、校准、滤波、积分或微分。
- 采样周期、更新时间、有效期以及由谁拥有和更新。

同一种数据变换必须只有一个明确所有者。除非算法设计明确要求多级滤波并有参数与测试依据，否则禁止在 Driver、Service 和 Middleware 中重复执行方向反转、滤波、缩放、积分、微分、限幅或单位换算。例如，编码器 Driver 已完成方向统一或滤波时，PID 输入路径不得再次进行同类处理。

修改数据生产者时必须搜索并审查所有消费者；修改消费者时也必须追溯到原始生产者。若无法证明输入数据当前处于什么处理阶段，不得继续添加新的数据处理。

## 本工程数据链所有权事实（改动前先核对）

- **编码器方向**：AB 相物理接反是预期内硬件事实，由 `driver/encoder/encoder.c` 的
  `s_direction_sign[]` 作为**唯一**修正点吸收（新板须实测重标）。任何第二处方向反转都是违规——
  两处反转互相抵消，等于没修。
- **编码器采样**：`app/tasks/task_groups.c` 是唯一采样所有者，计算真实 elapsed；
  SpeedLoop/Task1 只消费 `Encoder_GetSnapshot()` 快照（V05/V06 closed）。
- **PID**：双轮入口按值输入目标/反馈、双输出指针（P2F.T1）；PID 不感知 Motor/Encoder 类型（V04 closed）。
- **步进速度**：RPM 限幅与 ×10 协议尺度换算唯一所有者是 `driver/step_motor/emm42.c`；
  上游 `stepmotor_bus.c` 不再做第二次。
- **灰度**：`Gray_ReadDarkBitmap()` 一次 `DL_GPIO_readPins` 原子读全 12 路（单端口 PB 设计前提，
  IN4 在 PB8 就是为此让出的）；不要退回逐路分读（路间时间偏斜）。
- **IMU**：模组内置 Kalman，只出 Yaw 与 GyroZ（5 字节定长帧，230400/500Hz）；
  上层不得再对 Yaw 做第二次滤波/积分。
