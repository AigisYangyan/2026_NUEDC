---
name: embedded-closed-loop
description: Single-agent closed-loop protocol for 2026_Diansai embedded work — decide, build, and accept one bounded task with frozen evidence rows. Use when planning, implementing, or accepting any Driver, Middleware, App Service, App Task, scheduler, ISR, board-config, or architecture-debt change.
---

# Embedded Closed Loop (decide → build → accept)

One agent owns all three phases (user decree 2026-07-16: REASONIX/GPT contractor removed; no second party exists). Root `AGENTS.md` is the architecture authority; `agent/api_architecture_topology.md` is the sole authority on current state and is mandatory to read before and update after (§14).

## The problem this protocol exists to solve

The old three-party flow bought **independence for free**: the acceptor had not written the code, so it could not rationalize it. Closed-loop deletes that property. Merging the three skills into one without replacing it keeps the ceremony and loses the only mechanism that worked.

The replacement is mechanical, not attitudinal:

> **The task contract — including every evidence row — is committed to git before the first line of production code is written.**

Acceptance then diffs reality against a timestamped contract that cannot be quietly edited to match what got built. Git is the second party. This is not bureaucracy; it is the only thing standing between self-acceptance and self-congratulation. Precedent: during P6 the plan's §4 contract was wrong (declared `void` returns), and only `git show HEAD:<file>` settled whether the plan or the build was at fault.

Corollaries, all enforced by the same commit boundary:

- Discovering mid-build that a row was wrong is **normal**. Amend the contract in a **separate, explicit commit** that states what changed and why, then continue. Never edit a row silently in the same commit as the code that satisfies it.
- A row that cannot be reproduced is a `REJECTED` row, even though the same agent wrote both sides. Reject in writing, fix, rerun.
- Verdicts stay `ACCEPTED` / `REJECTED` (the `CODEX_*` prefix is retired; historical records keep it as period fact and are not rewritten).

## Local workspace facts (2026_Diansai)

- MSPM0G3519 (LQFP-100), SDK 2.11.00.07, ticlang 4.0.4, migrated from `NUEDC`/G3507 — recipe: `agent/MIGRATION_G3507_TO_G3519.md`.
- Single hardware config source: **`board.syscfg` at repo root** (historical plans' `project/mspm0/board.syscfg` no longer exists).
- Firmware build: `rtk make -C Debug all`. Host tests: `make.bat -C tests/host all` — **present and green since HT.T1** (V16 closed; 76 cases as of P6). A report claiming host rows is now checkable, not an automatic reject.
- Encoders are TIMG8/TIMG9 hardware QEI (GROUP1 IRQ serves keys only); stepper bus is physical UART7; MPU6050/I2C_IMU removed; grayscale is 12-channel.
- UART roles: `UART_HOST_LINK` = VOFA on **UART5/PA1/PA0/230400/DMA** (PA0/PA1 not yet routed on the board); `UART_BSL_ENTRY` = **UART0/PA10/PA11/9600/no DMA**, wireless BSL only, **listener unimplemented**.
- **`make.bat` must be launched via the PowerShell tool.** Invoking it as `cmd /d /c ".\make.bat ..."` from Bash silently no-ops and returns a fake exit 0 — a proven evidence-forging trap.
- `rg` is not on the PowerShell PATH; use the Grep tool for scans.

## Phase 1 — DECIDE (no production code)

1. Read `AGENTS.md`, `agent/api_architecture_topology.md`, the target implementation, its public header, init path, stop path, ISR path, every caller, producer, consumer, test, and build metadata. Never infer behavior from names or headers.
2. Record the current dependency chain and data transformations. Separate existing debt from new violations.
3. Answer, and stop if any answer is unclear: abstraction? hidden state? owning layer? who owns each hardware resource, buffer, ISR, and data transformation? what behavior must not change?
4. Write 1–3 independently verifiable tasks using [task-contract.md](references/task-contract.md). Concrete `allowed_files`/`forbidden_files` paths — no globs. Copy topology violation IDs verbatim from the table; a phase label is not an ID.
5. **Evidence-row budget**: ≤6 rows per task, one command per row, exactly one firmware-build row. Merge dependency scans into one alternation per row.
6. No hardware rows. Board bring-up is user-owned and outside acceptance (decree 2026-07-16). Motor/power tasks still carry software-provable safety rows: init-to-safe-output, single-owner limits, slew, reversal-through-zero, timeout-stop.
7. **Commit the plan.** This closes the phase and freezes the contract.

## Phase 2 — BUILD

1. Rerun the baseline. If it differs from the plan, that is drift: stop and amend the contract before coding.
2. Write or run the failing reproduction **first** — it must actually fail on the old implementation.
3. Smallest implementation that satisfies the public contract. Delete the old interface in the same step; no dual-track compatibility paths.
4. ISR does capture, buffering, counters, flags — nothing else. Upper layers pull.
5. One owner per data transformation. Never add a second sign correction, filter, scale, integral, or limit. (Standing example: encoder polarity has exactly one correction point, `encoder.c:41` `s_direction_sign[]`; a second inversion anywhere cancels it.)
6. No speculative wrappers, status codes, null checks on statically-guaranteed objects, recovery states, or generic frameworks.
7. Follow [failure-patterns.md](references/failure-patterns.md). Never use `-` Make prefixes, `|| true`, or silent excepts in an evidence path. Rebuild from source; stale binaries are not evidence.

## Phase 3 — ACCEPT

Run exactly this, in order. Reproduce rows, not vibes.

1. **Diff read**: `git diff`/`git status` against `allowed_files`. Any touch on `forbidden_files` or an unexplained file → reject, skip the rest.
2. **Scans**: rerun every dependency-scan row. Zero-hit rows must be zero; hit lists must match the contract.
3. **Host tests**: run the suite once. Count ≥ baseline + the task's new cases.
4. **Build**: reuse the build row's disclosed output unless the diff touches build metadata or linker layout; then rerun that row minimally (forced relink, not clean rebuild). An incremental "up to date" no-op is never evidence.
5. **Focused code read**: only the hunks carrying the core contract — new signatures, ISR paths, safety states, ownership moves. Use [acceptance-checklist.md](references/acceptance-checklist.md) to pick what to read per change type.
6. **Topology check**: verify only the edges and violation entries named in the contract, against code facts just confirmed.

**Token economy** (decree 2026-07-16): run each verification **at most once** — capture to a log, then grep the log; never rerun to reformat output. Cheap scans (git status, Grep) are always rerun.

**Never weakened:**

- Exit 0 is never success by itself; every row needs its observed postcondition.
- A fake proves the consumer contract, never the real ISR/DMA/Driver boundary.
- Deletion rows must show the artifact present before and absent after (`assert-paths.ps1`).
- No topology item closes without direct source evidence.

## Finish

Commit (scope-limited, Conventional Commits), then add **one** topology log line with measured facts — even a no-change task logs "已复核，无拓扑变化" (§14.4). Delivery note states: what changed, verification results, assumptions, remaining risk, and any hardware step left to the user.
