# Plan8: CCS Metadata And sdk/hal Detach

## Source And Status

- Source outline: `agent/general_plan_in_all.md`, Phase 8.
- Source handoff: `agent/agent.md`, Plan List `P7`.
- Dependency: Plan7 must be complete. `hc-team` must no longer include `hc_hal_*`, `hc_common.h`, `hc_common_header`, or `HC_*` compatibility types.
- Purpose: remove `sdk/hal` include paths and build participation from CCS metadata and generated Debug make rules without disturbing TI SDK, SysConfig, startup, linker, or target configuration.
- Status rule: completing this document is not completing `P7`. Mark `P7` done only after metadata is edited, a clean build passes, and the acceptance searches are clean.
- Implementation status (2026-07-13): **done** (`agent.md` Plan List `P7`). `sdk/hal` sources remain on disk for Plan9.

## Design Answers Before Coding

1. Abstraction: CCS metadata should describe the active firmware build only; unused wrapper-layer sources should not participate.
2. Hidden details: generated Debug makefile fragments are build artifacts; root/project `.cproject` and `.project` files are the durable project configuration.
3. Module boundary: detach only custom `sdk/hal`. Do not remove TI SDK includes, SysConfig generated files, startup files, linker scripts, target configs, or non-HAL project resources.

## Preconditions

Do not begin Plan8 until these searches pass:

```powershell
rtk rg -n "hc_hal|HC_HAL_|VPIN_|UART_CH_|I2C_CH_|DMA_CH_|hc_common|HC_U8|HC_U16|HC_U32|HC_S8|HC_S16|HC_S32|HC_Bool_e|HC_Error_e" hc-team
```

Expected result: no matches.

If matches remain, return to Plan7 or earlier phase. Do not remove include paths while active code still depends on them.

## Current Metadata Inventory

Expected metadata/build references before this phase:

- `.cproject`
  - `${PROJECT_ROOT}/sdk/hal/inc`
  - `${PROJECT_ROOT}/sdk/hal/cfg`
  - `${PROJECT_ROOT}/sdk/hal/hc_common_header`
- `project/mspm0/.cproject`
  - `${PROJECT_ROOT}/../../sdk/hal/inc`
  - `${PROJECT_ROOT}/../../sdk/hal/cfg`
  - `${PROJECT_ROOT}/../../sdk/hal/hc_common_header`
- `Debug/sources.mk`
  - `sdk/hal/cfg`
  - `sdk/hal/src`
- `Debug/makefile`
  - object files and included make fragments for `sdk/hal/cfg` and `sdk/hal/src`

Refresh inventory before editing:

```powershell
rtk rg -n "sdk/hal|hc_hal|hc_common|hal/inc|hal/cfg|hal/hc_common_header|hal/src" .cproject project/mspm0/.cproject project/mspm0/.project Debug/makefile Debug/sources.mk Debug/objects.mk
```

## Scope

Allowed implementation work:

- `.cproject`
- `project/mspm0/.cproject`
- `project/mspm0/.project`, only if a linked `sdk` resource exists solely to compile `sdk/hal`
- `Debug/makefile`
- `Debug/sources.mk`
- `Debug/objects.mk`
- `Debug/sdk/hal/**` make fragments, if present
- Any generated Debug make fragment that still lists `sdk/hal`
- `agent/agent.md` status/log updates

Out of scope:

- Do not delete or edit TI SDK files under `C:/ti`.
- Do not edit `project/mspm0/board.syscfg` unless SysConfig itself requires regeneration for an unrelated reason.
- Do not remove `ti_msp_dl_config.c/.h`, startup files, linker command files, target configs, or SysConfig product metadata.
- Do not delete `sdk/hal` source files in this plan. Deletion/quarantine belongs to Plan9.
- Do not modify application behavior.

## Metadata Edit Standard

CCS project files:

- Remove only `sdk/hal` include path entries.
- Keep project root, `hc-team`, Debug, TI SDK, CMSIS, and SysConfig include paths.
- Preserve XML structure, option IDs, toolchain IDs, and unrelated values.
- Prefer structured, minimal XML edits. Do not reformat the whole file.

Debug make files:

- Remove `sdk/hal/cfg` and `sdk/hal/src` source directory entries.
- Remove `sdk/hal` object files from `ORDERED_OBJS`, clean lists, and dependency includes.
- Remove `-include sdk/hal/cfg/subdir_vars.mk`, `-include sdk/hal/src/subdir_vars.mk`, `-include sdk/hal/cfg/subdir_rules.mk`, and `-include sdk/hal/src/subdir_rules.mk`.
- Remove stale `Debug/sdk/hal/**` make fragments only after confirming their resolved paths are inside `Debug/sdk/hal`.
- Keep SysConfig-generated build steps and all `hc-team` object entries.

Linked resource rule:

- If `project/mspm0/.project` links `sdk` only for `sdk/hal`, remove or update that link.
- If the link is still needed for other non-HAL code, keep it and document why.

## Execution Steps

1. Prove Plan7 is complete:

```powershell
rtk rg -n "hc_hal|HC_HAL_|VPIN_|UART_CH_|I2C_CH_|DMA_CH_|hc_common|HC_U8|HC_U16|HC_U32|HC_S8|HC_S16|HC_S32|HC_Bool_e|HC_Error_e" hc-team
```

2. Inventory metadata references:

```powershell
rtk rg -n "sdk/hal|hc_hal|hc_common|hal/inc|hal/cfg|hal/hc_common_header|hal/src" .cproject project/mspm0/.cproject project/mspm0/.project Debug/makefile Debug/sources.mk Debug/objects.mk
```

3. Remove `sdk/hal` include paths from `.cproject` and `project/mspm0/.cproject`.
4. Inspect `project/mspm0/.project` and remove only HAL-specific linked resources if present.
5. Remove `sdk/hal` source/build references from Debug make files.
6. Delete stale Debug `sdk/hal` make fragments only if they are generated build artifacts under `Debug/sdk/hal`.
7. Run metadata acceptance searches.
8. Run a clean Debug build.
9. Update `agent/agent.md` with status, files touched, verification result, and blockers.

## Acceptance Criteria

Plan8 is done only when all of these are true:

- `.cproject` does not mention `sdk/hal`, `hc_hal`, or `hc_common`.
- `project/mspm0/.cproject` does not mention `sdk/hal`, `hc_hal`, or `hc_common`.
- `project/mspm0/.project` does not link `sdk` for the purpose of compiling `sdk/hal`; if a non-HAL link remains, the reason is recorded.
- Debug make files do not list `sdk/hal/cfg`, `sdk/hal/src`, or `hc_hal_*` objects.
- SysConfig, TI SDK, startup, linker, target config, and generated `ti_msp_dl_config.*` files are still present and active.
- `rtk make -C Debug clean all` succeeds without missing include or unresolved symbol errors.
- No application source behavior changed in this phase.

## Verification

Required searches:

```powershell
rtk rg -n "sdk/hal|hc_hal|hc_common|hal/inc|hal/cfg|hal/hc_common_header|hal/src" .cproject project/mspm0/.cproject project/mspm0/.project Debug/makefile Debug/sources.mk Debug/objects.mk
rtk rg -n "hc_hal|HC_HAL_|VPIN_|UART_CH_|I2C_CH_|DMA_CH_|hc_common|HC_U8|HC_U16|HC_U32|HC_S8|HC_S16|HC_S32|HC_Bool_e|HC_Error_e" hc-team
```

Required build check:

```powershell
rtk make -C Debug clean all
```

Suggested post-build check:

```powershell
rtk powershell -NoProfile -Command "Get-Item -LiteralPath Debug/NUEDC.out,Debug/NUEDC.hex | Select-Object Name,Length,LastWriteTime"
```

## Blockers

Stop and record a blocker instead of guessing if:

- Any `hc-team` source still requires `sdk/hal` include paths.
- CCS regenerates Debug make files with `sdk/hal` after metadata edits, meaning a durable project reference remains.
- Removing a linked `sdk` resource would also remove non-HAL active code.
- Clean build fails with missing TI SDK, SysConfig, startup, or linker artifacts.
- XML edits would require broad reformatting or changing unrelated CCS tool options.

