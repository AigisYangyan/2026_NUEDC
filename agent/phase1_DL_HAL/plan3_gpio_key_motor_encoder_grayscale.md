# Plan3: GPIO, Key, Motor Direction, Encoder, And Grayscale Migration

## Source And Status

- Source outline: `agent/general_plan_in_all.md`, Phase 3.
- Source handoff: `agent/agent.md`, Plan List `P2`.
- Dependency: Plan1 local runtime and Plan2 system init must be in place.
- Purpose: remove GPIO virtual pin usage from `hc-team` modules that read keys, drive motor direction pins, read grayscale sensors, or consume encoder totals.
- Status rule: completing this document is not completing `P2`. Mark `P2` done only after code is implemented, built, and checked against the acceptance criteria below.
- Implementation status (2026-07-13): **implemented**; `agent.md` Plan List `P2` marked `done` after Debug build and acceptance searches.

## Design Answers Before Coding

1. Abstraction: application modules need stable domain operations: key pressed state, motor direction pin state, encoder left/right totals, and grayscale bitmap bits.
2. Hidden details: SysConfig GPIO port/pin macro names, DriverLib GPIO calls, GPIO interrupt pending flags, and encoder quadrature state stay inside board-specific runtime or the owning driver source files.
3. Module boundary: do not recreate a generic GPIO HAL or virtual pin table. Each driver should use either its own small board-specific helper or the Plan1 runtime API.

## Scope

Allowed implementation work:

- `hc-team/driver/key/key.h`
- `hc-team/driver/key/key.c`
- `hc-team/driver/motor/motor.h`
- `hc-team/driver/motor/motor.c`
- `hc-team/driver/encoder/encoder.h`
- `hc-team/driver/encoder/encoder.c`
- `hc-team/app/tasks/track_follow/track_follow.c`
- `hc-team/app/tasks/track_follow/track_follow.h`, only if the public comment or type contract must be clarified
- `hc-team/app/tasks/task1/task1.c`, only to remove stale comments such as references to `hc_hal_gpio.c`
- `hc-team/driver/mspm0_runtime/mspm0_runtime.h`
- `hc-team/driver/mspm0_runtime/mspm0_runtime.c`, only if a tiny standard-C getter/helper is needed for encoder totals

Out of scope:

- Do not migrate PWM duty output in this plan except for direction-pin interlock needed by `Motor_SetPwm()`. PWM removal belongs to Plan4.
- Do not migrate UART, I2C, OLED, MPU6050, AT24Cxx, or common `HC_*` aliases.
- Do not delete `sdk/hal`, remove include paths, or edit generated SysConfig files.
- Do not edit `project/mspm0/board.syscfg` unless hardware mapping is proven wrong and explicitly approved.
- Do not introduce new names beginning with `HC_HAL_`, `hc_hal`, `VPIN_`, or a generic `BoardGpio_*` virtual-pin layer.

## Required Mapping

Use these SysConfig mappings as the migration standard.

Keys:

| Key | Old virtual pin | SysConfig port | SysConfig pin |
| --- | --- | --- | --- |
| `KEY_ID_K1` | `VPIN_K1` | `GPIO_GRP_KEY_K1_PORT` | `GPIO_GRP_KEY_K1_PIN` |
| `KEY_ID_K2` | `VPIN_K2` | `GPIO_GRP_KEY_K2_PORT` | `GPIO_GRP_KEY_K2_PIN` |
| `KEY_ID_K3` | `VPIN_K3` | `GPIO_GRP_KEY_K3_PORT` | `GPIO_GRP_KEY_K3_PIN` |
| `KEY_ID_K4` | `VPIN_K4` | `GPIO_GRP_KEY_K4_PORT` | `GPIO_GRP_KEY_K4_PIN` |

Motor direction pins:

| Motor side | Semantic pin | Old virtual pin | SysConfig pin |
| --- | --- | --- | --- |
| Left | forward-high pin | `VPIN_MOTOR_L2` | `GPIO_GRP_MOTOR_BIN2_PIN` |
| Left | reverse-high pin | `VPIN_MOTOR_L1` | `GPIO_GRP_MOTOR_BIN1_PIN` |
| Right | forward-high pin | `VPIN_MOTOR_R1` | `GPIO_GRP_MOTOR_AIN1_PIN` |
| Right | reverse-high pin | `VPIN_MOTOR_R2` | `GPIO_GRP_MOTOR_AIN2_PIN` |

Grayscale sensors:

| Bitmap bit | Old virtual pin | SysConfig pin |
| --- | --- | --- |
| bit 0 | `VPIN_GRAY_0` | `GPIO_GRP_GRAYSCALE_PIN_IN1_PIN` |
| bit 1 | `VPIN_GRAY_1` | `GPIO_GRP_GRAYSCALE_PIN_IN2_PIN` |
| bit 2 | `VPIN_GRAY_2` | `GPIO_GRP_GRAYSCALE_PIN_IN3_PIN` |
| bit 3 | `VPIN_GRAY_3` | `GPIO_GRP_GRAYSCALE_PIN_IN4_PIN` |
| bit 4 | `VPIN_GRAY_4` | `GPIO_GRP_GRAYSCALE_PIN_IN5_PIN` |
| bit 5 | `VPIN_GRAY_5` | `GPIO_GRP_GRAYSCALE_PIN_IN6_PIN` |
| bit 6 | `VPIN_GRAY_6` | `GPIO_GRP_GRAYSCALE_PIN_IN7_PIN` |
| bit 7 | `VPIN_GRAY_7` | `GPIO_GRP_GRAYSCALE_PIN_IN8_PIN` |

Encoder totals:

| Total | Runtime source |
| --- | --- |
| left total | `Mspm0Runtime_GetEncoderCounts(&left, &right)` |
| right total | `Mspm0Runtime_GetEncoderCounts(&left, &right)` |

## Key Driver Standard

- Remove `#include "hc_hal_gpio.h"` from `key.h` and `key.c`.
- Include `ti_msp_dl_config.h` and the required DriverLib GPIO header only in `key.c`, not in `key.h`.
- Keep `Key_NotifyIrq(Key_Id_e key)` as the public interrupt notification entry used by the Plan1 runtime.
- Remove the legacy compatibility function `HC_HAL_GPIO_Callback(HC_HAL_GPIO_VPin_e vpin)`.
- Preserve debounce timing, event state transitions, and active-low key semantics.
- Read the pin state with `DL_GPIO_readPins(port, pin)`.
- A pressed key remains the electrical low state if the old code treated `HC_PIN_RESET` as pressed.
- Invalid key IDs should keep the current safe default behavior.

## Motor Direction Standard

- Remove `#include "hc_hal_gpio.h"` from motor files.
- Do not expose SysConfig macros, DriverLib types, or GPIO register pointers in `motor.h`.
- Replace `HC_HAL_GPIO_SetPin` and `HC_HAL_GPIO_ResetPin` with private helpers in `motor.c`, for example `motor_set_dir_pin(...)` and `motor_clear_dir_pin(...)`.
- Direction behavior must stay unchanged:
  - Positive left PWM: `BIN2` high, `BIN1` low.
  - Negative left PWM: `BIN2` low, `BIN1` high.
  - Positive right PWM: `AIN1` high, `AIN2` low.
  - Negative right PWM: `AIN1` low, `AIN2` high.
  - Zero command: both direction pins low for that motor.
  - Brake command: both direction pins high for that motor.
- Plan3 may leave `HC_HAL_PWM_*` references in place if PWM migration is deferred to Plan4, but it must not leave any `HC_HAL_GPIO_*` or `VPIN_*` references in the motor module.

## Encoder Standard

- Remove `#include "hc_hal_gpio.h"` from encoder files.
- Replace `HC_HAL_GPIO_GetEncoderCounts(&left_total, &right_total)` with the Plan1 runtime getter.
- Preserve signed total semantics expected by `Encoder_UpdateSample()`.
- Do not move quadrature counting back into `encoder.c` if the runtime already owns GPIO interrupts.
- Update comments that still name `HC_HAL_GPIO_GetEncoderCounts()` or `hc_hal_gpio.c`.

## Track Follow And Grayscale Standard

- Remove `#include "hc_hal_gpio.h"` from `track_follow.c`.
- Replace the virtual-pin table with a private pin table or a small switch using `GPIO_GRP_GRAYSCALE_PORT` and `GPIO_GRP_GRAYSCALE_PIN_IN1_PIN` through `GPIO_GRP_GRAYSCALE_PIN_IN8_PIN`.
- Preserve bitmap ordering exactly: IN1 maps to bit 0, IN8 maps to bit 7.
- Preserve active-high or active-low interpretation exactly as the old `HC_HAL_GPIO_ReadPin` path produced it. If the code currently sets a bitmap bit when the read state is set, keep that behavior.
- Keep grayscale sampling side-effect free: one sample updates the stored bitmap and related derived state only through the existing track-follow APIs.

## Execution Steps

1. Capture current GPIO dependencies:

```powershell
rtk rg -n "hc_hal_gpio|HC_HAL_GPIO|VPIN_" hc-team/driver/key hc-team/driver/motor hc-team/driver/encoder hc-team/app/tasks/track_follow hc-team/app/tasks/task1
```

2. Migrate key read and IRQ notification path.
3. Migrate motor direction pins while leaving PWM duty output to Plan4 if needed.
4. Migrate encoder total readout to `Mspm0Runtime_GetEncoderCounts()`.
5. Migrate grayscale sensor reads and preserve bitmap bit ordering.
6. Remove stale HAL comments in touched files.
7. Run acceptance searches and the Debug build.
8. Update `agent/agent.md` with status, files touched, verification result, and any blockers.

## Acceptance Criteria

Plan3 is done only when all of these are true:

- `hc-team/driver/key`, `hc-team/driver/motor`, `hc-team/driver/encoder`, and `hc-team/app/tasks/track_follow` compile without including `hc_hal_gpio.h`.
- No `HC_HAL_GPIO_*` names remain in those modules.
- No `VPIN_*` names remain in those modules.
- Key debounce behavior and active-low pressed semantics are unchanged.
- Motor direction mapping matches the old comments and `project/mspm0/board.syscfg`.
- `Motor_Brake()` still drives both H-bridge direction pins high for the selected motor.
- Encoder totals are read through the local runtime and `Encoder_UpdateSample()` still receives left/right totals.
- Track-follow grayscale bitmap ordering remains IN1 to bit 0 through IN8 to bit 7.
- No generic GPIO HAL, virtual pin table, or broad board abstraction was added.

## Verification

Required searches:

```powershell
rtk rg -n "hc_hal_gpio|HC_HAL_GPIO|VPIN_" hc-team/driver/key hc-team/driver/motor hc-team/driver/encoder hc-team/app/tasks/track_follow hc-team/app/tasks/task1
rtk rg -n "GPIO_GRP_KEY|GPIO_GRP_MOTOR|GPIO_GRP_GRAYSCALE|Mspm0Runtime_GetEncoderCounts|DL_GPIO_" hc-team/driver/key hc-team/driver/motor hc-team/driver/encoder hc-team/app/tasks/track_follow
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

- Press each key and confirm debounce/event behavior matches the previous firmware.
- Command positive, negative, zero, and brake on both motors while checking direction pins or wheel direction.
- Rotate each encoder and confirm signed left/right totals change in the expected direction.
- Present known grayscale patterns and confirm bitmap ordering matches IN1 through IN8.

## Blockers

Stop and record a blocker instead of guessing if:

- Generated SysConfig macro names differ from the mapping table above.
- Direction pin mapping conflicts with the physical motor wiring during smoke test.
- Encoder signs invert after switching the getter path.
- Track-follow logic depends on an undocumented electrical inversion that is not visible from current code.
- Migrating motor direction without PWM migration creates an unsafe transient output state.

