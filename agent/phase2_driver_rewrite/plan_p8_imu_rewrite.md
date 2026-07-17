# 计划：P8 —— 新单轴 IMU 驱动重写（面向小车底盘）

状态：`pending`
日期：2026-07-17
流程：单 agent 自闭环（`.agents/skills/embedded-closed-loop`）。**本契约在写任何生产代码前提交**，
验收比对本文冻结的 E 行。

## 0. 用户裁定（2026-07-17）

P7 的「IMU 暂不编写」裁定**已由用户解除**：用户更换了 IMU 器件，要求围绕**小车底盘**方向重写驱动。
器件参考资料位于 `hc-team/IMU_NEW_EXAMPLE/`（示例工程 + `数据手册.pdf`）。

用户在本次派工中当场裁定的三项：

| 议题 | 裁定 |
|---|---|
| 波特率 | **改 `board.syscfg` 为 115200**（原 230400 是旧 IMU 遗留） |
| 输出速率 RRATE | 用户当前未知/未动过 → 驱动提供 `Imu_SetOutputRate()` 但**不在 Init 自动调用**；实际帧率由用户上板实测 |
| 旧 `IMU.c`/`IMU.h` | **删除**，新驱动另起 |

## 0.1 契约修订：E05 符号名写错（2026-07-17，本修订单独提交）

**修订前**：E05 断言 `SYSCFG_DL_UART_IMU_init()` 体内出现 `DL_UART_INTERRUPT_RX`。

**为什么是错的**：SysConfig 对 Main 型 UART 生成的实际符号是 `DL_UART_MAIN_INTERRUPT_RX`。
`DL_UART_INTERRUPT_RX` **不是** `DL_UART_MAIN_INTERRUPT_RX` 的子串（前者在 `DL_UART_` 后紧跟
`INTERRUPT`，后者中间隔着 `MAIN_`），故照原样执行 E05 会得到 0 命中 —— 一条永远无法满足的行。
这是 DECIDE 阶段凭 `mspm0_runtime.c` 里 `DL_UART_INTERRUPT_RX`（**读**中断状态用的枚举）
推断了 `SYSCFG_*_init()` 里**使能**中断用的符号，属「从命名推断行为」，正是 SKILL §Phase1.1
明令禁止的。

**修订后**：断言 `DL_UART_MAIN_INTERRUPT_RX` 命中 1。

**断言强度未被削弱**：E05 要证明的是「RX 中断由 syscfg 单源开启，而非在 C 代码里手写」，
修订只更正符号名，该命题原样保留。本修订**先于**满足它的代码提交单独提交。

**新旧 IMU 是两颗不同的器件，协议无任何交集。** 旧器件：0x7E/0x23 帧头、九轴、14 个功能码。
新器件：单轴，**内置 Kalman 解算**，直接输出解算后的 Z 轴航向角与角速度。

### 1.1 读格式（器件 → MCU），5 字节定长

```text
0x5A | TYPE | DATAL | DATAH | SUM
```

- `TYPE=0xAA` 角速度：`Wz = (int16)((DATAH<<8)|DATAL) / 32768 * 2000` °/s
- `TYPE=0xBB` 航向角：`Yaw = (int16)((DATAH<<8)|DATAL) / 32768 * 180` °
- `SUM = (0x5A + TYPE + DATAL + DATAH) & 0xFF`

### 1.2 写格式（MCU → 器件），5 字节定长，**无校验和**

```text
0x55 | 0xAA | ADDR | DATAL | DATAH
```

| 动作 | 字节序列 | 备注 |
|---|---|---|
| 解锁 KEY | `55 AA 13 8E 5F` | 所有写操作前必须先发 |
| 保存 SAVE | `55 AA 00 00 00` | 所有写操作后必须发 |
| Z 轴归零 | `55 AA 15 00 00` | 解锁 →100ms→ 归零 →100ms→ 保存 |
| 输出速率 RRATE | `55 AA 02 <code> 00` | `ADDR=0x02`；10Hz=0x06(出厂) / 50Hz=0x08 / 100Hz=0x09 / 200Hz=0x0B |

### 1.3 datasheet 已查实的自相矛盾（施工必须绕开，不得据其猜测）

| # | 矛盾 | 处置 |
|---|---|---|
| A | 波特率：模组参数表写「默认 115200」，BAUD 寄存器表写「默认值 0x0002」(=9600) | 取 **115200** —— 示例工程 `empty.syscfg:188` `UART1.targetBaudRate = 115200` 是能跑通的实证 |
| B | BAUD 表中 `0x0006` 被同时标注为 115200 与 230400 | **230400 的寄存器码在手册中无可靠记载** → 本次不提供改波特率接口，不做试错 |
| C | 陀螺量程：参数指标表写 ±400°/s，协议换算写 `/32768*2000` | 取 **2000**（协议 + 示例代码一致）。若实物量程真为 ±400，raw 不会超过 6554，换算不受影响 |
| D | BIAS_CAL 状态回包 `5A CC 00 00 96`，按其自身 SUM 规则应为 `0x26` | **不实现该状态回读** —— 校验规则不自洽，无法可靠解析 |
| E | KEY 寄存器：正文写「写入 0x8E5F」，但字节序列 `13 8E 5F` 按 L-first 应解为 0x5F8E | 按**字节序列**原样发送（示例工程实证可用），不按寄存器语义重建 |

## 2. 三个设计问题

- **抽象是什么？** 底盘需要的是「当前航向角 + 航向角速度 + 这份数据有多旧」。
  不是帧格式、不是校验和、不是 `/32768*2000`、不是寄存器地址。
- **什么必须隐藏？** 0x5A 帧格式与解析状态、0x55AA 寄存器写序列、校验和、量程换算系数、
  RRATE 寄存器编码、RX 环形缓冲区、UART 实例。
- **代码属于哪一层？** Driver（`hc-team/driver/imu/`）。字节端口归 `driver/board_uart/imu_uart`
  （既有边界，本次补 RX）；UART3 IRQ 归 `driver/mspm0_runtime`（既有边界）。

依赖方向：`Service(未来) -> Imu -> ImuUart -> DL HAL`，以及 `Imu -> Clock/Mspm0Runtime`（同层受控）。

## 3. 关键设计裁定

### 3.1 禁止二次解算（AGENTS.md §8.2 单一所有者）

**器件内部已完成 Kalman 解算与积分。** 因此本 Driver **不得**再做：航向角积分、角速度滤波、
方向反转、单位再换算。旧 `IMU.c` 的 `IMU_Update_Yaw_Integration()` / `IMU_Get_User_Yaw()`
是对**外部裸陀螺**的积分，对本器件属重复处理，**随旧文件一并删除，不迁移**。

### 3.2 航向角不做 unwrap（分层裁定）

器件输出 `[-180,180)` 的 wrapped 航向角。底盘偏航闭环需要的「连续多圈航向」是**数据处理**，
按 AGENTS.md §3.3 属 Middleware/Service，不属 Driver。本次 Driver 只如实上报器件输出。
（示例工程 `control.c:86` 的做法是把**误差**归一化到 [-180,180]，同样不需要 Driver 做 unwrap。）

### 3.3 新鲜度是底盘安全项，不是防御代码

AGENTS.md §8.1：「周期控制必须检测命令或反馈是否过期；超时后必须停止输出」。
故障模型具体且真实：**IMU 线脱落 / 模块死机 → 航向角冻结 → 偏航闭环持续朝一个方向纠偏 → 车原地打转**。
故快照必须带 `age_ms` + `valid`，由未来 Service 据此停机。这是 §8.3 允许的「有真实故障模型 + 明确处理动作」的检查。

**只做一个 `age_ms`**（距最近一帧任意有效数据）。分别跟踪 yaw/rate 两个 age 解决的是我描述不出的故障模型 → 不做。

### 3.4 不实现陀螺零偏校准（BIAS_CAL）

器件支持 `55 AA 0A 01 00` + 20s 静止 + SAVE。**本次不实现**：
底盘当前需求（读航向 + 归零 + 提速率）不依赖它；它是一次性台架动作，可用厂家上位机完成；
且它是 21 秒阻塞调用 —— 示例工程自己也把 `performCaliBias()` 注释掉了。
按 AGENTS.md §8.3「最小方案」不引入。需要时再单独派工。

### 3.5 `Imu_ZeroYaw` / `Imu_SetOutputRate` 不进 `Init()`

两者都以 `SAVE` 结尾，**写器件内部 flash**。放进 `Init()` = 每次上电写一次 flash。
`Init()` 只重置解析状态；何时归零是底盘策略（跑第一圈前），归 Service。

## 4. 最小接口（先定契约，后实现）

```c
/* hc-team/driver/imu/imu.h */
typedef enum {                 /* 值与器件寄存器编码无关，编码在 .c 内部映射 */
    IMU_OUTPUT_RATE_10_HZ = 0,
    IMU_OUTPUT_RATE_50_HZ,
    IMU_OUTPUT_RATE_100_HZ,
    IMU_OUTPUT_RATE_200_HZ,
} Imu_OutputRate_t;

typedef struct {
    float    yaw_deg;       /* [-180,180)，器件已解算 */
    float    yaw_rate_dps;  /* °/s */
    uint32_t age_ms;        /* 距最近一帧有效数据；valid==false 时为 0 */
    bool     valid;         /* 是否收到过至少一帧 */
} Imu_Snapshot_t;

typedef struct {
    uint32_t frame_count;           /* 通过校验的帧数 —— 上层据此实测输出速率 */
    uint32_t checksum_error_count;  /* 帧头+类型对但校验和错 —— 波特率错时会暴涨 */
    uint32_t rx_overflow_count;     /* 端口 FIFO 溢出字节数 */
} Imu_Diag_t;

void Imu_Init(void);                        /* 重置解析状态与计数；不碰器件 */
void Imu_Update(void);                      /* 任务态：排空端口 FIFO 并解析 */
void Imu_GetSnapshot(Imu_Snapshot_t *out);  /* 任务态 */
void Imu_GetDiag(Imu_Diag_t *out);
bool Imu_ZeroYaw(void);                     /* 阻塞 ~200ms，写器件 flash */
bool Imu_SetOutputRate(Imu_OutputRate_t r); /* 阻塞 ~200ms，写器件 flash */
```

`ImuUart` 补 RX：`ImuUart_Read()` / `ImuUart_GetRxOverflowCount()` / `ImuUart_IsrPushByte()`。

**并发所有权**：ISR 只把字节推进 `ImuUart` 私有 FIFO（AGENTS.md §5）；`Imu_Update()` 与
`Imu_GetSnapshot()` 同为任务态，故快照本身无需临界区。FIFO 读取由 `ImuUart` 内部 PRIMASK 保护。

## 5. 范围

- `allowed_files`（逐一列出，无 glob）：
  - `agent/phase2_driver_rewrite/plan_p8_imu_rewrite.md`
  - `agent/api_architecture_topology.md`
  - `agent/README.md`
  - `board.syscfg`
  - `Debug/makefile`
  - `.gitignore`
  - `hc-team/driver/imu/IMU.c`（删除）
  - `hc-team/driver/imu/IMU.h`（删除）
  - `hc-team/driver/imu/imu.c`（新增）
  - `hc-team/driver/imu/imu.h`（新增）
  - `hc-team/driver/board_uart/imu_uart.c`
  - `hc-team/driver/board_uart/imu_uart.h`
  - `hc-team/driver/mspm0_runtime/mspm0_runtime.c`
  - `hc-team/app/system/sys_init.c`
  - `tests/host/Makefile`
  - `tests/host/test_imu.c`（新增）
  - `tests/host/fake_uart_port.c`
- `forbidden_files`：`hc-team/driver/encoder/encoder.c`、`hc-team/driver/motor/motor.c`、
  `hc-team/driver/motor/motor_hw.c`、`hc-team/driver/step_motor/emm42.c`、
  `hc-team/middleware/pid/pid.c`、`hc-team/app/tasks/`（全部）、`hc-team/app/ui/`（全部）、
  `hc-team/app/scheduler/`（全部）、`docs/`
- `preserved_behavior`：
  - `board.syscfg` 除 `UART_IMU` 的 `targetBaudRate` 与 `enabledInterrupts`/`rxFifoThreshold`
    外**一律不动**，尤其禁止触碰 `QEI_LEFT`/`QEI_RIGHT`/`PWM_DRIVE_*` 引脚与实例。
  - `Mspm0Runtime_InitUartDma()` 现有三路 DMA 编排不变（UART_IMU 无 DMA，走纯 IRQ）。
  - `sys_init.c` 现有初始化顺序不变，仅在 `ImuUart_Init()` 后插入 `Imu_Init()`。

### 5.1 非版本控制的必需同步项

`.gitignore` 含 `Debug/`，故以下文件**不会出现在提交里，但不同步则构建不反映 syscfg 改动**：

- `Debug/ti_msp_dl_config.c` / `.h` / `device.opt` / `device_linker.cmd` —— 由 SysConfig CLI 重新生成
- `Debug/hc-team/driver/imu/subdir_vars.mk` —— `IMU.c/.o/.d` 改为 `imu.c/.o/.d`

重新生成命令（**已验证**：在未改 `board.syscfg` 时逐字节复现当前 4 个生成物）：

```powershell
& "C:/ti/ccs2041/ccs/utils/sysconfig_1.26.0/sysconfig_cli.bat" `
  -s "C:/ti/mspm0_sdk_2_11_00_07/.metadata/product.json" `
  -o "Debug" --compiler ticlang "board.syscfg"
```

## 6. 前置条件（已实测，非假设）

| 事实 | 证据 |
|---|---|
| 主机基线 76 PASS / 0 FAIL | `make.bat -C tests/host all` exit 0（本次施工前实跑） |
| 固件基线 exit 0 / 0 诊断 | `rtk make -C Debug all` exit 0（本次施工前实跑） |
| 生成物与 `board.syscfg` 当前同步 | SysConfig CLI 输出与 `Debug/` 现有 4 文件 `diff` 全等 |
| `NVIC_EnableIRQ(UART_IMU_INST_INT_IRQN)` 已存在 | `driver/board/board.c:28` —— 只需在 syscfg 打开外设级 RX 中断并提供 handler |
| IMU 零外部调用者 | P7 已查实并记录于 `plan_p7_imu_stepmotor.md` §0 |

## 7. 证据行（冻结，6 行预算用满）

| ID | 命令 | 期望 | 后置条件（可观察） |
|---|---|---|---|
| E01 | `rtk make -C Debug all`（clean 后） | exit 0 | 诊断计数为 0。**计数口径锚定 `: (warning\|error\|remark)`**，不得子串匹配 `warning`（会命中链接器旗标 `--warn_sections`，见 `plan_p7` §0.1） |
| E02 | `grep -rn "ti_msp_dl_config\|DL_\|delay_cycles" hc-team/driver/imu/` | — | **输出为空** —— IMU 层零 TI 依赖，拓扑边 `IMU_API --> DL_HAL : exposed TI header` 可关闭 |
| E03 | `grep -rn "IMU_UART_\|IMU_Update_Yaw\|IMU_Get_User_Yaw\|IMU_Get_Relative_Yaw\|Send_IMU_\|imu_measurement_t\|IMU_FUNC_" --include=*.c --include=*.h hc-team/` | — | **输出为空**；且 `hc-team/driver/imu/IMU.c`、`IMU.h` 施工前 `git ls-files` 命中 2、施工后命中 **0**（删除行按协议须证明「前有后无」） |
| E04 | `make.bat -C tests/host all`（PowerShell 工具执行） | exit 0 | **≥ 92 PASS / 0 FAIL**（76 基线 + ≥16 新增 IMU 用例）。**计数口径锚定 `^\s*PASS:`**，不得子串匹配 `error`（会命中 PID 用例名，见 `plan_p7` §0.1） |
| E05 | `grep -n "UART_IMU_BAUD_RATE" Debug/ti_msp_dl_config.h` 与 `awk '/SYSCFG_DL_UART_IMU_init/,/^}/' Debug/ti_msp_dl_config.c \| grep -c "DL_UART_MAIN_INTERRUPT_RX"` | — | BAUD 宏为 **115200**；`SYSCFG_DL_UART_IMU_init()` 体内 `DL_UART_MAIN_INTERRUPT_RX` 命中 **1** —— 证明 RX 中断由 syscfg 单源开启，非手写（**符号名于 2026-07-17 修订，见 §0.1**） |
| E06 | `git status --short` + `git diff --stat` | — | 变更文件全部落在 §5 `allowed_files`；`forbidden_files` 命中 **0**；秘密扫描（`password\|token\|secret\|api[_-]key`）**0 命中** |

E 行预算 6/6。**无硬件行** —— 上板 bring-up 归用户（决议 2026-07-16）。

## 8. 停止条件

- 若 `board.syscfg` 改动导致 `QEI_*` / `PWM_DRIVE_*` 引脚或实例发生任何变化 → 停止并报告
  （电机链路，AGENTS.md §8.1，禁止夹带）。
- 若 SysConfig CLI 重新生成后 `diff` 出现 `UART_IMU` 以外的差异 → 停止并报告。
- 若主机基线在施工前复跑不是 76/0 → 基线漂移，停止并修订契约。

## 9. 移交给用户的硬件项（本次无法验证，必须上板实测）

1. **实测输出速率**：`Imu_GetDiag().frame_count` 在 1 秒内的增量 ÷ 2（每周期两种帧）= 实际 RRATE。
   若为 10Hz，须调用一次 `Imu_SetOutputRate(IMU_OUTPUT_RATE_100_HZ)` 或 `200_HZ`。
2. **验证波特率裁定**：若 `checksum_error_count` 暴涨而 `frame_count` 不涨 → 115200 判断错误，
   模块实际波特率为他值。这正是 `Imu_GetDiag()` 存在的理由。
3. **航向角正方向**：车体逆时针旋转时 `yaw_deg` 增大还是减小，取决于模块贴装方向。
   **必须实测**，且修正点只能有一个（承接编码器 `s_direction_sign` 的教训：第二个反转会抵消第一个）。
4. **接线**：模块 TX → MCU PA25（`UART_IMU` RX），模块 RX → MCU PA26（`UART_IMU` TX），共地。
5. `IMU_NEW_EXAMPLE/` 目前未纳入版本控制（含 `keil/Objects/*.o`、`.axf` 等构建产物）。
   本次不提交；如需保留 datasheet 应单独派工，只纳入 PDF 与源码。

## 10. 风险

1. **本次交付物是「驱动可用」，不是「IMU 已工作」。** 全部 E 行都是主机侧与构建侧证据。
   器件首次通信必须由用户上板完成，`Imu_GetDiag()` 是为此准备的唯一诊断入口。
2. **`Imu_ZeroYaw` / `Imu_SetOutputRate` 写器件 flash**，有写入寿命。二者均为一次性动作，
   禁止放入周期任务。本次不在任何自动路径上调用它们。
3. 承接未结项（本次不动）：编码器方向新板实测、核心板晶振书面确认。
