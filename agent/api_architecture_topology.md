# NUEDC API 平台架构拓扑图（2026_Diansai · MSPM0G3519）

最后复核：2026-07-17（P8：新单轴 IMU 驱动重写 + RX 接线；违规边 `IMU_API --> DL_HAL` 已关闭）  
适用工程：`2026_Diansai`（MSPM0G3519，LQFP-100，SDK 2.11.00.07；由旧工程 `NUEDC`/G3507 移植，见 `agent/MIGRATION_G3507_TO_G3519.md`）  
事实来源：当前工作区 `hc-team/**/*.c`、`hc-team/**/*.h`、仓库根 `board.syscfg`  
状态：当前实现拓扑，不是目标架构示意图  
维护规则：任何代码修改前必须先阅读本文件；修改完成后必须同步更新本文件和末尾日志。

## 1. 阅读规则

- Mermaid **类图**把每个模块的公共 API 打包成一个类；类之间的箭头表示源码依赖、调用或共享状态。
- Mermaid **逻辑图**表示初始化、调度和数据流。实线是当前正常调用，红色节点/连线说明违反根目录 `AGENTS.md` 的存量交叉依赖。
- 本图以当前代码为准。计划中尚未实现的 Board/Clock/UART 拉取接口不得提前画入当前图。
- 私有 `static` 函数不逐项列出，但其所属 `.c` 文件必须由对应模块类覆盖。
- API 新增、删除、改名、移动，模块新增/删除，依赖方向、资源所有权、数据处理位置或单位发生变化时，必须同步修改图。

## 2. Driver API 类图

```mermaid
classDiagram
direction LR

class DL_HAL {
  <<external>>
  +SYSCFG_DL_init()
  +DL_GPIO_xxx()
  +DL_TimerA_xxx()
  +DL_UART_xxx()
  +DL_DMA_xxx()
  +DL_I2C_xxx()
}

class Clock_API {
  <<driver:clock>>
  +Clock_Init()
  +Clock_NowMs()
}

class Board_API {
  <<driver:board>>
  +Board_Init()
  +Board_EnableInterrupts()
}

class BoardGpio_API {
  <<driver:board_gpio>>
  +BoardGpio_GetEncoderRawSnapshot()
  +BoardGpio_ConsumeKeyIrqEdges()
  +BoardGpio_GetKeyRawLevels()
}

class Runtime_API {
  <<driver:mspm0_runtime>>
  +Mspm0Runtime_InitUartDma()
  +Mspm0Runtime_DelayMs()
  +Mspm0Runtime_GetEncoderCounts()
  +Mspm0Runtime_ConsumeKeyIrqEdges()
}

class VisionUart_API {
  <<driver:board_uart>>
  +VisionUart_Init()
  +VisionUart_Read()
  +VisionUart_GetRxOverflowCount()
}

class VofaUart_API {
  <<driver:board_uart>>
  +VofaUart_Init()
  +VofaUart_Read()
  +VofaUart_TryWrite()
  +VofaUart_GetRxOverflowCount()
}

class StepmotorUart_API {
  <<driver:board_uart>>
  +StepmotorUart_Init()
  +StepmotorUart_Read()
  +StepmotorUart_TryWrite()
  +StepmotorUart_IsTxIdle()
  +StepmotorUart_ConsumeTxDone()
  +StepmotorUart_GetRxOverflowCount()
}

class ImuUart_API {
  <<driver:board_uart>>
  +ImuUart_Init()
  +ImuUart_TryWrite()
  +ImuUart_Read()
  +ImuUart_GetRxOverflowCount()
}

class Motor_API {
  <<driver:motor>>
  +Motor_Init()
  +Motor_SetOutput(id, output)
  +Motor_Update(elapsed_ms)
  +Motor_Brake(id)
  +Motor_BrakeAll()
}

class Encoder_API {
  <<driver:encoder>>
  +Encoder_Init()
  +Encoder_Update()
  +Encoder_GetSnapshot()
}

class Key_API {
  <<driver:key>>
  +Key_Init()
  +Key_Scan()
  +Key_IsPressed()
  +Key_GetPressEvent()
  +Key_PollPressEvent()
}

class OLED_API {
  <<driver:oled>>
  +OLED_Init()
  +OLED_Clear()
  +OLED_ShowChar()
  +OLED_ShowString()
  +OLED_Process()
  +OLED_IsReady()
}

class IMU_API {
  <<driver:imu>>
  +Imu_Init()
  +Imu_Update()
  +Imu_GetSnapshot(out)
  +Imu_GetDiag(out)
  +Imu_ZeroYaw()
  +Imu_SetOutputRate(rate)
}

class Emm42_API {
  <<driver:step_motor>>
  +Emm42_BuildEnableFrame()
  +Emm42_BuildReadSpeedFrame()
  +Emm42_BuildSpeedFrame()
  +Emm42_BuildPositionFrame()
  +Emm42_BuildSetZeroFrame()
  +Emm42_BuildStartHomingFrame()
  +Emm42_BuildExitHomingFrame()
  +Emm42_BuildPidConfigFrame()
}

class VofaDriver_API {
  <<driver:uart_vofa>>
  +vofa_init()
  +vofa_clear_profile()
  +vofa_register_float()
  +vofa_register_int()
  +vofa_bind_cmd()
  +vofa_run()
}

class PID_API {
  <<middleware>>
  +pid_Init()
  +pid_closeloop_motor(left_target_mps, right_target_mps, left_feedback_mps, right_feedback_mps, p_left_out, p_right_out)
}

Clock_API --> DL_HAL : SysTick
Board_API --> DL_HAL : SysConfig NVIC global IRQ
BoardGpio_API --> Runtime_API : transitional raw counts and key edge bitmap
Runtime_API --> Clock_API : bounded millisecond delay
Runtime_API --> DL_HAL : GPIO UART DMA
Runtime_API --> VisionUart_API : fixed UART RX dispatch
Runtime_API --> VofaUart_API : fixed RX DMA completion
Runtime_API --> StepmotorUart_API : fixed RX TX DMA dispatch
Runtime_API --> ImuUart_API : fixed UART_IMU IRQ RX dispatch, no DMA
Motor_API --> DL_HAL : GPIO and PWM via motor_hw.c
Encoder_API --> BoardGpio_API : raw snapshot
Key_API --> BoardGpio_API : pull raw key bitmap
OLED_API --> Clock_API : time
OLED_API --> DL_HAL : I2C_AUX exclusive
VisionUart_API --> DL_HAL : UART_VISION RX
VofaUart_API --> DL_HAL : UART_HOST_LINK = UART5 PA1/PA0 230400 RX DMA TX DMA
StepmotorUart_API --> DL_HAL : UART_STEPPER_BUS RX DMA TX DMA
ImuUart_API --> DL_HAL : UART_IMU = UART3 PA25/PA26 230400 IRQ RX polling TX
IMU_API --> ImuUart_API : 5-byte frame TX and RX drain
IMU_API --> Clock_API : frame freshness timestamp
IMU_API --> Runtime_API : bounded delay between device commands
VofaDriver_API --> VofaUart_API : UART transport
```

## 3. Middleware 与 App API 类图

```mermaid
classDiagram
direction LR

class System_API {
  <<app:system>>
  +main()
  +SysInit()
  +SysRun()
}

class Scheduler_API {
  <<app:scheduler>>
  +Sys_EnterRunEntry()
  +Sys_LeaveRunEntry()
  +Sys_GetActiveRunEntry()
  +TaskTimeSliceManage()
  +g_eSysFlagManage
}

class Clock_API { <<driver>> }
class Board_API { <<driver>> }
class BoardGpio_API { <<driver>> }

class RunRegistry_API {
  <<app:scheduler>>
  +RunRegistry_FindById()
  +RunRegistry_BuildMenuItems()
  +g_run_entries
}

class VofaRegister_API {
  <<app:scheduler>>
  +VofaRegister_Init()
  +VofaRegister_EnterProfile()
  +VofaRegister_ExitProfile()
  +VofaRegister_GetActiveProfile()
  +VofaRegister_GetXxxCtx()
}

class Menu_API {
  <<app:ui>>
  +Menu_Init()
  +Menu_HandleKey()
  +Menu_RenderIfDirty()
  +Menu_SetCurrentPage()
  +Menu_RequestRedraw()
  +Menu_IsDirty()
  +Menu_GetCurrentPage()
  +MenuPages_Init()
}

class TaskGroups_API {
  <<app:tasks>>
  +Task_UiService5ms()
  +Task_EncoderSpeedSample()
  +Task_MotorPidControl()
  +Task_StepmotorBusService5ms()
  +Task_VisionBusService5ms()
  +Task_VisionTrack10ms()
  +Task_VisionControl5ms()
  +Task_VisionTelemetry10ms()
  +Task_VofaService()
  +Task_SpeedLoop_Xxx()
  +Task_UartTest_Xxx()
  +Task_GrayTest_Xxx()
  +Task_UartStress_Xxx()
  +Task_Debug_Xxx()
  +Task_Task1_Xxx()
  +g_TaskGroups
}

class PID_API {
  <<middleware:pid>>
  +pid_Init()
  +pid_closeloop_angle()
  +pid_closeloop_motor(left_target_mps, right_target_mps, left_feedback_mps, right_feedback_mps, p_left_out, p_right_out)
  +pid_closeloop_track()
  +pid_formula_positional()
  +pid_formula_incremental()
  +pid_out_limit()
  +g_PID_instances
}

class Service_Layer {
  <<app:service>>
  +NO_ACTIVE_SOURCE_API
}

class SpeedLoop_API {
  <<app:task>>
  +SpeedLoop_Init()
  +SpeedLoop_Enter_Exit()
  +SpeedLoop_Sample10ms()
  +SpeedLoop_Control10ms()
  +SpeedLoop_Telemetry20ms()
}

class Task1_API {
  <<app:task>>
  +Task1_Init()
  +Task1_Enter_Exit()
  +Task1_Sample10ms()
  +Task1_Control10ms()
  +Task1_Telemetry20ms()
}

class TrackFollow_API {
  <<app:task>>
  +Track_UpdateSample()
  +Track_GetBitmap()
  +TrackN_To_BitmapString()
  +Calculate_Track_Error()
  +TrackN
}

class GrayTest_API {
  <<app:task>>
  +GrayTest_Init()
  +GrayTest_Enter_Exit()
  +GrayTest_SampleAndTelemetry10ms()
}

class UartTest_API {
  <<app:task>>
  +UartTest_Init()
  +UartTest_Enter_Exit()
  +UartTest_Telemetry10ms()
}

class UartStress_API {
  <<app:task>>
  +UartStress_Init()
  +UartStress_Enter_Exit()
  +UartStress_Tick5ms()
}

class VisionBus_API {
  <<app:task>>
  +VisionBus_Init()
  +VisionBus_Service5ms()
}

class VisionCoord_API {
  <<app:task>>
  +VisionCoord_Init()
  +VisionCoord_HandleFrame()
  +VisionCoord_GetLatest()
  +VisionCoord_GetState()
  +VisionCoord_GetStateUpdateMeta()
  +VisionCoord_GetTopic()
}

class StepMotorBus_API {
  <<app:task>>
  +StepmotorBus_Init()
  +StepmotorBus_Service5ms()
  +StepmotorBus_RequestBypass()
  +StepmotorBus_SetBypass()
  +StepmotorBus_SetControlGate()
  +StepmotorBus_ResetDiagCounters()
  +StepmotorBus_GetControlErrorCount()
  +StepmotorBus_GetLastReturnCode()
  +StepmotorBus_ClearControlFrames()
  +StepmotorBus_IsControlPathIdle()
  +StepmotorBus_GetLastSpeedRpm()
  +StepmotorBus_GetLastSpeedRaw()
}

class Platform2D_API {
  <<app:task>>
  +VisionHdl_Init()
  +VisionHdl_Enter_Exit()
  +VisionHdl_Run10ms()
  +VisionHdl_Control5ms()
  +VisionHdl_Telemetry10ms()
  +StepperTestX_Enter_Exit()
  +StepperTestY_Enter_Exit()
  +DebugSmooth_Xxx()
  +DebugVisionData_Xxx()
}

class Runtime_API { <<driver>> }
class VisionUart_API { <<driver>> }
class VofaUart_API { <<driver>> }
class StepmotorUart_API { <<driver>> }
class Motor_API { <<driver>> }
class Encoder_API { <<driver>> }
class Key_API { <<driver>> }
class OLED_API { <<driver>> }
class Emm42_API { <<driver>> }
class VofaDriver_API { <<driver>> }
class ImuUart_API { <<driver>> }
class DL_HAL { <<external>> }

System_API --> Scheduler_API : starts scheduler
System_API --> Board_API : board init and interrupt enable
System_API --> Clock_API : clock init
System_API --> Runtime_API : UART DMA init (transitional)
System_API --> Motor_API : init and stop
System_API --> Encoder_API : init
System_API --> Key_API : init
System_API --> OLED_API : init
System_API --> PID_API : init
System_API --> VofaRegister_API : init
System_API --> StepMotorBus_API : init
System_API --> VisionBus_API : init

Scheduler_API --> Clock_API : active elapsed query
Scheduler_API --> RunRegistry_API : resolve active entry
Scheduler_API --> TaskGroups_API : dispatch active group
RunRegistry_API --> SpeedLoop_API : lifecycle
RunRegistry_API --> GrayTest_API : lifecycle
RunRegistry_API --> UartTest_API : lifecycle
RunRegistry_API --> UartStress_API : lifecycle
RunRegistry_API --> Platform2D_API : lifecycle
RunRegistry_API --> Task1_API : lifecycle

Menu_API --> RunRegistry_API : builds pages
Menu_API --> Scheduler_API : enter and leave
Menu_API ..> Key_API : VIOLATION UI calls Driver
Menu_API ..> OLED_API : VIOLATION UI calls Driver

TaskGroups_API --> Menu_API : UI task
TaskGroups_API --> SpeedLoop_API
TaskGroups_API --> Task1_API
TaskGroups_API --> GrayTest_API
TaskGroups_API --> UartTest_API
TaskGroups_API --> UartStress_API
TaskGroups_API --> VisionBus_API
TaskGroups_API --> Platform2D_API
TaskGroups_API --> StepMotorBus_API
TaskGroups_API ..> PID_API : VIOLATION Task calls Middleware
TaskGroups_API ..> Motor_API : VIOLATION Task calls Driver
TaskGroups_API ..> Encoder_API : VIOLATION Task calls Driver
TaskGroups_API --> Clock_API : encoder elapsed time
TaskGroups_API ..> VofaDriver_API : VIOLATION Task calls Driver

SpeedLoop_API --> Encoder_API : sample and feedback
SpeedLoop_API --> PID_API : control
SpeedLoop_API --> Motor_API : output
SpeedLoop_API --> VofaRegister_API : telemetry context
SpeedLoop_API --> VofaDriver_API : telemetry send

Task1_API --> Clock_API : time
Task1_API --> Encoder_API : sample
Task1_API --> TrackFollow_API : track sample
Task1_API --> PID_API : control
Task1_API --> Motor_API : output
Task1_API --> VofaRegister_API
Task1_API --> VofaDriver_API

GrayTest_API --> TrackFollow_API
GrayTest_API --> VofaRegister_API
GrayTest_API --> VofaDriver_API
UartTest_API --> VofaRegister_API
UartTest_API --> VofaDriver_API
UartStress_API --> StepMotorBus_API : exclusive pause and resume
UartStress_API ..> StepmotorUart_API : VIOLATION Task calls Driver

VisionBus_API --> Clock_API : time
VisionBus_API --> VisionCoord_API : parsed frames
VisionBus_API --> VisionUart_API : pull buffered bytes
Platform2D_API --> VisionCoord_API : coordinates
Platform2D_API --> StepMotorBus_API : transport state
Platform2D_API --> Emm42_API : commands
Platform2D_API --> PID_API : axis control
Platform2D_API --> VofaRegister_API
Platform2D_API --> VofaDriver_API
StepMotorBus_API --> StepmotorUart_API : shared UART queue and ack
StepMotorBus_API --> Runtime_API : bounded wait helper
StepMotorBus_API --> Clock_API : time
StepMotorBus_API --> Emm42_API : frame packing
TrackFollow_API ..> DL_HAL : VIOLATION App reads GPIO
VisionCoord_API --> Clock_API : state update time

VofaRegister_API ..> VofaDriver_API : VIOLATION direct Driver profile registration
VofaRegister_API ..> PID_API : VIOLATION binds Middleware globals
VofaRegister_API ..> TrackFollow_API : VIOLATION exposes task state
Service_Layer ..> SpeedLoop_API : TARGET not implemented
```

## 4. 当前启动与调度逻辑图

```mermaid
flowchart TD
  Main[main.c main] --> SysInit[sys_init.c SysInit]
  SysInit --> BoardInit[Board_Init]
  SysInit --> ClockInit[Clock_Init]
  SysInit --> RuntimeInit[Runtime UART DMA fixed dispatch]
  SysInit --> DriverInit[OLED Key Motor Encoder VOFA BoardUart]
  SysInit --> MiddlewareInit[PID init]
  SysInit --> AppInit[Menu Run profiles Tasks Vision StepMotor]
  SysInit --> BoardIRQ[Board_EnableInterrupts]
  Main --> SysRun[SysRun]
  SysRun --> SchedulerLoop[TaskStartSchedule loop]

  SysTick[SysTick Handler] -->|1 ms| TickMs[s_tick_ms++]
  SchedulerLoop -->|query elapsed| TickMs
  SchedulerLoop -->|advance| TimeSlice[TaskTimeSliceManage]
  TimeSlice --> ActiveGroup[Resolve active TaskGroup]
  ActiveGroup --> TaskFlags[Run enabled task functions]
  TaskFlags --> UIGroup[UI group]
  TaskFlags --> FeatureGroups[Speed Gray UART Vision Task1 groups]

  RuntimeInit --> FixedDispatch[Runtime fixed IRQ DMA fanout]
  DMAIRQ[DMA and UART IRQ] --> FixedDispatch
  FixedDispatch --> RoleDrivers[VisionUart VofaUart StepmotorUart]

  classDef violation fill:#ffd6d6,stroke:#b00020,color:#700018
  class FixedDispatch violation
```

当前交叉点：App 仍有局部任务直接调用 Driver 或 DL HAL；Runtime ISR 已不再通过回调进入 App/VOFA 解析，但仍是过渡期固定分发层。

## 5. 关键数据流逻辑图

### 5.1 编码器、PID 与直流电机

```mermaid
flowchart LR
  EncGPIO[Encoder AB GPIO] --> QEI[TIMG8 TIMG9 硬件 QEI 计数器]
  QEI --> RawCount[Runtime 16 位模差扩展 int32 累计]
  RawCount --> BoardGpio[BoardGpio_GetEncoderRawSnapshot]
  BoardGpio --> Encoder[Encoder_Update elapsed_ms]
  Clock[Clock_NowMs] -->|elapsed ms| SampleOwner[TaskGroups unique encoder sampler]
  SampleOwner -->|real elapsed| Encoder
  Encoder -->|Encoder_Snapshot by value| Consumers[SpeedLoop and Task1]
  Consumers -->|targets and feedback mps by value| PID[pid_closeloop_motor]
  PID -->|left and right out pointers| Consumers
  Consumers --> MotorSet[Motor_SetOutput + Motor_Update slew/deadtime/timeout]
  MotorSet --> TB6612[TB6612 GPIO and PWM]
```

必须检查的数据处理：QEI 硬件判向（G3519 起编码器不再产生 GPIO 中断，GROUP1 仅服务按键）、`Mspm0Runtime_GetEncoderCounts()` 的 16→32 位模差扩展（前提：两次读数间位移 < 32767 计数）、Encoder `s_direction_sign` 全链路唯一方向修正点、速度 `m/s`、PID 输出限幅和 Motor 硬件限幅。任何修改都要证明没有重复反向、滤波、缩放或限幅。QEI 方向约定与旧版软件判向可能镜像，上板方向/PPR 校准由用户自理（`agent/MIGRATION_G3507_TO_G3519.md` §4.3）。

### 5.2 视觉与步进电机

```mermaid
flowchart LR
  VisionUART[Vision UART IRQ DMA] --> RuntimeVision[Runtime fixed IRQ dispatch]
  RuntimeVision --> VisionFifo[VisionUart private FIFO]
  VisionFifo --> VisionBus[VisionBus task-context parser]
  VisionBus --> VisionCoord[VisionCoord frame and state]
  VisionCoord --> Platform2D[VisionHdl control]
  Platform2D --> PIDAxis[PID axis output]
  PIDAxis --> Emm42[Emm42 command packing]
  Emm42 --> StepBus[StepMotorBus queue]
  StepBus --> StepTx[StepmotorUart private TX]
  StepTx --> RuntimeStep[Runtime fixed RX TX DMA dispatch]
  RuntimeStep --> Stepper[EMM42 hardware]
  Stepper --> RuntimeStep
  RuntimeStep --> StepRx[StepmotorUart private FIFO]
  StepRx --> StepBus

  classDef violation fill:#ffd6d6,stroke:#b00020,color:#700018
  class StepBus violation
```

### 5.3 按键、菜单、OLED 与 VOFA

```mermaid
flowchart LR
  KeyGPIO[Key GPIO IRQ] --> Group1IRQ[GROUP1 IRQ sets key edge bitmap]
  Group1IRQ --> BoardKey[BoardGpio edge and raw key pull]
  BoardKey --> KeyDriver[Key scan and events]
  KeyDriver --> Menu[Menu handle key]
  Menu --> Scheduler[Enter or leave RunEntry]
  Menu --> OLED[OLED render]

  Scheduler --> Task[Active task]
  Task --> VofaCtx[VofaRegister profile context]
  VofaCtx --> VofaDriver[vofa register and run]
  VofaDriver --> VofaUart[VofaUart FIFO and TX]
  VofaUart --> PC[VOFA PC]
  PC --> RuntimeRx[Runtime fixed VOFA RX dispatch]
  RuntimeRx --> VofaUart
  VofaUart --> VofaParse[vofa_run task-context parse and bind write]

  classDef violation fill:#ffd6d6,stroke:#b00020,color:#700018
  class VofaParse violation
```

## 6. 交叉依赖与风险登记

| ID | 当前交叉/风险 | 证据位置 | 违反规则 | 计划归属 |
|---|---|---|---|---|
| V01 | ~~Runtime SysTick 调用 App Scheduler~~ **closed 2026-07-13** | `driver/clock/clock.c` 拥有 SysTick；`task_scheduler.c` 主循环按 elapsed 推进 | Driver 不得反向调用 App | Phase 2 P1 |
| V02 | **closed 2026-07-16（P5 R02/R06）**：Runtime UART callback 表与 `Set*Callback`/`Send*`/`Busy` 接口已删除，IRQ/DMA 改为固定分发到 `board_uart` 角色 Driver | `mspm0_runtime.h/.c`、`sys_init.c`、`driver/board_uart/*`；R02 零命中，R06 clean 构建与 map 复核 | Driver 不得调用上层注册回调 | Phase 2 P5 |
| V03 | App 直接调用 SysConfig、NVIC、DL HAL **partially closed 2026-07-16（P5 R03）** | `sys_init.c` 已改为调用 `Board_Init()`/`Board_EnableInterrupts()`；`tasks/platform_2d/vision_bus.c`、`tasks/platform_2d/stepmotor_bus.c`、`tasks/uart_stress/uart_stress.c` 已清零；`tasks/track_follow/track_follow.c` 仍直接调用 `__enable_irq` 或包含 `ti/driverlib` | App 不得包含 DL HAL | Phase 2 P1 及后续模块 |
| V04 | **closed 2026-07-16（P3.T2 E04/E05）**：Motor 头不再包含 pid.h，`Motor_T`/`p_pid`/`g_tMotors` 已删除 | 依赖扫描零命中；`motor.h` 仅标准类型与自有枚举 | Driver 不应暴露 Middleware 内部对象 | Phase 2 P3 |
| V05 | **closed 2026-07-16（P2F E04/E05）**：Encoder 不再写 `g_tMotors`，deprecated API 与公开参数表已删除 | `Encoder_Update()`/`Encoder_GetSnapshot()`；依赖扫描零命中 | 模块不得修改其他模块全局 | Phase 2 P2-FIX |
| V06 | **closed 2026-07-16（P3.T2 E04）**：`encoder_sign` 随 `Motor_T` 删除；Runtime 正交判向 + Encoder `s_direction_sign` 单点修正 | 依赖扫描零命中 | 同一数据处理必须只有一个所有者 | Phase 2 P2-FIX/P3 |
| V07 | TaskGroups/SpeedLoop/Task1 直接编排 Driver 与 PID | 对应 App task 文件 | Task 应只调 Service | Driver 完成后迁入 Service |
| V08 | **closed 2026-07-16（P5 R04）**：Emm42 Driver 改为纯协议组包，`extern` App transport 已删除 | `emm42.c`、`stepmotor_bus.c`；R04 零命中 | Driver 不得依赖 App 符号 | Phase 2 P5 |
| V09 | **closed 2026-07-16（P5 R02）**：VOFA RX 仅在 `vofa_run()` 任务上下文解析，ISR 链只搬运到 `VofaUart` FIFO | `uart_vofa.c`、`driver/board_uart/vofa_uart.c`；R02 零命中且 `vofa_rx_isr` 删除 | ISR 只允许最小搬运/置位 | Phase 2 P5 |
| V10 | Service 目录当前没有有效源 API | `hc-team/app/service/` | 缺少 Driver 与 Middleware 的业务桥 | Driver API 稳定后补齐 |
| V11 | **closed 2026-07-16（P3.T3 E09）**：左右 PWM 统一为 80 MHz/period 7999（10 kHz），compare 按同一 period 换算，比例一致 | `board.syscfg` 单源 + 生成配置 `CLK_FREQ=80000000`/`period=7999` ×2 + `motor_hw.c` 单一常量 | 电机硬件安全与单位口径不一致 | Phase 2 P3.T3 |
| V12 | **closed 2026-07-16（P3.T1/T2 E01）**：`Motor_Update` 状态机实现 slew 限速、换向过零+5ms 死区、100ms 命令超时归零，主机 7 项测试覆盖 | `motor.c` 状态机 + `tests/host/test_motor.c` | 电机保护缺失 | Phase 2 P3 |
| V13 | Scheduler、PID、TrackFollow 暴露可写全局状态 | `g_eSysFlagManage`、`g_PID_instances`、`TrackN` | 模块状态必须私有，禁止跨模块直接写 | 对应模块重写时关闭 |
| V14 | UI 直接调用 Key/OLED Driver，并在 UI 头暴露 Key 类型 | `menu_core.*`、`menu_pages.c` | UI 应通过 App 接口/Service，不直接操作 Driver | App Service/UI 阶段 |
| V15 | VOFA Scheduler 直接依赖 VOFA Driver、PID 和 TrackFollow | `vofa_register.*` | Scheduler 不应成为跨层共享状态中心 | VOFA Service 阶段 |
| V17 | **closed 2026-07-16（P6 R02/R04）**：EEPROM 器件删除，I2C_AUX 只剩 `driver/oled/oled_hardware_i2c.c` 独占；未引入多余 I2C 总线层 | `driver/eeprom/` 删除；`rg -l 'I2C_AUX' hc-team` 仅命中 OLED driver `.c` | 多器件共享总线却无所有者；单器件独占时禁止过度抽象 | Phase 2 P6 |
| P1-SCOPE | P1 完成范围收窄：UART 角色迁移交 P5、按键共享 IRQ 交 P4；P1F.T1 仅关闭 Runtime 死接口与时间包装 | `plan1_fix_runtime_closeout.md` §1；P1F E01–E05 | 无新增违规；避免跨计划重复关闭 | P1-FIX / P4 / P5 |
| V16 | **closed 2026-07-16（HT.T1 E01/E04）**：主机测试套件 `tests/host/` 已从旧 `NUEDC` 仓库迁入当前仓库，可在本仓库复跑 32 项基线（Encoder 14 + PID 5 + Motor 7 + Key 6） | `tests/host/` 7 个源文件；`rtk make -C tests/host all` 全绿；`git ls-files tests/host` 恰好 7 个文件且无 `.exe` | 测试是交付内容；验收协议依赖主机测试基线 | HT.T1 done，P5 前置满足 |

登记表只允许基于代码证据新增或关闭。修复完成后不要直接删除记录：先把状态改为“closed + 日期 + 验证”，下一次阶段收口时再归档。

## 7. 源文件覆盖清单

| 层 | 模块/API 包 | 覆盖文件 |
|---|---|---|
| Driver | Clock | `driver/clock/clock.c/.h` |
| Driver | Board | `driver/board/board.c/.h` |
| Driver | Board GPIO | `driver/board_gpio/board_gpio.c/.h` |
| Driver | Runtime | `driver/mspm0_runtime/mspm0_runtime.c/.h` |
| Driver | Board UART Roles | `driver/board_uart/vision_uart.c/.h`、`driver/board_uart/vofa_uart.c/.h`、`driver/board_uart/stepmotor_uart.c/.h`、`driver/board_uart/imu_uart.c/.h` |
| Driver | Motor | `driver/motor/motor.c/.h`（纯状态机，无 TI 头）、`motor_hw.c/.h`（唯一 TI 头位置） |
| Driver | Encoder | `driver/encoder/encoder.c/.h` |
| Driver | Key | `driver/key/key.c/.h` |
| Driver | OLED | `driver/oled/oled_hardware_i2c.c/.h`（公共面仅 Init/Clear/ShowChar/ShowString/Process/IsReady；`oledfont.h` 仅 driver 私有字模） |
| Driver | IMU | `driver/imu/imu.c/.h`（2026-07-17 P8 重写：器件更换为内置 Kalman 解算的单轴模组，5 字节定长帧，只出 Yaw 与 GyroZ；旧 `IMU.c/.h`(0x7E 九轴协议)已删除。RX 已接线，仍零外部调用者 —— 待 Service 层消费；MPU6050/I2C_IMU 已于 2026-07-16 移除） |
| Driver | EMM42 | `driver/step_motor/emm42.c/.h` |
| Driver | VOFA UART | `driver/uart_vofa/uart_vofa.c/.h` |
| Middleware | PID | `middleware/pid/pid.c/.h` |
| App System | Main/Init | `app/system/main.c`、`sys_init.c` |
| App Scheduler | Scheduler/Registry/VOFA | `app/scheduler/task_scheduler.*`、`run_registry.*`、`vofa_register.*` |
| App UI | Menu | `app/ui/oled/menu_core.*`、`menu_pages.*` |
| App Tasks | Task groups | `app/tasks/task_groups.c/.h` |
| App Tasks | Speed loop | `app/tasks/speed_loop/speed_loop.c/.h` |
| App Tasks | Task1 | `app/tasks/task1/task1.c/.h` |
| App Tasks | Track/Gray | `app/tasks/track_follow/*`、`gray_test/*` |
| App Tasks | UART tests | `app/tasks/uart_test/*`、`uart_stress/*` |
| App Tasks | Platform 2D | `app/tasks/platform_2d/2DPlatform_LaserStrike.*`、`stepmotor_bus.*`、`vision_bus.*`、`vision_coord.*` |
| App Service | 预留层 | `app/service/` 当前无有效 `.c/.h` API |
| Utils | 空目录 | `hc-team/utils/` 当前无有效 `.c/.h` API |

新增源文件时必须先确定它属于哪个 API 包；无法归类时停止编码，不得把文件塞进 `utils` 或任意 task 目录。

## 8. 每次代码执行前检查

1. 在类图中找到将修改的 API 包及所有入边、出边。
2. 在逻辑图中从数据源追到最终执行器，阅读上下游 API 的实际实现。
3. 检查是否新增 Driver→App、Middleware→Driver、Task→Driver/Middleware 或循环依赖。
4. 检查单位、方向、滤波、校准、限幅和状态是否已有所有者。
5. 涉及 Motor 时检查 V11/V12 与 P0 闸门；未关闭前禁止带载执行。
6. 若图与代码不一致，先修正本图再继续设计。

## 9. 每次代码执行后更新

1. 更新顶部“最后复核”日期。
2. 更新类图中的公共 API；删除的 API 必须同时从图中删除。
3. 更新真实依赖箭头和数据流，不得只更新目标架构。
4. 更新交叉依赖登记：新增、保持或关闭，并附验证证据。
5. 更新源文件覆盖清单。
6. 在下方日志新增一行。即使拓扑没有变化，也必须记录“已复核，无拓扑变化”。
7. 验证 Mermaid 代码块闭合、节点名称唯一，文档能正常渲染。

## 10. 更新日志

| 日期 | 代码/文档变更 | 拓扑更新 | 复核结果 |
|---|---|---|---|
| 2026-07-13 | 创建 Phase 2 Driver 计划并首次建立全仓库 API 拓扑；同步 `AGENTS.md` 强制维护流程 | 建立 Driver/App 类图、启动/调度图、三条数据链和 V01-V15 登记 | 当前存在多处存量交叉；Motor P0 闸门有效 |
| 2026-07-13 | P1 复审第二轮：memcpy 显式模映射消除所有 implementation-defined 转换，Makefile RM 容错，拓扑删除虚设依赖/更新关闭项，P2.1 状态修正 | 删除 BoardGpio_API→Encoder_API 虚设箭头；修正 5.1 交叉点文本移除已关闭 V01；Encoder_API 新增 u32_mod_i32/i64_narrow_i32 内部帮助函数 | 13 项主机测试通过（-ftrapv），固件编译通过；P2.1 硬件口径仍待确认 |
| 2026-07-13 | P1 复审修复：编码器 int64_t 回绕防溢出、Runtime 原子快照、clock.h 注释修正、Makefile/测试加固、基线文档口径更新 | Encoder_API 新增 Encoder_Update/Encoder_GetSnapshot；数据流标注 PRIMASK 原子读取；无新增交叉依赖 | 13 项主机测试全部通过（含 -ftrapv），固件编译无 warning/error |
| 2026-07-13 | Makefile clean：Windows 分支改用 cmd.exe /D /C if exist del，每文件独立命令，无 `-` 吞错 | 无拓扑变更 | Windows 实测通过：`make clean` 返回 0，`test_encoder.exe` 与 `test_encoder` 均不存在 |
| 2026-07-13 | 新增 REASONIX 嵌入式重写项目级 Skills：规划、施工、独立验收，并固化常见 AI 失败模式与施工者 Prompt | 仅新增 `.agents/skills/reasonix-embedded-*`，不改变代码 API 拓扑 | 三个 Skill 均通过 `quick_validate.py`；路径断言脚本成功/失败路径通过；前向测试能限制任务范围并拒绝无后置条件证据的完成声明 |
| 2026-07-13 | 将流程明确为 Codex 计划 → REASONIX 施工 → REASONIX 独立自检 → Codex 最终验收；新增 `reasonix-embedded-self-check` 和统一证据行/状态标签 | 仅更新 `.agents/skills/reasonix-embedded-*`，不改变代码 API 拓扑 | 四个 Skill 均通过 `quick_validate.py`；CLEAN-1 前向测试按 E01 完成生成、存在断言、clean、缺失断言并输出 `SELF_CHECK_PASS` |
| 2026-07-16 | Codex 建立 P3/P4/P5 施工计划与验收契约（`plan3_motor_rewrite.md`、`plan4_key_rewrite.md`、`plan5_uart_role_drivers.md`），逐条核实 V02/V03/V04/V05/V06/V08/V09/V11/V12 证据行号 | 已复核，无拓扑变化；计划中的目标接口（Motor 新 API、BoardGpio 按键位图、board_uart 三角色）未提前画入当前图 | 证据核实与当前图一致；各违规项状态保持 open，关闭须凭对应计划的 E 行证据 |
| 2026-07-16 | REASONIX `SELF_CHECK_BLOCKED`（P1/P2 前置不成立 + StepMotor 921600×5ms 吞吐缺口）；Codex 建立收口契约 `plan1_fix_runtime_closeout.md`、`plan2_fix_encoder_closeout.md`，修订 plan3/4/5 前置条件与 P5 §0.1 吞吐契约（drain-until-empty、StepMotor 命令-应答流量模型），核实 runtime 死接口与 deprecated Encoder API 调用点 | 已复核，无拓扑变化；P1 范围收窄裁定（P1.3/P1.4→P5、P1.5.2→P4）登记于 plan1_fix §1，拓扑边待各 E 行验收后更新 | 阻塞裁定成立；全部违规项保持 open；施工顺序固化为 P1F.T1→P2F.T1→(P3/P4)→P5 |
| 2026-07-16 | FIX-BAUD：用户将三路 UART 统一为 230400（`board.syscfg:226,260,293`）；Codex 同步 6 文件 7 处代码注释与 plan1/plan5/phase1 文档中的旧波特率说明（`plan_fix_baud230400_comments.md`），P5 §0.1 吞吐数字按 230400 重算（线速缺口消除，drain 裁定不变） | 已复核，无拓扑变化；纯注释/文档同步，API 与依赖边不变 | `rg '921600\|115200\|460800' hc-team` 零命中；`rtk make -C Debug all` 退出码 0（SysConfig 按新波特率重新生成）；230400 实测归 P5 硬件行 E14 |
| 2026-07-16 | P1F.T1：删除 Runtime 8 个零调用接口与 `GetTickMs`/`InitTick` 包装；12 个调用点改为 `Clock_NowMs()` | Runtime_API 精确保留仍有调用者的 UART/Delay 接口；OLED、MPU6050、IMU、Task1、StepMotorBus、VisionBus、VisionCoord 时间边改指向 Clock；登记 P1 范围移交 | E01/E02 全仓零命中；E03 Host 13 项通过；E04 clean 固件构建退出 0、编译诊断 0、map 零命中；E05 对照施工前后状态确认本任务仅触及允许源文件与强制拓扑，既有 WIP 未归因于本任务；E06/E07 属 P1F.T2，仍开放 |
| 2026-07-16 | P2F.T1：PID 双轮入口改为目标/反馈按值输入、双输出指针；Encoder 删除 Motor 兼容写入、公开参数表与 10 个 deprecated API；TaskGroups 唯一采样所有者计算真实 elapsed，SpeedLoop/Task1 消费快照并显式输出 Motor | 删除 PID→Motor、Encoder→Motor 边；更新 Encoder/PID API 与 5.1 数据流；V05 关闭、V04 Middleware 侧关闭、V06 软件所有权收口 | RED 为六参调用遭旧两参声明拒绝；独立审查发现并修复首拍 elapsed/基线错位；E01 正则误报已透明更正；Host 19 项通过，E03–E05 零命中，clean 固件构建退出 0；硬件 E07–E09 仍开放 |
| 2026-07-16 | Codex 验收 P1F.T1+P2F.T1：独立复跑 E 行扫描（全零命中）与 Host 19 项测试，聚焦审读 PID 契约/采样所有者/重入修复，裁定 `CODEX_SOFTWARE_ACCEPTED`，提交 `455a968` | 已复核，无新增拓扑变化（施工时已同步） | 通过；P1F.T2/P2F.T2 硬件行开放 |
| 2026-07-16 | 用户裁定简化流程：撤销 REASONIX 自检阶段，删除 `reasonix-embedded-self-check` skill；execute/accept/plan 三个 skill 改为“施工行级报告 + Codex 精简验收”，E 行预算 ≤6/任务、单次构建证据复用；五份计划状态标签同步为 `CONSTRUCTION_*`/`CODEX_*`；plan3 消除“禁斜坡”与 AGENTS.md §8.1 的冲突，改为 Motor 私有 slew limit | 已复核，无代码 API 拓扑变化 | `rg 'SELF_CHECK' .agents` 仅余历史记述；硬件门与 Motor P0 不受简化影响 |
| 2026-07-16 | 用户裁定取消硬件验收（软件验收唯一制）：五份计划硬件行作废（P1F.T2/P2F.T2/P4.T3 removed，plan3 T3 改为 syscfg PWM 统一纯软件任务，P5 E14–E16 降为用户自测参考，UART2 归属改为用户书面确认）；skills 同步（验收结论收敛为 `CODEX_ACCEPTED`/`CODEX_REJECTED`，电机软件安全设计保留为主机可证验收项）；P1F.T1/P2F.T1 升格 `CODEX_ACCEPTED`，P1/P2 标 done | 已复核，无代码 API 拓扑变化 | 软件行即终局；板上实测由用户自理 |
| 2026-07-16 | P3.T1 验收 `CODEX_ACCEPTED`：新文件 `motor_new.c/.h`+`motor_hw.h`+主机测试实现带 slew/换向死区/命令超时的 Motor 状态机（占位常量 slew=100‰/ms、deadtime=5ms、timeout=100ms，待 T3/用户确定），不进固件构建 | 无拓扑变化（新符号在 T2 并入 `Motor_API` 时更新类图与覆盖清单） | Codex 复跑：负面扫描零命中，Host 26 项（Encoder14+PID5+Motor7）全绿；观察项：`pending_direction` 冗余记账，T2 并入时删除 |
| 2026-07-16 | 用户重命名 syscfg 全部外设组并新增 `UART_IMU`（物理 UART3，230400）；Codex 基线同步 16 文件宏名并按角色核对 DMA 通道（步进 TX CH2→CH3、VOFA RX CH5→CH2），修复 clean 构建失败（提交 `5131f6e`） | 类图/数据流按角色命名不受影响；`UART_IMU` 专用端口使 IMU 与步进共享发送链问题获得配置级解法（P5.T3 契约待修订） | `rtk make -C Debug all` 退出 0；宏名旧引用全仓零命中 |
| 2026-07-16 | P3.T2 验收 `CODEX_ACCEPTED`：`motor_new` 并入 `motor.c`+`motor_hw.c`，全部消费者改用 `Motor_SetOutput`/`Motor_Update`，`Motor_T`/`g_tMotors`/`Motor_SetPwm`/`Motor_GetSpeed`/`encoder_sign` 全删；`pending_direction` 冗余项已清 | `Motor_API` 更新为新 5 API；删除 `Motor_API→PID_API` 边；V04/V06/V12 closed、V11 partially closed（频率统一归 T3）；覆盖清单加 `motor_hw.c/.h`；5.1 数据流更新 | Codex 复跑：E04–E06 扫描零命中，Host 26 项全绿，E07 clean 构建退出 0、map 含 `Motor_Update`（构建阻塞由 syscfg 改名引起，基线修复后完成，非施工缺陷）；观察项：`Motor_Update` 使用名义周期常量而非实测 elapsed，slew 在调度抖动下偏保守，可接受 |
| 2026-07-16 | P4.T1：Key Driver 改为 `BoardGpio` 拉取边沿/电平位图，`key.c` 去除 TI 头依赖，新增主机按键测试 | `BoardGpio_API` 新增 `ConsumeKeyIrqEdges/GetKeyRawLevels`；`Key_API` 删除 `Key_NotifyIrq`；删除 `Runtime_API ..> Key_API` 与 `Key_API --> DL_HAL`，新增 `Key_API --> BoardGpio_API`；5.3 数据流改为 `GROUP1 IRQ -> BoardGpio -> Key_Scan` | E01 Key 6 项主机测试通过且 `hc-team/driver/key` 对 TI 头扫描零命中；E02 Host 全套 32 项通过，无 Encoder/PID/Motor 回归 |
| 2026-07-16 | P3.T3 验收 `CODEX_ACCEPTED`：syscfg 单源统一左右驱动 PWM 为 10 kHz（80 MHz/8000），`motor_hw.c` 收敛为单一 period 常量；P3 整体 done | V11 closed（生成配置双通道 `CLK_FREQ=80000000`、`period=7999` 为证据） | Codex 复核生成值与 diff；构建退出 0、0 警告 |
| 2026-07-16 | P4.T1/T2 验收 `CODEX_ACCEPTED`：`Key_NotifyIrq` 符号全仓零命中（T2 目标随 T1 完成）；runtime GROUP1 ISR 只置私有边沿位图，原子读清经 `BoardGpio` 拉取；Codex 将 `Mspm0Runtime_ConsumeKeyIrqEdges` 的裸 `extern` 声明归位到 `mspm0_runtime.h`；P4 整体 done | `Runtime_API` 新增 `+Mspm0Runtime_ConsumeKeyIrqEdges()`（经 BoardGpio 消费）；其余同上行 | Codex 复跑：E03/E04 扫描零命中、Host 32 项全绿、固件构建 0 警告退出 0 |
| 2026-07-16 | P5 统一施工（Vision→VOFA→StepMotor/EMM42/UartStress/IMU）：新增 `board_uart` 四角色 Driver，Runtime 删除全部 UART 回调/Send/Busy 接口，Vision/VOFA/StepMotor RX 全部改为任务态 drain-and-parse，IMU 改走 `UART_IMU` 最小 TX 角色 | 顶部复核日期改为 P5；Driver/App 类图新增 `VisionUart_API`/`VofaUart_API`/`StepmotorUart_API`/`ImuUart_API`，删除 Runtime callback/VOFA ISR/Emm42 extern 依赖；5.2/5.3/启动图改为固定分发 + 私有 FIFO；V02/V08/V09 closed，V03 partially closed | 以 R01-R06 为准：Host 全套通过、负面扫描零命中、clean 固件构建退出 0 且 map 含四个新角色符号 |
| 2026-07-16 | 调试串口迁移（用户裁定，Codex 自施工自验收）：`UART_HOST_LINK`(VOFA) 由 UART0/PA10/PA11 迁至 **UART5/PA1(TX)/PA0(RX)/230400/DMA**；PA10/PA11 收归 **`UART_BSL_ENTRY` = UART0/9600/无 DMA/无中断**，专供无线 BSL 烧录。仅改 `board.syscfg`，生成物由 SysConfig 产出 | `VofaUart_API --> DL_HAL` 边标注更新为 UART5 PA1/PA0；顶部复核日期更新。**无新增/关闭违规**（配置迁移，非违规修复） | R01 Host 76 项全绿；R02 clean 固件构建 exit 0 且 0 warning；R03 `UART_HOST_LINK_INST=UART5`、IRQ 改指 `UART5_IRQHandler`；R04 `UART_BSL_ENTRY_INST=UART0` @9600 且引脚与 SDK BSL 示例逐字吻合；**R05 `git status hc-team tests` 为空——驱动零改动，证实 UART 实例号为 Driver 以下私有事实**；R06 DMA 触发改指 UART5 且 UART0 无 NVIC。**已知缺口**：`UART_BSL_ENTRY` 暂无消费者（ENTRY 字节 0x22 监听器未实现，属独立特性）；PA0/PA1 板上尚未引出，待硬件组新画 |
| 2026-07-16 | P6：删除 `driver/eeprom/at24cxx.*` 死代码，OLED 公共头收口为 6 个显示能力接口，I2C 等待上限按 400 kHz + 80 MHz 算式替代 `50000u`，新增主机 OLED 测试 | 顶部复核日期改为 P6；Driver 类图删除 `EEPROM_API` 与其 I2C 边，`OLED_API` 收敛为 6 个公共接口并标注 `I2C_AUX` 独占；新增并关闭 V17；覆盖清单删除 EEPROM 行并更新 OLED 行 | 以 P6 R01-R06 为准：Host 76 项通过，EEPROM/旧 OLED 公共符号零命中，clean 固件构建退出 0，map 含 `OLED_ShowString`/`OLED_IsReady` 且不含 `at24cxx_*`/`oled_pow` |
| 2026-07-16 | plan5 修订 4：P5.T3 的 IMU 处置改为迁移到最小 `imu_uart` TX 角色（`ImuUart_Init/TryWrite`，UART_IMU 无 DMA）；"UART2 归属确认"前置作废；Codex 核实 IMU 模块零外部调用者（休眠代码），禁止推测性 RX FIFO（归 P7） | 已复核，无代码 API 拓扑变化（imu_uart 为批准的未来接口，不提前画入） | `rg -c 'IMU_UART_\|IMU_Update_Yaw\|IMU_Get_Reset\|IMU_Calibrate' hc-team --glob '!hc-team/driver/imu/*'` 零命中；P5.T1–T3 全部可派工 |
| 2026-07-16 | G3507→G3519 迁移后拓扑本地适配（仅文档同步，未改代码）：工程移入 `2026_Diansai`（MSPM0G3519/LQFP-100，SDK 2.11.00.07，配置源为仓库根 `board.syscfg`）；编码器改 TIMG8/TIMG9 硬件 QEI（PA7/PA6、PA3/PA2），GROUP1 仅服务按键；步进总线物理实例 UART2→UART7（PB15/PB16 不变）；MPU6050/I2C_IMU 已移除（提交 `37ff7fc`）；灰度 8 路升级 12 路（提交 `c60f4eb`，`TRACK_SENSOR_COUNT=12`） | 删除 MPU6050_API 类、System/Task1→MPU6050 边与覆盖清单行；5.1 数据流改为 QEI 硬件计数；事实来源路径改为根 `board.syscfg`；登记 V16（`tests/host` 未迁入） | 对照 `hc-team` 源码与 `agent/MIGRATION_G3507_TO_G3519.md` 复核：`rg 'MPU6050' hc-team` 仅余 task1.c 一条移除说明注释；公共 API（Encoder/BoardGpio/Runtime）与依赖边未变 |
| 2026-07-16 | 计划目录整理（仅文档，未改代码）：phase2 已验收计划（P1/P1F/P2/P2F/P3/P4/FIX-BAUD 共 7 份）移入 `done/`，作废的 GROUP1 判向基线移入 `obsolete/`；新建 `agent/README.md`（目录导航 + 项目脉络）与 HT.T1 派工契约 `plan_host_tests_restore.md`（tests/host 迁入恢复 32 项基线，P5 前置） | 已复核，无代码 API 拓扑变化；V16 计划归属更新为 HT.T1 | 工作区 `git status` 中 `hc-team` 零改动；phase2 顶层只余索引与两份待派工计划 |
| 2026-07-16 | HT.T1 验收：从 `../NUEDC/tests/host/` 迁入 7 个主机测试源文件，恢复 `tests/host` 32 项基线（Encoder 14 + PID 5 + Motor 7 + Key 6）；`.gitignore` 补齐主机测试可执行产物忽略；`hc-team` 与 `board.syscfg` 保持零改动 | V16 closed；tests 不进入类图；无新增 API 边 | `rtk make -C tests/host all` 全绿；`rg -n 'ti_msp_dl_config|ti/driverlib' tests/host` 零命中；`git ls-files tests/host` 恰好 7 个文件；`git status --porcelain hc-team board.syscfg` 为空；`rtk make -C Debug all` 通过 |
| 2026-07-16 | 流程自闭环（用户裁定，仅文档/skills，未改代码）：取消第二个施工者，三个 `reasonix-embedded-*` skill 合并为 `.agents/skills/embedded-closed-loop`（决策→施工→验收由单 agent 全占），删除 3 份 GPT 承包商注册 `openai.yaml` 与施工者派工 Prompt；4 份 reference 经 `git mv` 保留历史。代偿机制：**契约含全部 E 行须在写生产代码前先提交**，验收比对带时间戳的冻结契约，由 git 充当消失的第二方；E 行改错须单独提交说明。标签 `CODEX_*` 退役为 `ACCEPTED`/`REJECTED`。顺带回退 CCS 自动写入 `.cproject` 的失效索引项（指向已删 `docs/pin_table_v2`）与重复 Debug 构建配置块 | 已复核，无代码 API 拓扑变化 | `git grep -ri "reasonix|codex" .agents` 零命中；`git status --porcelain hc-team board.syscfg tests Debug` 为空；日志表 REASONIX 历史记述按惯例不回改。教训登记：本次 `sed -i` 曾静默剥掉全文 687 行 CRLF，被 `--ignore-all-space` 与 `cat -A` 发现并回退，改用 Edit 工具重做 |
| 2026-07-16 | QEI/灰度引脚重映射（`plan_qei_gray_pinmux.md`，自闭环施工+验收）：编码器 4 脚中 3 个是核心板晶振/振荡器脚（PA3=SYSCTL.LFXIN、PA6=SYSCTL.HFXOUT、PA2=SYSCTL.ROSC，经 TI 器件数据核实），核心板为现成模块、晶振实焊，固件不可绕过 —— 采纳硬件组方案：`QEI_LEFT` 迁 **TIMG9/PB7/PB9**、`QEI_RIGHT` 迁 **TIMG8/PB10/PB11**；12 路灰度让出 PB7/PB9/PB10/PB11，IN5/IN10/IN11 迁 PB20/PB14/PB0，**IN4 迁 PB8 而非引脚表建议的 PA7**（唯一偏离：PA7 跨端口会消灭 `GPIO_LINE_SENSOR_PORT` 组级单端口宏并破坏 12 路原子采样；PB8 是 PB7 物理邻脚）。释放 PA2/PA3/PA6/PA7。仅改 `board.syscfg` | **无 API 边变化**：5.1 数据流 `TIMG8 TIMG9 硬件 QEI 计数器` 不含引脚与左右归属，迁移后仍为真。左右轮与 timer 的绑定互换由 syscfg `$name` 吸收（QEI_LEFT/RIGHT 继续与物理轮子绑定），**无新增/关闭违规** | E01 clean 固件构建 exit 0 且 0 warning；E02 `QEI_LEFT_INST=TIMG9`/`QEI_RIGHT_INST=TIMG8`；E03 `GPIO_LINE_SENSOR_PORT=(GPIOB)` 仍存在（12 路同端口未破）；**E04 `git status hc-team tests` 为空 —— 驱动零改动，再次证实外设实例号为 Driver 以下私有事实**；E05 主机 76 项全绿 0 FAIL；E06 引脚表与 `board.syscfg` 逐行交叉核对 0 冲突（原 11 行 Q4 冲突全消），原表 44 脚零丢失、只增 PB8，并删除 2 行与陀螺仪共用 PA25/PA26 的「作废」重复记录。**遗留风险：`encoder.c:41` `s_direction_sign[]={-1,1}` 按旧板 AB 极性标定，新板须实测重标（禁止新增第二个反转开关）；核心板晶振实焊情况待硬件组书面确认** |
| 2026-07-17 | P8/P8B 收官：两份已验收契约扫入 `agent/phase2_driver_rewrite/done/`（9 份）；README 目录地图改为真实状态。**纯文档，零代码改动** | **已复核，无拓扑变化**（§14.4）。巡查副产物（登记，未施工）：`plan5`/`plan6`/`plan_debug_uart_remap`/`plan_qei_gray_pinmux`/`plan_p7` 五份状态均为 `(CODEX_)ACCEPTED` 却仍在根目录；`plan_host_tests_restore.md` 状态行写 `pending` 但 HT.T1 实为已验收（V16 closed/`d57b728`）；`plan_pin_table_v2_migration.md` 状态行写 `BLOCKED` 但已被 `plan_qei_gray_pinmux.md` 取代 —— **后两者是陈旧状态行，非真实冲突**，未擅自改写 | `git status --untracked-files=all` 复核：代码路径下 143 个条目全部为未跟踪的参考资料，已暂存 **0**。**施工事故（记录在案）**：首次提交时 `git add -A` 误将用户施工期间放入的 `hc-team/12路灰度传感器检测20240331（STM32F103C8T6）/`（约 80 文件，含 STM32 固件库/PDF/JPG）整个扫入，且提交信息谎称「纯文档，零代码改动」。**根因是量具错误第四次**：核实用的 `git status --untracked-files=no` 恰好排除了未跟踪文件 —— **而要查的对象正是未跟踪的**。已 `reset --soft` 撤销后以 `--untracked-files=all` 重新核实。**教训：核实「有没有多余东西被提交」时，口径必须能看见未跟踪文件** |
| 2026-07-17 | P8B：IMU 链路提速 **230400 + 500 Hz**（契约 `plan_p8b_imu_230400_500hz.md`，冻结于 `92e11f5`）。`board.syscfg` 的 `UART_IMU` 115200→230400；`imu.h` 枚举**末尾追加** `IMU_OUTPUT_RATE_500_HZ`（RRATE 0x0D）；`imu_uart.c` 的 TX 有界轮询超时按 230400 重算、FIFO 注释按 500 Hz 重算 | `ImuUart_API --> DL_HAL` 边波特率标注 115200→230400。无新增/删除 API，无依赖方向变化 | E01 clean 构建 exit 0/诊断 0；E02 主机 **101 PASS / 0 FAIL**（98 基线 + 3 新增）；E03 `UART_IMU_BAUD_RATE`=230400；E04 `imu_uart.c` 中 `115200` **0 命中**；E05 未暴露 1000 Hz（**0 命中**）；E06 生成物 diff 仅波特率行，`QEI|PWM_DRIVE` 漂移 **0**。**裁定理由经审查后更正入账**：用户「底盘 500Hz 更精准」不成立（速率与精度在本器件解耦，内部采样 50kHz、Kalman 常驻，精度指标各档相同；且 100Hz 控制环自身滞后 10ms 已远大于 5ms↔2ms 之差）。**真实依据是云台前馈延迟**：180°/s 转弯时 200Hz 的 5ms 数据龄=0.90° 指向误差，为器件自身 0.2° 精度的 4.5 倍；500Hz 压至 0.36° 同量级；1000Hz 的 0.18° 跌破噪声底故不暴露 | 
| 2026-07-17 | 用户裁定入规范：**App 上层将整体重置，当前只做 Driver 层下层接口，不管上层调用者**。`AGENTS.md` 新增 §15（不重排既有编号 —— §8.1/§14 等被 11 处文档引用）；新建严格计划表 `agent/phase2_driver_rewrite/plan_driver_first_order.md`；新增 `docs/IMU陀螺仪配置指南.md`（面向厂商上位机的配置动作）。**纯文档，零代码改动** | **已复核，无拓扑变化**（§14.4）。巡查副产物（登记，未施工）：`driver/gray/` **不存在**，12 路灰度采样在 `app/tasks/track_follow/track_follow.c:26,61` 直接 `#include ti_msp_dl_config.h` 并调 `DL_GPIO_readPins` —— 这是 **V03 至今 partially closed 而非 closed 的唯一残留点**，且处于用户暂缓裁定下 | `git diff --stat` 仅 4 个文档文件；`hc-team/**` 与 `board.syscfg` 零触碰 |
| 2026-07-17 | P8：新单轴 IMU 驱动重写（契约 `plan_p8_imu_rewrite.md`，冻结于 `f0ef8f0`）。**器件已更换**：旧 0x7E 九轴协议 → 新 5 字节定长帧单轴模组（内置 Kalman 解算，只出 Yaw 与 GyroZ）。删除 `IMU.c/.h`(616 行)，新增 `imu.c/.h`；`imu_uart` 补 RX FIFO；`mspm0_runtime` 新增 `UART_IMU_INST_IRQHandler`（唯一不走 DMA 的接收角色）；`board.syscfg` 的 `UART_IMU` 230400→115200 且开启外设级 RX 中断 | `IMU_API` 类改为 6 个新接口；**违规边 `IMU_API --> DL_HAL : exposed TI header` 关闭**（E02 实测 IMU 层零 TI 依赖）；新增边 `Runtime_API --> ImuUart_API`、`IMU_API --> Runtime_API`；`ImuUart_API` 补 `Read`/`GetRxOverflowCount`；覆盖清单 IMU 行改写 | E01 clean 固件构建退出 0、诊断 0（`imu.o` 实测重编、`.out` 实测重链，非空转）；E02 `hc-team/driver/imu/` 中 `ti_msp_dl_config|DL_|delay_cycles` **0 命中**；E03 旧器件 API 全工程 0 命中，`git ls-files` 施工前 2 → 施工后 0；E04 主机 **98 PASS / 0 FAIL**（76 基线 + 22 新增 IMU 用例）；E05 `UART_IMU_BAUD_RATE=115200` 且 `SYSCFG_DL_UART_IMU_init()` 内 `DL_UART_MAIN_INTERRUPT_RX` 命中 1；E06 变更文件全在 allowed_files。**契约修订 2 次且均单独提交**：E05 符号名（`27e50d7`）、E03 模式自始不可满足（`93bf75f`）。**教训：证据行的模式必须在冻结前先对现有代码树跑一遍**（量具错误第三次）；**`core.ignorecase=true`，证明文件删除只能用 `git ls-files`，`ls` 会假阴性** |
| 2026-07-17 | P7：巡查推翻原范围 —— Step Motor 已于 P5 拆完（`emm42.c` 已是纯组包），IMU 无外部调用者且 RX 未接线（`mspm0_runtime.c` 中 IMU 字样 0 处）。**IMU 部分经用户 2026-07-17 裁定推迟**（IMU 与 12 路灰度后续要改，避免与重写冲突），已施工的 IMU 改动全部回退，本次仅交付 emm42 残渣清理（契约 `plan_p7_imu_stepmotor.md`，冻结于 `16e0c96`） | 类图去 `Emm42_RunCommandTask()`。**违规边 `IMU_API --> DL_HAL : exposed TI header` 保持 open** —— IMU 未施工，该边仍是真实现状 | E01/E03/E05/E06 全过（构建 0 告警、emm42 残渣 0 命中、主机 76 PASS）。巡查副产物（未修，随 IMU 一并推迟）：`IMU.c` 三处延时按 32MHz 计算而 `CPUCLK=80MHz`，实际时长仅标称 40%；因 IMU 是死代码从未暴露。**教训：`git show HEAD:` 输出的是已归一化的 blob，不能用来判断工作区行尾，只能用 `cat -A` 看工作区文件本身。** |
