# 调试串口迁移：VOFA 迁 UART5/PA1/PA0，PA10/PA11 收归 BSL 烧录专用

计划所有者：Codex
制定日期：2026-07-16
状态：**`CODEX_ACCEPTED`（2026-07-16，Codex 自施工自验收）**

> 流程例外：用户 2026-07-16 裁定本任务**不经 REASONIX**，由 Codex 自行施工并自行验收。
> 记录此例外以免后续误读为流程漂移——常规仍是「Codex 计划 → REASONIX 施工 → Codex 验收」。

前置：P6 已验收合入（`9e6b590`）；`tests/host` 基线 76 项全绿。

## 0. 用户裁定（2026-07-16）

1. **PA25/PA26 = 陀螺仪 IMU**（`UART_IMU`=UART3，当前配置已如此，本计划不动）。
2. **PA10/PA11 只做 BSL 烧录**，波特率 **9600**，**不需要 DMA**；运行期只等上位机的 ENTRY 信号跳 bootloader，之后不再工作。
3. **VOFA 上下位机实时调参串口另开 UART5 @ PA1(TX)/PA0(RX) @ 230400，需要 DMA**。
4. **UART5 的引脚板上尚未引出**——用户将要求硬件组新画。本计划是固件先行。
5. 硬件表 Sheet1 第 41/42 行（调试串口占 PA25/PA26）与 Sheet2 第 4/5 行（陀螺仪迁 PA28/PA31）**均按本裁定作废**，须回告硬件组。

## 1. 基线事实（Codex 逐条核实，非推测）

| 事实 | 证据来源 |
|---|---|
| BSL ROM 固定 UART0 + PA11(RX)/PA10(TX) | SDK `bsl_uart_flash_interface/README.md` 引脚表 |
| BSL UART 波特率 **9600 是 ROM 固定值**，非可调项 | `BSL_UART_DEFAULT_BAUD ((uint32_t) 9600U)`；TI BSL GUI `UART_send.py:62` 把 `baudrate=9600` 写死，UART 路径不发换波特率命令（`CMD_CHANGE_BAUDRATE 0x52` 仅 CAN 插件用） |
| **ENTRY 信号 = 0x22**，软件跳 BSL 机制成立 | `bsl_software_invoke_app_demo_uart/main.c:197` `if (gData == 0x22) BSL_trigger_flag = true;` → `invokeBSLAsm()`（擦 SRAM + 写 `DL_SYSCTL_RESET_BOOTLOADER_ENTRY`）；BSL GUI 在 9600 下发 `b"\x22"` |
| 该 SDK 示例的 UART0 亦为 **9600** | `bsl_software_invoke_app_demo_uart/ti_msp_dl_config.h:93` `UART_0_BAUD_RATE (9600)` |
| **PA28/PA31 = UART0.TX/RX**，与 PA10/PA11 同一外设，**不是"另一个 UART"** | SysConfig 器件数据 `MSPM0G351X.json`（`muxes` × `peripheralPins` 关联） |
| **PA1 = UART5.TX，PA0 = UART5.RX**，且 **LQFP-64(PM) 与 LQFP-100(PZ) 下均存在** | 同上，按 `packages` 逐封装枚举 |
| UART5：`SYS_EN_DMA=1`、`SYS_UARTADV=false`、`SYS_FENTRIES=4`——与**当前在跑**的 `UART_VISION`(UART1) 能力档案完全一致 | `MSPM0G351X.json` `peripherals` |
| 本器件**无 UART2**（仅 UART0/1/3/4/5/6/7） | 同上 |
| PA0、PA1 在改动前 `board.syscfg` 零占用 | 已占用 39 脚清单不含 PA0/PA1 |
| 构建链**会**从 `board.syscfg` 自动重生成 `ti_msp_dl_config.*` | `Debug/subdir_rules.mk:8-21` 调用 `sysconfig_cli.bat` |
| 所有 `UART_HOST_LINK` 引用点**全部走生成宏**，无一处硬编码 `UART0` | `mspm0_runtime.c:216,224,321,322,343,358`；`vofa_uart.c:138,145` |

**关键推论（已被 R05 证实）**：保留 `$name = "UART_HOST_LINK"` 与 DMA 通道 `$name` → 换外设**零代码改动**。

## 2. 三个设计问题

1. **抽象是什么？** 不变。`VofaUart_*` 对外仍是"上位机遥测链路"，调用者不需要知道它落在哪个 UART 实例或哪对引脚。本计划只改配置层的物理归属。
2. **必须隐藏什么？** UART 实例号与引脚归属本就是 `board.syscfg` + 生成宏的私有事实，Driver 以上不可见。**本计划同时是对这一点的实测**：R05 证明零代码改动，泄漏假设被证伪。
3. **代码属于哪里？** 全部落在 `board.syscfg`（硬件配置唯一源）与其生成物。Driver/App 零改动。

## 3. 目标配置

```text
UART0 @ PA10(TX)/PA11(RX) @ 9600      -> UART_BSL_ENTRY（无线 BSL 烧录，无 DMA、无中断）
UART5 @ PA1(TX)/PA0(RX)   @ 230400    -> UART_HOST_LINK（VOFA 实时调参，DMA TX+RX）★迁入
UART3 @ PA26(TX)/PA25(RX) @ 230400    -> UART_IMU（陀螺仪，不变）
UART1 @ PA8/PA9           @ 230400    -> UART_VISION（不变）
UART7 @ PB15/PB16         @ 230400    -> UART_STEPPER_BUS（不变）
```

- **DMA 净占用不变**：`DMA_CH1`(TX)/`DMA_CH2`(RX) 的触发源由 UART0 改指 UART5，通道数与 `$name` 均不变（`vofa_uart.c:16` 依赖 `DMA_CH1_CHAN_ID` 这一 `$name` 派生宏）。
- **UART_BSL_ENTRY 不开中断**：现在开却没有 IRQ handler，首个到达字节就会落进 startup 的默认处理程序死循环。中断待 ENTRY 监听器落地时同批次开启。

## 4. 实际改动（Codex 施工）

仅 `board.syscfg`，四处：

1. 新增实例声明 `const UART5 = UART.addInstance();`
2. `UART2` 块（`$name=UART_HOST_LINK`）：`peripheral.$assign` `UART0`→`UART5`；`txPin` `PA10`→`PA1`；`rxPin` `PA11`→`PA0`。**其余字段（FIFO/DMA 触发/中断/230400/pinConfig）逐字未动。**
3. 新增 `UART5` 块：`$name=UART_BSL_ENTRY`、`UART0`、`PA10`(TX)/`PA11`(RX)、9600、FIFO、**无 DMA、无 `enabledInterrupts`**。
4. 两处块首加注释，说明脚本变量名与物理外设名的错位（脚本 `UART2`→外设 `UART5`；脚本 `UART5`→外设 `UART0`），以及 9600/无中断的理由。

生成物 `Debug/ti_msp_dl_config.*` 由 SysConfig 产出，未手改。

## 5. 验收证据（R01–R06，Codex 自验，全部实跑）

- **R01** `make.bat -C tests/host clean` 后 `all` → **exit 0**，76 项全绿（Encoder 14 / PID 5 / Motor 7 / Key 6 / UART FIFO 13 / VOFA RX 6 / StepMotor UART 10 / OLED 15）。证明未误伤。
- **R02** `make.bat -C Debug clean` 后 `all` → **exit 0**，SysConfig 重新生成成功，**0 error / 0 warning**，无引脚或 DMA 触发冲突。
- **R03** 生成宏（`Debug/ti_msp_dl_config.h`）：
  - `UART_HOST_LINK_INST = UART5`；`UART_HOST_LINK_INST_IRQHandler = UART5_IRQHandler`；`UART_HOST_LINK_BAUD_RATE = (230400)`
  - `GPIO_UART_HOST_LINK_TX_PIN = DL_GPIO_PIN_1` / `IOMUX_PINCM2_PF_UART5_TX`；`RX_PIN = DL_GPIO_PIN_0` / `IOMUX_PINCM1_PF_UART5_RX`
- **R04** `UART_BSL_ENTRY_INST = UART0`；`GPIO_UART_BSL_ENTRY_TX = PA10/IOMUX_PINCM21`、`RX = PA11/IOMUX_PINCM22`；`UART_BSL_ENTRY_BAUD_RATE = (9600)`。**与 SDK BSL 示例的引脚定义逐字吻合**（该示例 `GPIO_UART_0_IOMUX_RX = IOMUX_PINCM22`、`TX = IOMUX_PINCM21`）。
- **R05** `git status --short hc-team tests` → **输出为空**。**驱动/应用/测试零改动**，§2 推论成立（UART 实例号确为 Driver 以下私有事实，无泄漏）。
- **R06** 生成的 `ti_msp_dl_config.c`：`DMA_CH1/DMA_CH2` 的 `.trigger` 取自 `UART_HOST_LINK_INST_DMA_TRIGGER_0/1`（即 UART5）；`UART_BSL_ENTRY` 段只有 `reset/enablePower/IOMUX/ClockConfig/init/setOversampling/setBaudRateDivisor(9600)`，**无 `enableInterrupt`、无 NVIC**——确认未开中断。map 含 `UART5`、`UART5_IRQHandler`。

## 6. 已知缺口（Codex 自认，需用户裁定）

1. **`UART_BSL_ENTRY` 目前没有消费者**——syscfg 会在 `SYSCFG_DL_init()` 里把 UART0 配成 9600，但**没有任何代码读它**，ENTRY 字节 0x22 的监听器尚未实现。严格说这是"配置无消费者"，与本项目刚在 P6 删掉 `at24cxx` 死代码的口径存在张力。
   保留的理由：它是 ENTRY 监听器的前置，且把 PA10/PA11 钉在 UART 模式，语义上声明了归属。**但在监听器落地前，它不产生任何运行期行为。**
   落地监听器需要：UART0 RX 中断 + handler（判 0x22）+ `invokeBSLAsm()`（擦 SRAM 的内联汇编 + `DL_SYSCTL_RESET_BOOTLOADER_ENTRY` 复位，含 BSL_ERR_01 勘误绕行）。参考 `bsl_software_invoke_app_demo_uart/main.c`。**这是独立特性，须单独立计划。**
2. **命名**：用户原话把 `UART_HOST_LINK` 指为烧录口。Codex 反向处置——让 `UART_HOST_LINK` 跟随 VOFA 迁到 UART5，烧录口另起 `UART_BSL_ENTRY`。理由：(a) "host link"语义上就是上位机遥测链路，(b) 跟随 VOFA 可换来**零代码改动**（否则 `mspm0_runtime.c` 6 处 + `vofa_uart.c` 2 处都要改）。若用户坚持原命名，改回即可，代价是上述 8 处改动。

## 7. 硬件闭环（软件验收无法覆盖）

**所有 R 行只证明"配置与代码一致"，证明不了 PCB 接线。** 以下必须由用户/硬件组闭环：

1. **PA0/PA1 需硬件组新引出**（用户 2026-07-16 明示板上尚无）。Codex 只能证明芯片支持（§1），证明不了焊盘存在。**在硬件引出前，VOFA 在实物上不可用。**
2. VOFA 上位机在 PA1/PA0 上收到 230400 正常波形。
3. 无线模块在 PA10/PA11 上仍能 9600 BSL 烧录。注意：**在 ENTRY 监听器落地前，软件跳 BSL 不可用**，需用硬件 BSL invoke 引脚 + 复位进 BSL。

## 8. 与其他计划的关系

- **与 `plan_pin_table_v2_migration.md` 同改 `board.syscfg`**。本计划已合入，引脚迁移解除阻断后须在此基础上改，避免并发改动源。
- **本计划绕开引脚表 Q1（封装 LQFP-64 vs 100）**：PA0/PA1 两种封装下均存在，故封装争议不阻塞本计划。
- 本计划**否决**引脚表 Sheet1 第 41/42 行与 Sheet2 第 4/5 行（见 §0.5），须随 Q1/Q4 一并回告硬件组。
