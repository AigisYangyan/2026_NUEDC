# P1：`mspm0_runtime` 拆分与边界修复计划

状态：in_progress（P1.1/P1.2 已完成；P1.3-P1.5 待执行）  
前置条件：Phase 1 构建基线通过；不得带负载运行 Motor。  
目标：把当前板级“总管模块”拆成小而明确的 Driver 能力，消除 Driver→App 和 ISR→上层控制流。

## 1. 三个设计问题

1. **抽象是什么？**
   - 板级一次性初始化。
   - 单调毫秒时钟。
   - 三个固定 UART 角色的非阻塞收发。
   - 共享 GPIO IRQ 中必须在 ISR 内完成的最小计数/事件采集。
2. **必须隐藏什么？**
   - SysConfig 宏、寄存器、IRQ 名称、DMA 通道、RX/TX 缓冲、head/tail、busy/done 标志和临界区。
3. **代码属于哪里？**
   - 板级启动与共享 IRQ 路由属于 Driver 板级模块。
   - 时钟、UART、Encoder、Key、Motor 必须逐步成为各自资源所有者。
   - Scheduler、协议解析、VOFA 变量写入和压测模式属于 App/Service，不得留在 runtime ISR。

## 2. 当前问题证据

- `mspm0_runtime.c:11` 包含 `app/scheduler/task_scheduler.h`，`SysTick_Handler()` 在约第 462 行调用 `TaskTimeSliceManage()`，构成 Driver→App 反向依赖。
- 公共头第 19-20 行定义上层回调类型，第 43-46 行暴露四个注册接口；DMA/UART ISR 随后直接调用这些回调。
- `sys_init.c:84-86` 注册 StepMotor、VOFA、Vision 上层函数，`uart_stress.c:268/282` 运行时替换消费者，存在切换窗口和在途字节归属问题。
- Runtime 只保存三个 DMA 单字节目标，真正 FIFO 分散在 StepMotor、Vision 和 Stress 的 App 文件中。
- `uart_vofa.c` 当前可能在 ISR 回调链中解析文本并修改绑定变量，ISR 职责过重。
- Runtime 同时处理 SysTick、三个 UART、DMA、Encoder、Key 和 Motor PWM 启动，硬件所有权不清。
- 大于 512 字节时退化为 `DL_UART_transmitDataBlocking()`，单字节接口同样阻塞且无超时。
- `sys_init.c` 直接包含 `ti_msp_dl_config.h`、调用 `SYSCFG_DL_init()`、NVIC 和 `__enable_irq()`，违反 App 禁止接触 DL HAL 的规则。
- UART2 生成配置为 921600 波特率 StepMotor 端口，但 `IMU.c` 也通过 StepMotor 发送 API 使用它；实际设备归属未确认。（2026-07-16 更正：三路 UART 已统一为 230400，见 `board.syscfg:226,260,293`；设备归属仍未确认，由 P5.T3 前置条件把关。）

## 3. 目标模块边界

不得把现有文件简单改名。目标拆分为以下能力，名称可按项目风格微调，但职责不得重新合并：

| 能力 | 最小接口 | 所有权 |
|---|---|---|
| Board | `Board_Init()`、`Board_EnableInterrupts()` | 唯一调用 SysConfig、NVIC 和全局中断控制 |
| Clock | `Clock_NowMs()` | SysTick ISR 只递增计数；App Scheduler 主动计算并消费时间差 |
| Vision UART | `VisionUart_Init()`、`VisionUart_Read()`、`VisionUart_GetOverflowCount()` | RX DMA/IRQ、私有 FIFO、溢出计数；当前无发送需求则不提供 TX |
| VOFA UART | `VofaUart_Init()`、`VofaUart_Read()`、`VofaUart_TryWrite()`、`VofaUart_GetOverflowCount()` | RX/TX DMA、私有 FIFO 和 busy 状态；解析留在任务上下文 |
| StepMotor UART | `StepmotorUart_Init()`、`StepmotorUart_Read()`、`StepmotorUart_TryWrite()`、`StepmotorUart_IsIdle()`、`StepmotorUart_ConsumeTxDone()`、`StepmotorUart_GetOverflowCount()` | RX/TX DMA、私有 FIFO 和完成事件 |
| Shared GPIO IRQ | `BoardGpio_GetEncoderRawSnapshot()`、`BoardGpio_ConsumeKeyEvents()` | IRQ 只维护板级原始计数和原始事件；Encoder/Key 主动拉取，不写其他模块私有状态 |

公共 API 只使用 `stdint.h`、`stdbool.h`、`stddef.h` 和模块自有类型。不得暴露 DMA channel、UART 寄存器、SysConfig 宏、缓冲区地址或上层回调类型。

接口契约固定为：

- 所有 `Init()` 必须在启用全局中断前调用一次，只初始化本模块自己的私有状态和硬件资源。
- `Board_Init()` 只初始化 Board 自有状态并执行 SysConfig 根初始化；不得直接写 Clock/UART/Encoder/Key/Motor 的私有状态。App System 负责按依赖顺序调用各 Driver 的公开 `Init()`。
- `Clock_NowMs()` 返回允许自然回绕的 `uint32_t` 单调毫秒计数；调用者必须用无符号差值计算 elapsed。
- `Read(uint8_t *out, size_t capacity)` 把最多 `capacity` 字节复制到调用者拥有的缓冲区并返回实际数量；无数据返回 0，`out == NULL` 或 `capacity == 0` 是调用前置条件错误，不为此增加细分状态码。
- `TryWrite(const uint8_t *data, size_t length)` 在复制到 Driver 私有 TX 缓冲并成功启动 DMA 后返回 true；未初始化、busy、空数据、空指针或超出容量统一返回 false，调用者仍拥有原缓冲区。
- `IsIdle()` 只报告当前无在途 TX；`ConsumeTxDone()` 原子读取并清除一次性完成事件。
- `GetOverflowCount()` 返回只增计数，不隐式清零。
- `BoardGpio_GetEncoderRawSnapshot(BoardEncoderRawSnapshot *out)` 在一个短临界区复制左右 `int32_t` 原始累计值；`out` 必须非空。
- `BoardGpio_ConsumeKeyEvents()` 原子读取并清除按键事件位图；位定义属于 Board GPIO 公共契约，不暴露寄存器位。

## 4. UART 拉取模型

每个 UART 必须拥有固定大小静态 RX 环形缓冲区：

- ISR 只读取/搬运字节、推进写索引、重装 DMA、清中断并更新 overflow/tx_done 标志。
- 上层通过 `Read()` 主动批量消费，不得在 ISR 中解析协议。
- FIFO 满时固定采用“保留旧数据、丢弃新字节、overflow 计数加一”，不得静默覆盖尚未消费的数据。
- `Read()` 在一个短临界区内取得可读范围并更新读索引，禁止用 `volatile` 代替一致性保护。
- TX 只允许长度在私有 DMA 缓冲容量内且当前不 busy 时启动；过长或 busy 立即返回 false，不得退化为无界阻塞发送。
- TX 完成只置标志；StepMotor 上层在 Service 调用中消费完成事件并启动下一帧。
- UART 压测只能通过 Service 显式执行“请求暂停正常消费者→非阻塞轮询 TX 空闲→清 FIFO/状态→独占→恢复”，不得替换 ISR 回调。等待上限固定为“当前波特率发送最长合法帧所需时间的两倍 + 一个 Service 周期”；超时必须取消切换、保持正常消费者有效并报告进入压测失败，不得无界等待或强抢端口。

缓冲容量不得凭感觉决定。实施前必须按波特率、最长调度间隔和最大连续帧计算，并把计算结果写进实现注释或计划日志。

## 5. 迁移步骤

### P1.1 时基去反向依赖

1. 先增加 Scheduler 基于 `Clock_NowMs()` 的主循环时间差推进测试，覆盖 tick 回绕。
2. SysTick ISR 只维护 `uint32_t` 毫秒计数，不再包含或调用 Scheduler。
3. App Scheduler 在主循环主动获取 elapsed ms，并按 elapsed 推进任务计数。
4. 搜索确认 Driver 中不存在 `TaskTimeSliceManage` 或任何 App 头文件。

### P1.2 板级初始化归位

1. `Board_Init()` 唯一调用 `SYSCFG_DL_init()` 并只初始化 Board 自有状态，保持上电输出为安全态；Clock/UART/Encoder/Key/Motor 分别由自己的 `Init()` 初始化。
2. `Board_EnableInterrupts()` 在所有 Driver 初始化完成后统一配置 NVIC 并开启全局中断。
3. `sys_init.c` 只编排 Driver/Middleware/App 初始化，不再包含 TI 头或直接调用 NVIC。
4. Motor PWM 的 compare 清零、计数器启动移交 Motor Driver；此处只做最小所有权搬迁，不提前重写 Motor 公共 API。

### P1.3 Vision 与 VOFA UART

1. 先用主机测试覆盖 FIFO wrap、空/满、overflow 和批量读取。
2. 迁移 Vision RX 到 Driver FIFO，Vision Service 主动读取并解析。
3. 迁移 VOFA RX/TX，确保解析和绑定变量更新只发生在任务上下文。
4. 删除对应 RX callback 注册、App FIFO 和 ISR 命名入口。

### P1.4 StepMotor UART

1. 执行前必须确认 UART2 实际连接对象，以及 IMU 使用 StepMotor API 是否为错误旧代码。
2. 明确 StepMotor 正常模式与 UART Stress 独占模式的状态转换和在途数据处理。
3. 迁移 RX FIFO、TX busy 和 tx_done 到 Driver，StepMotor Service 主动消费。
4. 删除 `SetStepmotorRxCallback`、`SetStepmotorTxCallback` 和回调替换流程。

### P1.5 共享 GPIO IRQ 与收口

1. 共享 GPIO ISR 只在板级 IRQ 模块内维护原始正交计数，左右轮使用同一种 AB 判向公式；Encoder 主动取得原始快照，物理前进方向修正只允许在 Encoder 模块保留一次。
2. 共享 GPIO ISR 只在板级 IRQ 模块内置原始按键事件，Key Driver 主动读取并去抖，不由 runtime 调用 `Key_NotifyIrq()`，也不直接写 Key 私有状态。
3. 删除无调用证据的 byte-send、Vision TX、Delay 等接口；确有调用者时先记录真实需求再保留最小接口。
4. 最终删除 `mspm0_runtime` 大一统公共接口；若仍需目录，只允许保留板级启动/共享 IRQ 的最小实现。

## 6. 允许与禁止的改动范围

允许：

- Runtime 及拆出的 Board/Clock/UART Driver 文件。
- 为适配拉取接口所必需的 `sys_init`、Scheduler、Vision/VOFA/StepMotor Service 和 Key/Encoder 直接调用点。
- 构建元数据中新增/删除这些源文件所需的最小修改。
- 相关主机测试和硬件冒烟记录。

禁止：

- 改变 StepMotor、Vision、VOFA 的协议格式和业务语义。
- 调整 PID、轨迹、菜单或任务功能。
- 在 UART2 归属未确认前猜测并合并 IMU/StepMotor 行为。
- 为兼容旧代码保留第二套 callback/FIFO 数据路径。
- 新增通用 HAL、动态内存或没有调用证据的扩展接口。

## 7. 测试先行与验收

### 静态/主机测试

- 旧代码上先让“Driver 不包含 App”“无注册回调”“ISR 不解析协议”检查失败。
- FIFO：空读、单字节、批量、wrap、刚好满、overflow、读写交错。
- Clock：首次调用、连续 elapsed、多毫秒滞后、`uint32_t` 回绕。
- UART：busy 拒绝、最大合法长度、超长拒绝、tx_done 只消费一次。
- Scheduler：1/5/10/20 ms 任务在主循环主动推进后保持原周期语义。

### 构建与依赖验收

执行前保存一次全仓库违规搜索作为存量 allowlist；执行后全仓库不得新增命中，并且 P1 新增/迁移的 Board、Clock、UART、共享 IRQ Driver 及其 App 调用点必须满足：

```text
rg '#include "app/|#include "middleware/' <P1_DRIVER_FILES>
rg 'Set.*Callback|TaskTimeSliceManage' <P1_DRIVER_FILES>
rg 'ti_msp_dl_config|ti/driverlib|NVIC_|__enable_irq' <P1_APP_CALLSITE_FILES>
rg 'transmitDataBlocking' <P1_DRIVER_FILES>
```

`<P1_DRIVER_FILES>` 与 `<P1_APP_CALLSITE_FILES>` 必须在执行日志中展开为实际文件列表，不得直接扫描未迁移模块后宣称 P1 失败。P1 文件内违规搜索必须为空；全仓库结果必须等于或少于基线 allowlist。随后执行 clean build、警告检查和 `git diff` 审查。

### 无调试器硬件验收

- 逻辑分析仪确认 UART1/2/3 实际波特率。每个已迁移 UART 使用带单调序号和校验值的最短帧、典型帧和最长合法帧，以配置波特率连续输入至少 60 秒或 10000 帧（取较大者）；通过条件为序号连续、校验全对、Driver overflow 为 0。
- 构造 FIFO overflow，串口/OLED 可观察 overflow 计数，系统不死锁。
- 验证 StepMotor TX 完成后由 Service 启动下一帧，而不是 ISR 递归发送。
- 连续运行 10 分钟，将 `Clock_NowMs()` 与外部计时基准比较；允许误差上限必须在测试前按“所选时钟源数据手册最差精度 × 600 秒 + 2 ms 测量分辨率”计算并记录。内部还必须满足累计 Scheduler elapsed 与 `Clock_NowMs()` 无符号差值完全一致，不允许丢 tick。
- 示波器确认 Board 初始化期间 Motor PWM 始终为零；不得接负载。

## 8. 完成门槛

- P1 新增和迁移的 Board/Clock/UART/共享 IRQ Driver 源码不包含 App/Middleware；其他 Driver 的存量违规继续由后续计划负责。
- Runtime/UART 不存在上层函数指针和注册回调。
- ISR 不执行协议解析、日志、调度或下一帧业务发送。
- SysConfig/NVIC 只在 Driver 板级模块出现。
- 三个 UART 的缓冲、DMA 和状态各有唯一 Driver 所有者。
- UART2 设备归属已记录；未确认则 P1 保持 `blocked`，不得删除旧链路后声称完成。
- 软件测试、clean build 和要求的硬件验收全部通过后，P1 才能标为 `done`。
