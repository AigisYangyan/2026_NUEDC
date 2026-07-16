# P2：Encoder Driver 解耦与数据口径修复计划

状态：in_progress（P2.1 文档部分完成，硬件确认待执行；P2.2 已完成；P2.3-P2.5 待执行）  
前置条件：P1 已提供一致的双轮原始计数快照；Motor P0 安全闸门仍然有效。  
目标：让 Encoder 独立拥有编码器测量状态和方向/速度口径，不再把数据写入 Motor，也不承载底盘目标分配等上层算法。

## 1. 三个设计问题

1. **抽象是什么？**
   - 提供左右编码器在一个采样时刻的一致快照：累计脉冲、周期增量和轮速 `m/s`。
2. **必须隐藏什么？**
   - 上次原始计数、方向修正、PPR、轮周长、校准系数、速度换算和私有快照。
3. **代码属于哪里？**
   - AB 相边沿计数由必须响应中断的底层 Driver/IRQ 所有者完成。
   - 安装方向修正和从脉冲到物理轮速的传感器标定只在 Encoder Driver 做一次。
   - 差速轮目标分配、车辆里程策略和 PID 属于 Middleware/App Service，不属于 Encoder Driver。

## 2. 当前问题证据

- `encoder.h:31` 直接包含 `driver/motor/motor.h`。
- `encoder.c` 把 `encoder_total`、`encoder_delta` 和 `speed` 写进公开的 `g_tMotors[]`，Encoder 没有自己的私有状态。
- Motor 公共结构又持有 PID 指针，PID 读取 `g_tMotors[].speed`，形成 `Encoder -> Motor <-> Middleware PID` 循环耦合。
- Runtime 正交计数已经对左右轮使用不同判向公式，Encoder 通道配置又设置左 `-1`、右 `+1`，Motor 还保存一份未使用的 `encoder_sign`；当前无法证明是否重复取反。
- Encoder API 同时承担原始测量、速度换算、差速目标分配、目标限幅和行驶距离积分，职责混杂。
- 当前控制实际反馈单位为 `m/s`，但 `Encoder_CalcWheelTargets()` 文档和 `1200` 限幅使用 `mm/s`；未使用 API 保留了危险的双口径。
- PID 输出限制为 1000，Motor 又限制为 1000；这是算法限幅和硬件限幅两个不同职责，当前没有明确说明。
- `Mspm0Runtime_GetEncoderCounts()` 分别读取两个 `volatile int32_t`，没有保证左右轮来自同一快照。

## 3. 唯一数据处理链

P2 后数据链固定为：

```text
Encoder GPIO AB 相
 -> Shared IRQ 使用同一正交公式累计 raw_total
 -> 原子读取 left/right raw snapshot
 -> Encoder 仅一次安装方向修正
 -> delta = current_total - previous_total
 -> 使用实际 elapsed_ms、PPR、轮周长和校准系数换算 speed_mps
 -> EncoderSnapshot 按值交给 Service
 -> Service 将 feedback_mps 传给 PID
 -> Service 将 PID 输出交给 Motor
```

必须记录每一步：

- `raw_total`：硬件相位决定的有符号累计脉冲。
- `total_pulses`：统一成“车辆前进为正”后的累计脉冲。
- `delta_pulses`：本次与上次快照的差值，不缩窄为 `int16_t`；先按 `uint32_t` 做模减法再转换为有符号差值，禁止直接进行可能触发 C 有符号溢出的 `int32_t` 减法。
- `speed_mps`：使用本次真实 `elapsed_ms` 计算，不再混用 `mm/s`。
- P2 不增加滤波；若未来需要滤波，只能由一个明确 Middleware 模块实现并记录参数。

## 4. 最小目标接口

公共头文件只保留标准类型和 Encoder 自有类型，推荐契约：

```c
typedef enum {
    ENCODER_LEFT = 0,
    ENCODER_RIGHT,
    ENCODER_COUNT
} Encoder_Id;

typedef struct {
    int32_t total_pulses[ENCODER_COUNT];
    int32_t delta_pulses[ENCODER_COUNT];
    float speed_mps[ENCODER_COUNT];
} Encoder_Snapshot;

void Encoder_Init(void);
bool Encoder_Update(uint32_t elapsed_ms);
void Encoder_GetSnapshot(Encoder_Snapshot *out);
```

约束：

- `Encoder_Init()` 原子读取当前 raw totals 作为基线，首拍 delta/speed 必须为零。
- `Encoder_Update()` 只在 `elapsed_ms > 0` 时更新；使用调用者提供的真实间隔，不维护可被任意修改的 sample-period 全局参数。累计计数差值必须使用定义明确的无符号模运算处理回绕。
- `Encoder_GetSnapshot()` 在一个短临界区复制完整快照，调用者不能获得内部可写指针。
- PPR、轮周长、安装方向和必要校准值作为 Encoder 私有板级常量；修改时必须伴随测量依据。
- 不提供参数数组、通用 `SetParam()`、公开配置表或公开可写全局实例。

最终删除或迁出：

- `#include "driver/motor/motor.h"`。
- `g_tEncoderChannelCfg` 公共配置。
- `Encoder_CalcWheelTargets()`：迁到差速底盘 Middleware/Service。
- `Encoder_UpdateTravelDistance()`、Get/ResetTravelDistance：迁到里程/里程计 Middleware 或 Service。
- `Encoder_SetMiu()`、`Encoder_SetSamplePeriodMs()` 和通用参数枚举：没有真实运行时需求则删除。
- Motor 中的 `encoder_sign`、`encoder_delta`、`encoder_total`、`speed` 字段及 `Motor_GetSpeed()`：由 P2 调用点迁移一并清除，Motor 输出 API 的安全重写留给 P3。

## 5. 迁移步骤

### P2.1 冻结当前测量口径

1. 记录轮子离地时左右轮正转/反转对应的 raw totals 符号。
2. 确认编码器每圈脉冲数是电机轴、减速后输出轴还是四倍频后的计数。
3. 确认轮径/周长和当前 `miu` 的来源，不能把未知经验系数直接复制到新实现。
4. 用固定脉冲与 10 ms 间隔记录旧实现 speed 结果，作为行为对照，但不把错误双符号或错误单位固化为验收标准。

### P2.2 测试先行

先建立可注入假的 raw count snapshot 的主机测试，不在生产公共 API 中增加 test hook。覆盖：

- 初始化基线和首拍零增量。
- 左右轮正向、反向和静止。
- 相同脉冲在不同 `elapsed_ms` 下的速度换算。
- `int32_t` 计数回绕差值。
- 延迟调度后使用真实 elapsed，而不是固定周期误算速度。
- 双轮快照一致性和 `GetSnapshot()` 不暴露内部内存。
- PPR/轮周长私有常量对应的已知换算结果。

### P2.3 私有化 Encoder 状态

1. 新建模块私有 previous totals 和 `Encoder_Snapshot`。
2. Runtime/IRQ 输出左右原始计数的一致快照。
3. 两轮使用同一正交判向公式，Encoder 通道表只做一次安装方向修正。
4. delta 保持 `int32_t`；计算速度前显式转换，避免旧 `int16_t` 截断。
5. 不增加滤波、平均窗口或额外安全码。

### P2.4 迁移消费者

1. Service/当前最小调用适配层主动 `Encoder_Update(elapsed_ms)` 并取得 snapshot。
2. PID 接口改为由调用者按值传入 target 和 feedback；Middleware 不再包含 Motor/Encoder Driver，也不读取全局状态。
3. SpeedLoop、Task1 和遥测从 snapshot/Service 状态读取 `speed_mps`，不读 `g_tMotors[].speed`。
4. 删除 Encoder 对 Motor 的写入和 Motor 中编码器/PID 混合字段。
5. Task 直接调用 Driver 的存量问题不在 P2 扩大；能在最小范围内落到现有 `app/service/` 的采样编排必须优先落到 Service。

### P2.5 移出非 Driver 职责

1. 对 `Encoder_CalcWheelTargets()` 和里程 API 再搜索调用者；无调用者则直接删除。
2. 若有真实需求，将纯计算迁到 Middleware，输入输出都使用明确单位，不引用 Encoder Driver。
3. PID 输出限幅属于算法配置；Motor P3 仍必须独立执行硬件安全限幅，两者不得共享隐藏常量。

## 6. 允许与禁止的改动范围

允许：

- `driver/encoder` 及 P1 提供的 raw snapshot 接口。
- 为删除 `g_tMotors` 数据耦合所必需的 Motor 结构字段、PID 输入接口、Service 和直接消费者适配。
- Encoder 主机测试、依赖扫描和硬件记录。

禁止：

- 在 P2 重写 Motor PWM、方向、换向或制动实现；这些属于 P3。
- 新增滤波器、自动校准、复杂参数管理器或通用传感器框架。
- 同时在 Runtime 和 Encoder 修正车辆前进方向。
- 保留 `m/s` 与 `mm/s` 两套无类型区分的公开 API。
- 为兼容旧代码继续镜像写入 `g_tMotors`。
- 用固定 10 ms 代替真实 elapsed，同时允许调度延迟。

## 7. 验收标准

### 静态与主机验收

```text
rg 'driver/motor|g_tMotor|PID_T|middleware/' hc-team/driver/encoder
rg 'driver/' hc-team/middleware/pid
rg 'encoder_sign|encoder_delta|encoder_total|Motor_GetSpeed' hc-team/driver/motor
rg 'CalcWheelTargets|TravelDistance|SetSamplePeriodMs|SetMiu' hc-team
```

- 前三项必须为空；最后一项必须为空或只命中已明确迁移后的 Middleware/Service 名称。
- 单元测试覆盖上述边界，主机可测逻辑覆盖率达到 80% 以上。
- `rtk make -C Debug all` 和 clean build 均通过，无新增警告。

### 无调试器硬件验收

- 轮子离地、Motor P0 限制下，只使用手动慢速转轮或安全低功率测试。
- OLED/串口同时显示 raw total、corrected total、delta 和 speed_mps，确认方向修正恰好一次。
- 左轮正转、左轮反转、右轮正转、右轮反转分别验证，不得只测整车前进。
- 固定时间手转若干圈，累计脉冲与确认后的 PPR 对应。
- 改变采样间隔，速度结果按实际 elapsed 比例变化且无首拍跳变。
- 示波器/逻辑分析仪确认 AB 相边沿计数在正常转速下没有明显漏计。

## 8. 完成门槛

- Encoder 公共头不包含 Motor、PID、Runtime 硬件类型或公开配置表。
- Encoder 状态私有，调用者只能获得按值快照。
- 全链路只有一个方向修正点、一个脉冲到 `m/s` 换算点，不存在额外滤波。
- PID 只接收按值 target/feedback，不读取任何 Driver 全局。
- Motor 不再承载编码器状态或 PID 指针，为 P3 独立重写做好边界。
- 软件与硬件验收全部完成后，P2 才能标为 `done`。
