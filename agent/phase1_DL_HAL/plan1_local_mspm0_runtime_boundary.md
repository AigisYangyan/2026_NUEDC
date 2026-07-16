# Plan1: Local MSPM0 Runtime Boundary

## Source And Status

- Source outline: `agent/general_plan_in_all.md`, Phase 1.
- Source handoff: `agent/agent.md`, Plan List `P0`.
- Purpose: create the smallest board-specific runtime boundary under `hc-team` so later phases can remove `sdk/hal` without scattering raw interrupt, SysTick, UART, DMA, and encoder state across application code.
- Status rule: completing this document is not completing `P0`. Mark `P0` done only after the code is implemented, built, and checked against the acceptance criteria below.
- Implementation status (2026-07-13): **done** after blocker re-verify. Runtime owns UART/DMA IRQs without hc_hal; SysTick uses CPUCLK_FREQ; Debug build links runtime and produces NUEDC.out. Hardware smoke tests not run.

## Design Answers Before Coding

1. Abstraction: expose board runtime capabilities needed by the application: millisecond timebase, bounded delays, UART RX/TX callback dispatch, UART/DMA busy state, and encoder total counts.
2. Hidden details: hide SysTick counters, callback tables, DMA channel mapping, ISR flag handling, encoder quadrature state, and generated SysConfig macro names inside the runtime source file.
3. Module boundary: place hardware ownership in one board-specific runtime module under `hc-team`, not in app tasks and not in a recreated generic HAL.

## Scope

Recommended new files:

- `hc-team/driver/mspm0_runtime/mspm0_runtime.h`
- `hc-team/driver/mspm0_runtime/mspm0_runtime.c`

The exact folder may change only if an existing project convention is clearly better. If it changes, keep the runtime in one board-specific directory and record the choice in `agent/agent.md`.

Allowed implementation work:

- Add a local runtime module that includes `ti_msp_dl_config.h` and direct TI DriverLib headers.
- Own `SysTick_Handler`, `GPIOA_IRQHandler`, `GPIOB_IRQHandler`, `DMA_IRQHandler`, and UART instance IRQ handlers when the old HAL no longer owns them.
- Provide callback registration for `StepmotorBus_RxISR`, `vofa_rx_isr`, `VisionBus_RxISR`, and the stepmotor TX completion callback path.
- Track UART TX busy using either direct DMA status or local volatile flags cleared by DMA completion.
- Track encoder left/right counts from the SysConfig encoder GPIO interrupts.

Out of scope:

- Do not migrate key, motor direction, grayscale, OLED, MPU6050, AT24Cxx, or full UART send APIs in this plan.
- Do not remove `sdk/hal` include paths or delete `sdk/hal`.
- Do not edit `project/mspm0/board.syscfg` or generated SysConfig files.
- Do not introduce `hc_hal`, `HC_HAL_*`, `VPIN_*`, `UART_CH_*`, `DMA_CH_*`, or a new generic virtual pin abstraction.

## Minimal Runtime Interface Standard

The public runtime header must include only standard C headers such as `stdint.h`, `stdbool.h`, and `stddef.h`. It must not include `ti_msp_dl_config.h`, DriverLib headers, or `sdk/hal` headers.

Use board-specific names instead of generic HAL names. A compliant interface can be shaped like this:

```c
typedef void (*Mspm0Runtime_UartRxCallback)(uint8_t data);
typedef void (*Mspm0Runtime_UartTxCallback)(void);

void Mspm0Runtime_InitTick(void);
uint32_t Mspm0Runtime_GetTickMs(void);
void Mspm0Runtime_DelayMs(uint32_t delay_ms);
void Mspm0Runtime_DelayUs(uint32_t delay_us);

void Mspm0Runtime_SetStepmotorRxCallback(Mspm0Runtime_UartRxCallback callback);
void Mspm0Runtime_SetVofaRxCallback(Mspm0Runtime_UartRxCallback callback);
void Mspm0Runtime_SetVisionRxCallback(Mspm0Runtime_UartRxCallback callback);
void Mspm0Runtime_SetStepmotorTxCallback(Mspm0Runtime_UartTxCallback callback);

bool Mspm0Runtime_IsStepmotorTxBusy(void);
bool Mspm0Runtime_IsVofaTxBusy(void);
bool Mspm0Runtime_IsVisionTxBusy(void);

void Mspm0Runtime_GetEncoderCounts(int32_t *left, int32_t *right);
```

This is a standard, not a forced exact API. Any deviation must still keep hardware details private, keep the API board-specific, and use only standard C types in public headers.

## Required Behavior

SysTick:

- Configure a 1 ms tick from SysConfig `CPUCLK_FREQ` (currently 80 MHz): period = CPUCLK_FREQ/1000.
- Increment a volatile millisecond counter in `SysTick_Handler`.
- Preserve the old scheduler behavior by calling `TaskTimeSliceManage()` from `SysTick_Handler`.
- Treat `DelayMs(0)` and `DelayUs(0)` as no-op.
- Keep delay loops simple and bounded by the requested argument; do not add dynamic allocation or RTOS assumptions.

UART callback dispatch:

- Map SysConfig UART instances to board roles:
  - `UART_STEPMOTOR_HORIZON` / UART2 -> stepmotor bus.
  - `UART_VOFA` / UART3 -> VOFA.
  - `UART_Vision` / UART1 -> vision bus.
- Dispatch received bytes to the currently registered callback for that role.
- Allow UART stress mode to temporarily replace and later restore the stepmotor RX callback.
- Keep ISR work minimal: read/clear interrupt state, move bytes or flags, dispatch byte callbacks, and return.
- If TX completion callbacks are needed, invoke them from DMA or UART completion state, not from polling loops.

DMA and busy state:

- Preserve the current behavior where stepmotor, VOFA, and vision TX can test whether a send path is busy.
- If direct DriverLib DMA status is reliable, use it. If not, maintain a local volatile busy flag set at transfer start and cleared in DMA completion.
- The runtime must not spin forever waiting for DMA completion.

Encoder IRQ state:

- Use `GPIO_GRP_MOTOR_INTERRUPT` pins from `project/mspm0/board.syscfg`:
  - right A: `PIN_R_A` on `PB14`.
  - right B: `PIN_R_B` on `PB0`.
  - left A: `PIN_L_A` on `PA7`.
  - left B: `PIN_L_B` on `PB20`.
- Update encoder totals in GPIO IRQ handlers using the same left/right sign semantics expected by `Encoder_UpdateSample()`.
- Clear GPIO interrupt status after reading the triggered pins.
- Keep encoder counters private to the runtime and expose totals through a getter.

## Execution Steps

1. Search current dependencies:

```powershell
rtk rg -n "HC_HAL_SYSTICK|HC_HAL_UART|HC_HAL_DMA|HC_HAL_GPIO_GetEncoderCounts|SysTick_Handler|GPIOA_IRQHandler|GPIOB_IRQHandler|DMA_IRQHandler|UART.*IRQHandler" hc-team sdk/hal
```

2. Create the runtime header first and keep it limited to the minimal API.
3. Implement SysTick and confirm it preserves `TaskTimeSliceManage()`.
4. Implement UART callback slots and IRQ dispatch for the three board UART roles.
5. Implement DMA completion handling and busy state for UART TX paths.
6. Implement encoder interrupt counting and total readout.
7. Wire only the call sites required to prove the runtime compiles; leave broad module migrations to later plans.
8. Update `agent/agent.md` with changed status, files touched, verification result, and any blockers.

## Acceptance Criteria

Plan1 is done only when all of these are true:

- Runtime public APIs use standard C types only and do not expose TI register pointers, DriverLib types, SysConfig macros, or old `HC_*` aliases.
- New runtime source includes `ti_msp_dl_config.h` and direct TI DriverLib headers where hardware access is needed.
- No new file exposes names beginning with `HC_HAL_`, `hc_hal`, `VPIN_`, `UART_CH_`, `DMA_CH_`, or generic virtual pin concepts.
- ISR ownership is clear for `GPIOA`, `GPIOB`, `DMA`, and the three UART instances.
- `SysTick_Handler` increments the tick and continues to drive `TaskTimeSliceManage()`.
- Runtime callback switching supports the existing stepmotor, VOFA, vision, and UART stress callback flow.
- Encoder totals update through GPIO interrupts and can be read by later encoder migration work.

## Verification

Required searches:

```powershell
rtk rg -n "HC_HAL_|hc_hal|VPIN_|UART_CH_|DMA_CH_" hc-team/driver/mspm0_runtime
rtk rg -n "SysTick_Handler|GPIOA_IRQHandler|GPIOB_IRQHandler|DMA_IRQHandler|UART.*IRQHandler" hc-team
```

Required build check:

- Build the CCS project if the local environment supports it.
- If CCS build cannot be run, record that explicitly in `agent/agent.md` and at minimum run the reference searches above.

Suggested hardware smoke checks after build:

- Tick advances once per millisecond enough for UI/task scheduling.
- UART stepmotor RX callback can be replaced by UART stress mode and restored.
- Encoder totals change when left/right encoder inputs toggle.

## Blockers

Stop and record a blocker instead of guessing if:

- `ti_msp_dl_config.h` is not generated or its macro names differ from `board.syscfg`.
- SysConfig already generated conflicting IRQ handlers.
- Removing the old HAL ISR owner would leave duplicate or missing interrupt symbols.
- The current code still requires `HC_HAL_PWM_Init()` or other old init state to preserve behavior; coordinate with Plan2/Plan4 before changing startup behavior.

## Blocker re-verify (2026-07-13)

- Cleared: stale i2c/timer cfg make refs; SysTick 80 MHz; Plan1 no longer depends on hc_hal for IRQ/dispatch; runtime sources in Debug make rules; `make -C Debug all` OK.
