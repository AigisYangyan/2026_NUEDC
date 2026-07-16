# Plan6: I2C Device Migration

## Source And Status

- Source outline: `agent/general_plan_in_all.md`, Phase 6.
- Source handoff: `agent/agent.md`, Plan List `P5`.
- Dependency: Plan2 system init must call `SYSCFG_DL_init()` so I2C instances are configured by SysConfig before device drivers run.
- Purpose: remove I2C HAL wrapper usage from OLED, MPU6050, and AT24Cxx device drivers while preserving each device protocol.
- Status rule: completing this document is not completing `P5`. Mark `P5` done only after code is implemented, built, and checked against the acceptance criteria below.
- Implementation status (2026-07-13): **done** — code migrated, Debug build passed, acceptance searches passed. Hardware smoke tests not run. AT24 address corrected to 7-bit `0x50`.

## Design Answers Before Coding

1. Abstraction: device drivers need register/byte transactions for their own devices, not a generic HAL channel enum.
2. Hidden details: I2C controller instance, bus wait loops, controller status checks, start/stop sequencing, and timeout handling stay in private source helpers.
3. Module boundary: OLED, MPU6050, and AT24Cxx may each own their device protocol. Share only a tiny board-specific I2C helper if duplication becomes risky.

## Scope

Allowed implementation work:

- `hc-team/driver/oled/oled_hardware_i2c.h`
- `hc-team/driver/oled/oled_hardware_i2c.c`
- `hc-team/driver/MPU6050/mpu6050.h`
- `hc-team/driver/MPU6050/mpu6050.c`
- `hc-team/driver/eeprom/at24cxx.h`
- `hc-team/driver/eeprom/at24cxx.c`
- `hc-team/driver/mspm0_runtime/mspm0_runtime.h`
- `hc-team/driver/mspm0_runtime/mspm0_runtime.c`, only if delay/tick helpers already present are needed by touched device modules
- A new private helper under `hc-team/driver/mspm0_runtime` or `hc-team/driver/i2c_board`, only if it removes meaningful duplication without becoming a generic HAL.

Allowed small adjacent work:

- Replace `hc_hal_systick.h` in touched I2C device modules with `Mspm0Runtime_GetTickMs()` and `Mspm0Runtime_DelayMs()` when the driver needs timestamps or write-cycle waits.
- Keep current `HC_Error_e` and `HC_*` common types only if removing them would expand the scope into Plan7. Do not introduce new compatibility typedefs.

Out of scope:

- Do not migrate UART/DMA in this plan. That belongs to Plan5.
- Do not perform full common-type removal. That belongs to Plan7.
- Do not change OLED drawing APIs, MPU6050 public data layout, AT24Cxx public read/write APIs, or device register constants unless required for correctness.
- Do not edit `project/mspm0/board.syscfg` or generated SysConfig files.
- Do not remove `sdk/hal` include paths or source participation in this plan.

## Required Mapping

Use the generated SysConfig symbols from `ti_msp_dl_config.h`.

I2C buses:

| Device | Old channel | SysConfig instance | Hardware I2C | Bus speed |
| --- | --- | --- | --- | --- |
| OLED SSD1306-style display | `I2C_CH_OLED` / `HC_HAL_I2C_ID_OLED` | `I2C_OLED_INST` | I2C1 | 400000 |
| AT24C02 EEPROM | `I2C_CH_AT24C02` / `HC_HAL_I2C_ID_AT24C02` | `I2C_OLED_INST` | I2C1 | 400000 |
| MPU6050 IMU | `I2C_CH_MPU6050` | `I2C_MPU6050_INST` | I2C0 | 400000 |

Device addresses:

| Device | Current address contract |
| --- | --- |
| OLED | `OLED_I2C_ADDR` is `0x3C` as a 7-bit address |
| MPU6050 | default 7-bit address `0x68`; `WHO_AM_I` expected `0x68` |
| AT24C02 | current code uses `0xA0`; confirm whether DriverLib send APIs expect 7-bit `0x50` or shifted `0xA0` before implementation |

The AT24Cxx address representation is a required checkpoint. Do not silently switch between shifted and 7-bit addressing without checking the DriverLib API and current behavior.

## I2C Transaction Standard

Implement blocking transactions with bounded waits.

Required helper capabilities:

- Write bytes to a 7-bit device address.
- Write one register/memory address byte followed by data bytes.
- Write one register/memory address byte, repeated-start if supported or stop/start if required, then read bytes.
- Return a clear success/failure status.
- Bound every wait loop with a timeout counter or timestamp.
- On failure, issue STOP or cleanup where DriverLib requires it before returning.

Use direct DriverLib APIs and SysConfig instance symbols. The exact DriverLib calls must match the MSPM0 SDK available in this workspace.

## OLED Standard

- Remove `#include "hc_hal_i2c.h"` from OLED files.
- Remove `HC_HAL_I2C_Ch_e` from OLED public structs if it is exposed in `oled_hardware_i2c.h`.
- Bind OLED privately to `I2C_OLED_INST` and address `0x3C`.
- Preserve packet format:
  - command packet prefix `0x00`
  - data packet prefix `0x40`
  - current command sequence bytes
- Preserve bus recovery behavior if practical. If direct recovery is not implemented, document why and keep failure bounded.
- Replace OLED tick reads with `Mspm0Runtime_GetTickMs()` if needed.
- OLED init, clear, show string, draw, and refresh behavior must remain unchanged.

## MPU6050 Standard

- Remove `#include "hc_hal_i2c.h"` from MPU6050 files.
- Remove `HC_HAL_I2C_Ch_e i2c_ch` from `MPU6050_T` unless retaining it is strictly necessary until Plan7; preferred replacement is no bus field or a private enum.
- Bind MPU6050 privately to `I2C_MPU6050_INST`.
- Preserve register protocol:
  - read `REG_WHO_AM_I` and expect `0x68`
  - wake by writing `REG_PWR_MGMT_1 = 0x00`
  - preserve sample rate, accel config, and gyro config writes
  - preserve 14-byte read behavior in `MPU6050_Read_All()`
- Preserve behavior where read API failures leave previous data fields unchanged.
- Replace MPU6050 tick reads with `Mspm0Runtime_GetTickMs()` if needed.

## AT24Cxx Standard

- Remove `#include "hc_hal_i2c.h"` from AT24Cxx files.
- Remove I2C HAL macros from `at24cxx.h`; avoid large macro transaction bodies in the public header.
- Move page-write/read transaction logic into `at24cxx.c`.
- Keep current public API names and bounds checks.
- Preserve AT24C02 constraints:
  - total size 256 bytes
  - page size 8 bytes
  - 8-bit memory address
  - 5 ms write-cycle delay after page writes
- Use `I2C_OLED_INST` unless hardware mapping is corrected in SysConfig.
- Confirm address representation before implementation:
  - If DriverLib expects 7-bit address, use `0x50`.
  - If existing wrapper already passed shifted `0xA0` through to DriverLib unchanged and worked, document the API expectation before preserving it.

## Execution Steps

1. Capture current I2C HAL dependencies:

```powershell
rtk rg -n "hc_hal_i2c|HC_HAL_I2C|I2C_CH_|HC_HAL_I2C_ID|hc_hal_systick|HC_HAL_SYSTICK" hc-team/driver/oled hc-team/driver/MPU6050 hc-team/driver/eeprom
```

2. Inspect the MSPM0 DriverLib I2C APIs available in the local TI SDK headers.
3. Decide whether to use per-device private helpers or one tiny board-specific helper.
4. Implement bounded blocking write and memory read/write helpers.
5. Migrate OLED write path while preserving `0x00` command and `0x40` data packet prefixes.
6. Migrate MPU6050 register write/read helpers and preserve `WHO_AM_I` validation.
7. Migrate AT24Cxx page write/read logic out of public macros and into source.
8. Replace touched device delay/tick calls with `Mspm0Runtime_*` helpers where needed.
9. Remove stale HAL comments in touched files.
10. Run acceptance searches and the Debug build.
11. Update `agent/agent.md` with status, files touched, verification result, and any blockers.

## Acceptance Criteria

Plan6 is done only when all of these are true:

- OLED, MPU6050, and AT24Cxx code compile without `hc_hal_i2c.h`.
- No `HC_HAL_I2C_*`, `I2C_CH_*`, or `HC_HAL_I2C_ID_*` names remain in OLED, MPU6050, or AT24Cxx driver files.
- OLED init and drawing command packet format is preserved.
- MPU6050 `WHO_AM_I`, register writes, and 14-byte read behavior are unchanged.
- AT24Cxx preserves 256-byte capacity, 8-byte page writes, 8-bit memory addressing, and write-cycle delay.
- I2C routines use bounded waits/timeouts and do not spin forever on obvious error states.
- Any shared helper is board-specific and minimal; no generic `HC_HAL_I2C` replacement was introduced.
- The Debug build succeeds.

## Verification

Required searches:

```powershell
rtk rg -n "hc_hal_i2c|HC_HAL_I2C|I2C_CH_|HC_HAL_I2C_ID" hc-team/driver/oled hc-team/driver/MPU6050 hc-team/driver/eeprom
rtk rg -n "I2C_OLED_INST|I2C_MPU6050_INST|DL_I2C_|Mspm0Runtime_GetTickMs|Mspm0Runtime_DelayMs" hc-team/driver/oled hc-team/driver/MPU6050 hc-team/driver/eeprom hc-team/driver/mspm0_runtime
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

- OLED initializes, clears, and displays at least one known string.
- MPU6050 init returns success and `WHO_AM_I` reads `0x68`.
- MPU6050 14-byte all-read updates accel/gyro/temp fields plausibly.
- AT24C02 writes one byte, reads it back, writes across a page boundary, and reads back expected bytes.
- I2C failure or disconnected-device case returns without hanging forever.

## Blockers

Stop and record a blocker instead of guessing if:

- The local MSPM0 DriverLib I2C API sequence is unclear or differs from expected examples.
- AT24Cxx address representation cannot be confirmed.
- OLED bus recovery behavior cannot be mapped safely to DriverLib.
- A device driver public header cannot remove `hc_hal_i2c.h` without triggering broad Plan7 common-type cleanup.
- SysConfig macro names differ from the mapping table above.

