# Plan7: Common Type Removal

## Source And Status

- Source outline: `agent/general_plan_in_all.md`, Phase 7.
- Source handoff: `agent/agent.md`, Plan List `P6`.
- Dependency: Plan5 and Plan6 should be complete so remaining `HC_*` usage is compatibility/common-type cleanup, not active HAL wrapper calls.
- Purpose: remove `hc_common.h` and custom `HC_*` aliases from all `hc-team` public headers and source files, replacing them with standard C99 types and small module-local status enums where needed.
- Status rule: completing this document is not completing `P6`. Mark `P6` done only after code is implemented, built, and checked against the acceptance criteria below.
- Implementation status (2026-07-13): **done** (`agent.md` Plan List `P6`).

## Design Answers Before Coding

1. Abstraction: modules should expose their own domain contracts using standard C types, not a project-wide compatibility typedef layer.
2. Hidden details: error-code choices, null checks, and module-specific status conventions stay inside each module or its own public enum.
3. Module boundary: do not introduce a replacement `hc_common` header. Use standard C headers directly and keep status conventions consistent inside each module.

## Current Inventory

Current residual files are expected to include these groups:

- Scheduler/UI: `run_registry.c`, `task_scheduler.c/h`, `vofa_register.h`, `menu_core.c/h`
- App tasks: `task_groups.c`, `task1.c/h`, `track_follow.c`, `uart_stress.c/h`
- Platform 2D: `2DPlatform_LaserStrike.c`, `stepmotor_bus.c/h`, `vision_bus.c/h`, `vision_coord.c/h`
- Drivers: `key.c/h`, `oled_hardware_i2c.c/h`, `mpu6050.c/h`, `emm42.c/h`

Refresh this list before editing:

```powershell
rtk rg -l "hc_common|HC_U8|HC_U16|HC_U32|HC_S8|HC_S16|HC_S32|HC_Bool_e|HC_TRUE|HC_FALSE|HC_NULL_PTR|HC_Error_e|HC_HAL_OK|HC_HAL_ERR|HC_ERR_" hc-team
```

## Replacement Standard

Use this direct mapping unless a module-specific enum is clearer.

| Old name | Replacement |
| --- | --- |
| `HC_U8` | `uint8_t` |
| `HC_U16` | `uint16_t` |
| `HC_U32` | `uint32_t` |
| `HC_S8` | `int8_t` |
| `HC_S16` | `int16_t` |
| `HC_S32` | `int32_t` |
| `HC_CHAR` | `char` |
| `HC_Bool_e` | `bool` |
| `HC_TRUE` | `true` |
| `HC_FALSE` | `false` |
| `HC_NULL_PTR` | `NULL` |
| `HC_NULL_FN` | `NULL` |
| `HC_VOID` | `void` |
| `HC_LOCAL` | `static` |
| `HC_WEAK` | compiler-specific weak attribute only where still required |

Error/status replacement:

| Old status | Preferred replacement |
| --- | --- |
| `HC_HAL_OK` | `true`, `0`, or module enum success value |
| `HC_HAL_ERR_INVALID` | module enum invalid-argument value or `false` |
| `HC_HAL_ERR_NULL_PTR` | module enum invalid-argument/null value or `false` |
| `HC_HAL_ERR_TIMEOUT` | module enum timeout value or `false` |
| `HC_ERR_BUSY` | module enum busy value or `false` |
| `HC_ERR_NOT_READY` | module enum not-ready value or `false` |
| `HC_ERR_UNKNOWN` | module enum I/O error value or `false` |

Do not blindly convert every status-returning function to `bool`. If callers distinguish busy, timeout, invalid, and not-ready, define a small module-local enum such as `StepmotorBus_Status_e`, `Oled_Status_e`, or `Mpu6050_Status_e`.

## Include Standard

- Remove `#include "hc_common.h"` from every `hc-team` file.
- Add direct standard includes where needed:
  - `#include <stdint.h>` for fixed-width integer types.
  - `#include <stdbool.h>` for `bool`, `true`, `false`.
  - `#include <stddef.h>` for `NULL` and `size_t`.
- Do not include headers from `sdk/hal/hc_common_header`.
- Do not create `hc_types.h`, `compat.h`, `common_legacy.h`, or any new compatibility shim that only renames `HC_*`.

## Public Header Standard

Public headers must be cleaned first enough to compile without `sdk/hal/hc_common_header`.

Priority public headers:

- `hc-team/app/scheduler/task_scheduler.h`
- `hc-team/app/scheduler/vofa_register.h`
- `hc-team/app/ui/oled/menu_core.h`
- `hc-team/app/tasks/platform_2d/stepmotor_bus.h`
- `hc-team/app/tasks/platform_2d/vision_bus.h`
- `hc-team/app/tasks/platform_2d/vision_coord.h`
- `hc-team/app/tasks/task1/task1.h`
- `hc-team/app/tasks/uart_stress/uart_stress.h`
- `hc-team/driver/key/key.h`
- `hc-team/driver/oled/oled_hardware_i2c.h`
- `hc-team/driver/MPU6050/mpu6050.h`
- `hc-team/driver/step_motor/emm42.h`

For each header:

- Replace exposed `HC_*` types with standard C types or module enums.
- Include the standard headers that declare those types.
- Keep existing API names unless changing a status type forces a small, coordinated source update.
- Preserve binary data widths exactly for UART/I2C protocols.

## Module-Specific Guidance

Key:

- Convert key state and event returns from `HC_Bool_e` to `bool`.
- Preserve active-low/debounce behavior from Plan3.

OLED:

- Prefer a small `Oled_Status_e` if caller-visible errors remain important.
- Preserve command/data packet behavior from Plan6.
- If keeping integer status, document `0` as success and negative as error.

MPU6050:

- Preserve `MPU6050_Init()` ability to report `WHO_AM_I` failure separately if callers use it.
- Keep read APIs' behavior where communication failure leaves previous data unchanged.

Stepmotor bus:

- Preserve busy/not-ready/invalid distinction if control scheduling uses it.
- Keep frame byte widths exact: command frames are byte arrays, not signed char data.

Vision:

- Preserve `VisionCoord_GetLatest()` boolean semantics and timestamp types.

Menu/UI:

- Convert dirty/action booleans to `bool`; preserve menu action callback behavior.

## Execution Steps

1. Refresh the residual inventory:

```powershell
rtk rg -n "hc_common|HC_U8|HC_U16|HC_U32|HC_S8|HC_S16|HC_S32|HC_Bool_e|HC_TRUE|HC_FALSE|HC_NULL_PTR|HC_Error_e|HC_HAL_OK|HC_HAL_ERR|HC_ERR_" hc-team
```

2. Clean public headers first, one module group at a time.
3. Update the corresponding `.c` files for each cleaned header.
4. Replace `HC_Bool_e` with `bool`, then replace integer aliases, then replace null/status macros.
5. For status-returning APIs, choose `bool`, `int`, or a small module enum before editing call sites.
6. Run focused searches after each module group.
7. Build after each large group, especially after public header changes.
8. Run final acceptance searches and the Debug build.
9. Update `agent/agent.md` with status, files touched, verification result, and blockers.

## Acceptance Criteria

Plan7 is done only when all of these are true:

- No `#include "hc_common.h"` remains in `hc-team`.
- No `HC_U8`, `HC_U16`, `HC_U32`, `HC_S8`, `HC_S16`, `HC_S32`, `HC_Bool_e`, `HC_TRUE`, `HC_FALSE`, `HC_NULL_PTR`, `HC_Error_e`, `HC_HAL_OK`, `HC_HAL_ERR_*`, or `HC_ERR_*` remains in `hc-team`.
- All `hc-team` public headers compile without `sdk/hal/hc_common_header`.
- Migrated files include `stdint.h`, `stdbool.h`, and `stddef.h` directly when needed.
- Function return conventions are consistent inside each module.
- No compatibility typedef header was introduced to hide old `HC_*` names.
- The Debug build succeeds.

## Verification

Required searches:

```powershell
rtk rg -n "hc_common|HC_U8|HC_U16|HC_U32|HC_S8|HC_S16|HC_S32|HC_Bool_e|HC_TRUE|HC_FALSE|HC_NULL_PTR|HC_Error_e|HC_HAL_OK|HC_HAL_ERR|HC_ERR_" hc-team
rtk rg -n "sdk/hal/hc_common_header|hc_common_header" hc-team .cproject project/mspm0/.cproject
```

Required build check:

```powershell
rtk make -C Debug all
```

Suggested clean verification before closing the phase:

```powershell
rtk make -C Debug clean all
```

Suggested smoke checks after build:

- Key/menu navigation still works.
- OLED init and drawing still work.
- Stepmotor queue and control frames still work.
- Vision coordinate latest-data path still works.
- MPU6050 init and read APIs still behave as before.

## Blockers

Stop and record a blocker instead of guessing if:

- A public API's old `HC_Error_e` values are relied on by callers in a way that needs a new module enum.
- Removing `hc_common.h` reveals hidden include-order dependencies.
- A protocol frame width becomes ambiguous after type replacement.
- A large module cannot be converted without changing behavior beyond type/status naming.
- Build errors imply Plan8 metadata changes are being attempted before Plan7 is clean.

