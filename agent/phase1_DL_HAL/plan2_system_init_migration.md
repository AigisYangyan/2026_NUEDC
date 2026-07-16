# Plan2: System Init Migration

## Source And Status

- Source outline: `agent/general_plan_in_all.md`, Phase 2.
- Source handoff: `agent/agent.md`, Plan List `P1`.
- Primary file: `hc-team/app/system/sys_init.c`.
- Dependency: Plan1 runtime boundary must exist or this plan must create only the minimum missing runtime entry points needed by `SysInit()`.
- Status rule: completing this document is not completing `P1`. Mark `P1` done only after `sys_init.c` is migrated, built, and checked.
- Implementation status (2026-07-13): **done** after blocker re-verify. sys_init remains free of hc_hal; build produces NUEDC.out. Hardware smoke tests not run.

## Design Answers Before Coding

1. Abstraction: `SysInit()` orchestrates system startup order; it should not own hardware driver internals.
2. Hidden details: SysConfig-generated peripheral setup, NVIC details, SysTick setup, UART/DMA dispatch state, and encoder IRQ state must stay behind DriverLib/SysConfig calls or the local runtime boundary.
3. Module boundary: `sys_init.c` may sequence module initialization, but it must not recreate HAL wrappers or contain reusable hardware abstractions.

## Scope

Allowed implementation work:

- Convert `SysInit()` from old wrapper init calls to `SYSCFG_DL_init()` plus local runtime and direct DriverLib startup calls.
- Remove all `#include "hc_hal_*.h"` lines from `sys_init.c`.
- Register UART RX callbacks through the local Plan1 runtime API.
- Start PWM counters directly with `DL_TimerA_startCounter()` only after confirming zero-duty startup remains safe.
- Enable required NVIC lines directly when SysConfig does not already enable them.
- Preserve existing module initialization order unless DriverLib requires a small, documented reorder.

Out of scope:

- Do not migrate GPIO users, motor PWM implementation, UART send APIs, I2C device drivers, or common `HC_*` aliases unless a tiny change is strictly needed to keep `SysInit()` compiling.
- Do not edit `board.syscfg` or generated SysConfig files.
- Do not remove `sdk/hal` include paths or linked resources in this plan.
- Do not alter startup files, linker scripts, target configs, or unrelated CCS metadata.

## Required Init Shape

`SysInit()` should follow this startup shape:

1. Set `g_eSysFlagManage = SYS_STA_INIT`.
2. Call `SYSCFG_DL_init()` or the project-approved SysConfig root init function exactly once.
3. Initialize the local runtime pieces needed before app modules:
   - SysTick tick and scheduler time slice.
   - UART callback/DMA dispatch state.
   - Encoder IRQ state, if Plan1 requires explicit initialization.
4. Initialize app, driver, middleware, and task modules in the existing readable order:
   - `OLED_Init()`
   - `Key_Init()`
   - `Menu_Init()`
   - `Motor_Init()`
   - `Encoder_Init()`
   - `pid_Init()`
   - `VofaRegister_Init()`
   - `Motor_SetPwm(&g_tMotorL, 0.0f)`
   - `Motor_SetPwm(&g_tMotorR, 0.0f)`
   - `StepmotorBus_Init()`
   - `VisionBus_Init()`
   - `vofa_init()`
   - app task init calls in their current order.
5. Register UART RX callbacks through local runtime functions:
   - stepmotor -> `StepmotorBus_RxISR`
   - VOFA -> `vofa_rx_isr`
   - vision -> `VisionBus_RxISR`
6. Enable required NVIC lines.
7. Enable global interrupts with `__enable_irq()`.

## PWM Startup Rule

The outline requires direct PWM timer startup in this phase, but the current motor module may still depend on the old `HC_HAL_PWM_Init()` state until Plan4 migrates motor PWM.

Before removing wrapper PWM init from `SysInit()`, the implementing agent must prove one of these is true:

- Motor PWM has already been migrated to direct DriverLib or local board-specific helpers.
- `SYSCFG_DL_init()` plus direct zero-duty compare setup provides the same safe startup state.
- Plan2 is intentionally extended with the smallest board-specific PWM zero/start helper needed to preserve behavior, and that helper is not a generic HAL.

If none is true, mark `P1` blocked rather than leaving `Motor_SetPwm()` dependent on uninitialized old HAL state.

## NVIC Standard

Keep NVIC enables explicit and reviewable:

```c
NVIC_EnableIRQ(GPIOA_INT_IRQn);
NVIC_EnableIRQ(GPIOB_INT_IRQn);
NVIC_EnableIRQ(DMA_INT_IRQn);
NVIC_EnableIRQ(UART1_INT_IRQn);
NVIC_EnableIRQ(UART2_INT_IRQn);
NVIC_EnableIRQ(UART3_INT_IRQn);
```

Only remove one of these if generated SysConfig code demonstrably enables it or the interrupt owner no longer exists. Record the reason in `agent/agent.md`.

## Execution Steps

1. Confirm Plan1 runtime exists and inspect its public header.
2. Inspect `sys_init.c` and list every old wrapper include and old init call:

```powershell
rtk rg -n "hc_hal|HC_HAL_|UART_CH_|DMA_CH_" hc-team/app/system/sys_init.c
```

3. Replace wrapper includes with the local runtime header plus existing module headers.
4. Replace wrapper init calls:
   - `HC_HAL_SYSTICK_Init()` -> local runtime tick init.
   - `HC_HAL_Delay_Init()` -> remove if no longer needed by startup.
   - `HC_HAL_DMA_Init()` -> local runtime DMA/callback state init, if needed.
   - `HC_HAL_GPIO_Init()` -> `SYSCFG_DL_init()` responsibility.
   - `HC_HAL_PWM_Init()` / `HC_HAL_PWM_Start()` -> direct DriverLib startup only after satisfying the PWM startup rule.
   - `HC_HAL_UART_ModuleInit()` -> local runtime UART dispatch init, if needed.
5. Replace `HC_HAL_UART_RegisterRxCallback(...)` calls with local runtime callback registration.
6. Preserve the rest of the module init order unless a DriverLib dependency forces a documented reorder.
7. Run searches and build checks.
8. Update `agent/agent.md` with status, files touched, verification result, and blockers.

## Acceptance Criteria

Plan2 is done only when all of these are true:

- `hc-team/app/system/sys_init.c` no longer includes any `hc_hal_*.h`.
- `SysInit()` does not call `HC_HAL_*_Init`, `HC_HAL_PWM_Start`, or `HC_HAL_UART_ModuleInit`.
- `SYSCFG_DL_init()` or the approved SysConfig init path is the root peripheral initialization call.
- UART RX callbacks are registered through the local runtime boundary, not through `HC_HAL_UART_RegisterRxCallback`.
- Required NVIC lines are enabled directly or documented as already handled by SysConfig.
- Startup order remains readable and preserves existing module behavior.
- Any PWM startup dependency is resolved or explicitly marked blocked; it is not silently broken.

## Verification

Required searches:

```powershell
rtk rg -n "hc_hal|HC_HAL_|UART_CH_|DMA_CH_" hc-team/app/system/sys_init.c
rtk rg -n "SYSCFG_DL_init|NVIC_EnableIRQ|__enable_irq" hc-team/app/system/sys_init.c
```

Required build check:

- Build the CCS project if available.
- If CCS build cannot be run, record that explicitly in `agent/agent.md`.

Suggested hardware smoke checks after build:

- Boot reaches the end of `SysInit()` and enters scheduler/main loop.
- UI tick still advances.
- Motor PWM outputs remain at safe zero duty immediately after startup.
- StepMotor, VOFA, and Vision RX callbacks receive bytes after registration.

## Blockers

Stop and record a blocker instead of guessing if:

- `SYSCFG_DL_init()` is unavailable because generated SysConfig files are missing from the active build.
- Direct PWM startup cannot preserve safe zero-duty behavior before Plan4.
- Plan1 runtime does not yet provide callback registration needed by `SysInit()`.
- NVIC enum names differ from the expected MSPM0G3507 names.

## Blocker re-verify (2026-07-13)

- Re-confirmed sys_init acceptance searches and successful Debug link after Plan1 IRQ ownership fix.
