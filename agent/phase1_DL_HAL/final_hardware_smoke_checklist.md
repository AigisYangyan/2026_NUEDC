# Final Hardware Smoke Checklist

## Purpose

This checklist is the final on-board acceptance gate after the custom `sdk/hal`
tree has been removed and the firmware builds only through `hc-team`, SysConfig,
and TI DriverLib.

## Preconditions

- Build artifact: `Debug/NUEDC.out` or `Debug/NUEDC.hex` from a clean build.
- Target board: TI MSPM0G3507 project hardware.
- CCS/debugger can flash and reset the target.
- Required peripherals connected: keys, OLED, motors, encoders, grayscale sensor,
  VOFA serial link, vision serial link, stepmotor serial link, MPU6050, and
  AT24C02 if populated.

## Required Checks

| ID | Item | Expected Result | Result |
| --- | --- | --- | --- |
| H1 | Flash `Debug/NUEDC.out` or `Debug/NUEDC.hex` | Flash succeeds; target resets into firmware. | Not run in Codex environment |
| H2 | Keys K1 to K4 | Each key is detected once per press; no stuck input. | Not run in Codex environment |
| H3 | OLED I2C | OLED initializes and renders the expected UI/menu content. | Not run in Codex environment |
| H4 | Motor PWM and direction | Positive/negative commands drive the expected side and direction; stop command stops output. | Not run in Codex environment |
| H5 | Encoder counts | Counts change with wheel movement and sign matches expected direction. | Not run in Codex environment |
| H6 | Grayscale/track inputs | Sensor bitmap/state changes when line positions change. | Not run in Codex environment |
| H7 | VOFA UART | Telemetry frame is received by VOFA without framing corruption. | Not run in Codex environment |
| H8 | Vision UART | Vision RX messages update parsed coordinate/state data. | Not run in Codex environment |
| H9 | Stepmotor UART | Command transmit succeeds and driver response path remains stable. | Not run in Codex environment |
| H10 | MPU6050 I2C | WHO_AM_I/device init succeeds and 14-byte data reads update IMU values. | Not run in Codex environment |
| H11 | AT24C02 I2C, if populated | Page write/readback succeeds at 7-bit address `0x50`. | Not run in Codex environment |

## Acceptance Rule

Software migration can be accepted after clean build and reference searches pass.
Hardware release can be accepted only after each populated-board item above is
changed from `Not run in Codex environment` to `PASS` with date/operator notes.
