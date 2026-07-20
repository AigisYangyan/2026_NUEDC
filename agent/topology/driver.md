# 拓扑分层文件：Driver API 类图（§2）

本文件是 `agent/api_architecture_topology.md`（拓扑索引，唯一入口）的分层部分，只承载 §2。
阅读规则（§1）、数据流（§5）、风险登记（§6）、覆盖清单（§7）、执行前后检查（§8/§9）与更新日志（§10）都在索引文件。
章节编号沿用原单文件，不重排 —— `§2` 锚点被 AGENTS.md、agent 定义与历史冻结契约引用。
维护义务与索引文件一体生效：Driver API 增删改必须同步本图，并在索引文件 §10 追加日志（AGENTS.md §14）。

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
  +VisionUart_TryWrite()
  +VisionUart_IsTxIdle()
  +VisionUart_ConsumeTxDone()
  +VisionUart_GetRxOverflowCount()
}

class UartVision_API {
  <<driver:uart_vision>>
  +UartVision_Init()
  +UartVision_Poll()
  +UartVision_GetLatestCoord(out) bool
  +UartVision_GetCoordSeq() uint32_t
  +UartVision_SendTopic(main, sub) bool
  +UartVision_GetTopicAck(main*, sub*) bool
  +UartVision_GetTopicAckSeq() uint32_t
  note: 唯一编解码所有者——坐标帧 0xAA55+len+payload+CRC16-MODBUS RX，选题帧 0xFF+main+sub+0xFE 双向
  note: 坐标 float32 原样透传，不做单位/极性变换；时效判定归上层（只给单调 seq，不持墙钟）
}

class VofaUart_API {
  <<driver:board_uart>>
  +VofaUart_Init()
  +VofaUart_Read()
  +VofaUart_TryWrite()
  +VofaUart_GetRxOverflowCount()
  +VofaUart_GetTxOverflowCount()
  note: T-VTXQ1 (2026-07-20) — software TX byte-ring FIFO symmetric to RX FIFO; TryWrite busy semantics changed from drop-on-busy to enqueue-on-busy (rejects only when ring would overflow, counts tx overflow)
  note: VofaUart_IsrTxDone now clears busy AND kicks next DMA segment (vofa_uart_tx_kick, private), was clear-only; TX buffer/in-flight/ISR pump sole-owned by vofa_uart.c, no new callback
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

class Gray_API {
  <<driver>>
  +GRAY_CHANNEL_COUNT = 12
  +GRAY_BITMAP_MASK
  +Gray_ReadDarkBitmap() uint16_t
  note: 深色=1 为器件事实（比较器输出），非本层约定
  note: 无 Init（SysConfig 已配输入）、无状态、无去抖、无诊断
  note: 不声明左右 —— 位序矛盾未决，须实测
}

class GrayPort_API {
  <<driver>>
  +gray_port_read() uint32_t
  +gray_port_channel_mask(channel) uint32_t
  note: HAL 边界，非公共 API；主机测试由 fake_gray_port.c 顶替
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
  +Emm42_BuildQPosPresetFrame()
  +Emm42_BuildQPosFrame()
  +Emm42_BuildMultiCmdFrame()
  +Emm42_BuildClearPositionFrame()
}

class BslEntry_API {
  <<driver:bsl_entry>>
  +BslEntry_IsrOnByte(byte)
  +BslEntry_InvokeBsl()
  note: BslEntry_InvokeBsl 擦 SRAM 后复位进官方 ROM BSL，永不返回
  note: ISR 内直接触发跳转 —— 对 V09 的显式豁免（契约冻结，跳转即复位无返回栈无共享态竞争）
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
  <<middleware:pid>>
  +Pid_Init(Pid_T*, const Pid_Config_T*)
  +Pid_UpdateIncremental(Pid_T*, target, feedback) float
  +Pid_UpdatePositional(Pid_T*, target, feedback) float
}

class ParamStore_API {
  <<driver:param_store, NEW W5>>
  +PARAM_STORE_MAX_PAYLOAD = 48
  +ParamStore_Read(buf, len) bool
  +ParamStore_Save(const buf, len) bool
  note: payload-agnostic — magic+format-version+len+CRC16 framing only; caller (param_tune) self-holds payload semantic version as first byte
  note: erase-before-write, read-back verify; no retry / no wear-leveling (single sector, SAVE is rare, simplicity-first)
  note: internal param_store_port.h seam (仿 gray_port.h) + param_store_hw.c (sole DL_FlashCTL touch point, last 1KB sector 0x0007FC00) not shown as separate node — port pattern identical to Gray_API/GrayPort_API
}

Clock_API --> DL_HAL : SysTick
Board_API --> DL_HAL : SysConfig NVIC global IRQ
BoardGpio_API --> Runtime_API : transitional raw counts and key edge bitmap
BoardGpio_API --> DL_HAL : DL_GPIO_readPins for key raw levels
Runtime_API --> Clock_API : bounded millisecond delay
Runtime_API --> DL_HAL : GPIO UART DMA
Runtime_API --> VisionUart_API : fixed RX TX DMA dispatch, VISION_TX IRQ clear + VisionUart_IsrTxDone
Runtime_API --> VofaUart_API : fixed RX TX DMA dispatch (RX push + VofaUart_IsrTxDone on VOFA_TX complete; pre-existing edge, label corrected 2026-07-20 — was RX-only, TX dispatch already wired)
Runtime_API --> StepmotorUart_API : fixed RX TX DMA dispatch
Runtime_API --> ImuUart_API : fixed UART_IMU IRQ RX dispatch, no DMA
Runtime_API --> BslEntry_API : fixed UART_BSL_ENTRY IRQ RX dispatch, no DMA, ISR calls BslEntry_IsrOnByte
Motor_API --> DL_HAL : GPIO and PWM via motor_hw.c
Encoder_API --> BoardGpio_API : raw snapshot
Key_API --> BoardGpio_API : pull raw key bitmap
OLED_API --> Clock_API : time
OLED_API --> DL_HAL : I2C_AUX exclusive
VisionUart_API --> DL_HAL : UART_VISION RX TX DMA
UartVision_API --> VisionUart_API : task-context drain and TryWrite, Driver-Driver same layer controlled
VofaUart_API --> DL_HAL : UART_HOST_LINK = UART5 PA1/PA0 230400 RX DMA TX DMA
StepmotorUart_API --> DL_HAL : UART_STEPPER_BUS = UART7 PB15/PB16 256000 RX DMA TX DMA
ImuUart_API --> DL_HAL : UART_IMU = UART3 PA25/PA26 230400 IRQ RX polling TX
IMU_API --> ImuUart_API : 5-byte frame TX and RX drain
IMU_API --> Clock_API : frame freshness timestamp
IMU_API --> Runtime_API : bounded delay between device commands
VofaDriver_API --> VofaUart_API : UART transport
BslEntry_API --> DL_HAL : invokeBSLAsm inline asm, target-only, SRAM erase + RESETLEVEL/RESETCMD
```

