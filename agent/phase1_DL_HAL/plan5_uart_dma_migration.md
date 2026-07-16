# Plan5: UART And DMA Migration

## Source And Status

- Source outline: `agent/general_plan_in_all.md`, Phase 5.
- Source handoff: `agent/agent.md`, Plan List `P4`.
- Dependency: Plan1 runtime must own UART/DMA IRQ dispatch; Plan2 system init must call the runtime UART/DMA init path.
- Purpose: remove UART and DMA HAL wrapper usage from `hc-team` while preserving the three board UART roles, DMA throughput behavior, RX callback switching, and TX busy checks.
- Status rule: completing this document is not completing `P4`. Mark `P4` done only after code is implemented, built, and checked against the acceptance criteria below.
- Implementation status (2026-07-13): **done** — code migrated, Debug build passed, acceptance searches passed. Hardware smoke tests not run.

## Design Answers Before Coding

1. Abstraction: application modules need named board UART roles and bounded send/busy operations, not logical HAL channel enums.
2. Hidden details: UART instances, DMA channel IDs, UART FIFO/register details, DMA transfer setup, callback slots, and busy flags stay inside the local MSPM0 runtime or a private source helper.
3. Module boundary: do not recreate `HC_HAL_UART_*`, `HC_HAL_DMA_*`, `UART_CH_*`, or `DMA_CH_*` under new names. Runtime APIs must be board-specific and use standard C types.

## Scope

Allowed implementation work:

- `hc-team/driver/mspm0_runtime/mspm0_runtime.h`
- `hc-team/driver/mspm0_runtime/mspm0_runtime.c`
- `hc-team/app/tasks/platform_2d/stepmotor_bus.c`
- `hc-team/app/tasks/platform_2d/stepmotor_bus.h`
- `hc-team/app/tasks/platform_2d/vision_bus.c`
- `hc-team/app/tasks/platform_2d/vision_bus.h`
- `hc-team/app/tasks/uart_stress/uart_stress.c`
- `hc-team/app/tasks/uart_stress/uart_stress.h`
- `hc-team/driver/uart_vofa/uart_vofa.c`
- `hc-team/driver/uart_vofa/uart_vofa.h`, only if the public type contract must be cleaned
- `hc-team/driver/imu/IMU.c`, only for its UART send calls
- Comments in touched files that mention old `UART_CH_*`, `HC_HAL_UART_*`, or `HC_HAL_DMA_*`.

Allowed small adjacent work:

- If a touched UART module uses `hc_hal_systick.h` only for timeout/timestamp, it may be migrated to `Mspm0Runtime_GetTickMs()` in the same edit to avoid leaving a HAL include in that module.
- If a touched module still uses `HC_U8`, `HC_U16`, `HC_U32`, or `HC_Bool_e`, keep the smallest necessary compatibility until Plan7 unless removing it is required to delete a HAL include safely.

Out of scope:

- Do not migrate I2C device drivers in this plan. That belongs to Plan6.
- Do not perform the full common-type removal. That belongs to Plan7.
- Do not edit `project/mspm0/board.syscfg` or generated SysConfig files.
- Do not remove `sdk/hal` include paths or source participation in this plan.
- Do not change UART protocol framing, packet layout, retry behavior, or scheduler semantics.

## Required Mapping

Use the generated SysConfig symbols from `ti_msp_dl_config.h`.

UART roles:

> 2026-07-16 更正：下表 Baud 列为本计划编写时的历史值；`board.syscfg` 现已将三路统一为 230400。本文件为已完成阶段的存档，仅在此注记，不改写历史表格。

| Board role | Old channel | SysConfig instance | Hardware UART | Baud |
| --- | --- | --- | --- | --- |
| Stepmotor bus | `UART_CH_STEPMOTOR` | `UART_STEPMOTOR_HORIZON_INST` | UART2 | 921600 |
| VOFA debug | `UART_CH_VOFA` | `UART_VOFA_INST` | UART3 | 115200 |
| Vision bus | `UART_CH_VISION` | `UART_Vision_INST` | UART1 | 115200 |

DMA channels:

| Board role | Direction | Old logical channel | SysConfig channel macro |
| --- | --- | --- | --- |
| Stepmotor | RX | `DMA_CH_STEPMOTOR_RX` | `DMA_CH0_CHAN_ID` |
| Stepmotor | TX | `DMA_CH_STEPMOTOR_TX` | `DMA_CH2_CHAN_ID` |
| VOFA | RX | `DMA_CH_VOFA_RX` | `DMA_CH5_CHAN_ID` |
| VOFA | TX | `DMA_CH_VOFA_TX` | `DMA_CH1_CHAN_ID` |
| Vision | RX | `DMA_CH_VISION_RX` | `DMA_CH7_CHAN_ID` |
| Vision | TX | `DMA_CH_VISION_TX` | `DMA_CH8_CHAN_ID` |

Use channel macros, not hardcoded numeric values. `DMA_CH8_CHAN_ID` currently maps to DMA channel number 4; do not assume the suffix equals the hardware channel number.

## Runtime API Standard

Extend the Plan1 runtime with board-role UART operations using only standard C types.

A compliant interface can be shaped like this:

```c
bool Mspm0Runtime_SendStepmotor(const uint8_t *data, uint32_t length);
bool Mspm0Runtime_SendVofa(const uint8_t *data, uint32_t length);
bool Mspm0Runtime_SendVision(const uint8_t *data, uint32_t length);

bool Mspm0Runtime_SendStepmotorByte(uint8_t data);
bool Mspm0Runtime_SendVofaByte(uint8_t data);
bool Mspm0Runtime_SendVisionByte(uint8_t data);

bool Mspm0Runtime_IsStepmotorTxBusy(void);
bool Mspm0Runtime_IsVofaTxBusy(void);
bool Mspm0Runtime_IsVisionTxBusy(void);
```

This is a standard, not forced exact code. Any deviation must still remove the old UART/DMA wrapper names from `hc-team`, hide DriverLib details, and preserve the existing behavior.

## DMA Send Standard

- Preserve DMA TX for throughput-critical send paths.
- Keep TX buffers valid until DMA completion. If current callers pass stack or transient buffers, the runtime must copy into a private static TX buffer before starting DMA.
- Keep bounded behavior for busy TX:
  - Either return `false` immediately when busy, or wait only within a documented bounded timeout.
  - Do not spin forever waiting for DMA completion.
- Clear busy state from DMA completion handling, not from the caller after starting DMA.
- Keep UART RX dispatch work minimal inside IRQ handlers.
- RX callback switching must keep working:
  - `StepmotorBus_RxISR`
  - `VisionBus_RxISR`
  - `vofa_rx_isr`
  - UART stress mode takeover and restore of the stepmotor RX callback

## Module Standards

Stepmotor bus:

- Remove `#include "hc_hal_uart.h"` and `#include "hc_hal_dma.h"`.
- Replace `HC_HAL_UART_SendBuffer(UART_CH_STEPMOTOR, ...)` with the runtime stepmotor send helper.
- Replace `HC_HAL_DMA_IsBusy(DMA_CH_STEPMOTOR_TX)` with `Mspm0Runtime_IsStepmotorTxBusy()`.
- Preserve existing frame format, queue behavior, retry behavior, and timeout thresholds.
- Update comments in `stepmotor_bus.h` that tell users to call `HC_HAL_UART_RegisterRxCallback`.

Vision bus:

- Remove `#include "hc_hal_uart.h"`.
- Replace vision UART sends, if any, with the runtime vision send helper.
- Preserve byte-wise RX framing and timeout behavior.
- Keep `VisionBus_RxISR` as the registered callback target.

VOFA:

- Remove `#include "hc_hal_uart.h"`.
- Replace `HC_HAL_UART_SendBuffer(UART_CH_VOFA, ...)` with the runtime VOFA send helper.
- Preserve the current VOFA protocol selection and packet bytes.
- Do not alter float packing, JustFloat/FireWater framing, or output cadence.

UART stress:

- Remove `#include "hc_hal_uart.h"` and `#include "hc_hal_dma.h"`.
- Replace stress TX with runtime stepmotor send helper.
- Replace stress busy checks with `Mspm0Runtime_IsStepmotorTxBusy()`.
- Preserve the callback takeover/restore behavior through `Mspm0Runtime_SetStepmotorRxCallback(...)`.

IMU UART send:

- Replace `HC_HAL_UART_SendByteById(HC_HAL_UART_ID_0, ...)` and `HC_HAL_UART_SendById(HC_HAL_UART_ID_0, ...)` with the runtime stepmotor send helpers only if this IMU path truly uses the stepmotor UART.
- If the intended UART role is unclear, stop and record a blocker rather than guessing.

## Execution Steps

1. Capture current UART/DMA HAL dependencies:

```powershell
rtk rg -n "hc_hal_uart|HC_HAL_UART|UART_CH_|hc_hal_dma|HC_HAL_DMA|DMA_CH_" hc-team
```

2. Inspect `mspm0_runtime.h/.c` and decide the smallest send API extension needed.
3. Implement runtime send helpers for stepmotor, VOFA, and vision roles.
4. Implement or confirm DMA TX busy state and DMA completion clearing.
5. Migrate stepmotor bus send and busy checks.
6. Migrate UART stress send, busy checks, callback takeover, and restore comments.
7. Migrate VOFA send path.
8. Migrate vision send path if it sends data; preserve RX callback registration.
9. Migrate IMU UART send path only after confirming the intended board UART role.
10. Remove stale UART/DMA HAL comments in touched files.
11. Run acceptance searches and the Debug build.
12. Update `agent/agent.md` with status, files touched, verification result, and any blockers.

## Acceptance Criteria

Plan5 is done only when all of these are true:

- No `hc_hal_uart.h` include remains in `hc-team`.
- No `hc_hal_dma.h` include remains in `hc-team`.
- No `HC_HAL_UART_*`, `UART_CH_*`, `HC_HAL_DMA_*`, or `DMA_CH_*` names remain in `hc-team`, except in standalone migration documentation under `agent/`.
- `StepmotorBus_RxISR`, `VisionBus_RxISR`, and `vofa_rx_isr` are still runtime dispatch targets.
- UART stress mode can take over and restore the stepmotor RX callback through runtime APIs.
- TX busy checks use direct DMA state or local runtime busy flags updated by DMA completion.
- UART IRQ and DMA IRQ handlers do not call into `sdk/hal`.
- No generic UART or DMA HAL replacement was introduced.
- The Debug build succeeds.

## Verification

Required searches:

```powershell
rtk rg -n "hc_hal_uart|HC_HAL_UART|UART_CH_|hc_hal_dma|HC_HAL_DMA|DMA_CH_" hc-team
rtk rg -n "Mspm0Runtime_.*(Stepmotor|Vofa|Vision)|Mspm0Runtime_Is.*TxBusy|SetStepmotorRxCallback|StepmotorBus_RxISR|VisionBus_RxISR|vofa_rx_isr" hc-team
rtk rg -n "UART_STEPMOTOR_HORIZON_INST|UART_VOFA_INST|UART_Vision_INST|DMA_CH0_CHAN_ID|DMA_CH1_CHAN_ID|DMA_CH2_CHAN_ID|DMA_CH5_CHAN_ID|DMA_CH7_CHAN_ID|DMA_CH8_CHAN_ID" hc-team/driver/mspm0_runtime Debug/ti_msp_dl_config.h
```

Required build check:

```powershell
rtk make -C Debug all
```

Suggested clean verification before closing the phase:

```powershell
rtk make -C Debug clean all
```

Suggested hardware smoke checks after build:

- Stepmotor UART receives commands at 921600 and TX busy clears after DMA completion.
- UART stress mode can take over RX and restore normal stepmotor RX.
- VOFA still emits the selected protocol format.
- Vision UART RX frames still parse and timeout behavior is unchanged.
- IMU UART send path, if used, still transmits on the intended board UART role.

## Blockers

Stop and record a blocker instead of guessing if:

- A current UART caller's intended board role is ambiguous.
- DMA TX buffer lifetime cannot be preserved without changing a public API.
- A send path depends on unbounded blocking in the old HAL.
- DMA completion flags do not reliably clear busy state.
- SysConfig macro names differ from the mapping table above.

