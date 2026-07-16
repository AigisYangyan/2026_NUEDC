---
name: reasonix-embedded-execute
description: Execute an approved REASONIX construction task for NUEDC embedded rewrites with strict layering, upstream/downstream inspection, tests first, hardware safety, real postcondition checks, and topology synchronization. Use when implementing a planned Driver, Middleware, Service, Task, scheduler, ISR, or migration step.
---

# REASONIX Embedded Execute

This is a **REASONIX worker** skill. Execute only one Codex-approved construction task at a time. Treat the task's file boundaries and stop conditions as mandatory.

For a ready-to-use contractor instruction, load [reasonix-worker-prompt.md](references/reasonix-worker-prompt.md) and append the approved task.

## Before Editing

1. Read `AGENTS.md`, the active plan task, and `agent/api_architecture_topology.md`.
2. Re-read every allowed file plus target callers, producers, consumers, initialization, stop path, ISR path, and tests.
3. Run the task baseline. If it differs from the plan, stop and report the drift.
4. Restate the abstraction, hidden state, owning layer, data units, direction, timing, and safety state.

## Execute

1. Write or run the failing reproduction first.
2. Make the smallest implementation that satisfies the public contract.
3. Keep ISR work to capture, buffering, counters, and flags. Upper layers must pull.
4. Keep each data transformation in one owner. Do not repeat filtering, sign correction, scaling, integration, or limits.
5. For motors and power devices, preserve zero-output initialization, single-owner limits, slew, reversal-through-zero, and timeout-stop; prove them with host tests (hardware bring-up is user-owned).
6. Do not add speculative wrappers, status codes, null checks, recovery states, or generic frameworks.

## Prove The Result

Run the task commands and then verify the behavior they were intended to cause. Follow [failure-patterns.md](references/failure-patterns.md).

Mandatory rules:

- Never use ignored errors (`-command`, `|| true`, silent exception handling) in an acceptance path.
- Never equate exit code 0 with success. Check files, symbols, counters, output values, dependencies, or hardware observations.
- Rebuild tests from source before running them; stale binaries are not evidence.
- Test production behavior where practical. A fake only proves the consumer contract, not the real ISR, critical section, DMA, or Driver implementation.
- On this project, launch terminal commands through `rtk`; use commands valid for the active Windows shell.

## Complete The Task

Update `agent/api_architecture_topology.md` only after proof:

- Add or remove the actual API edge direction.
- Update violation status without closing partially verified work.
- Record commands plus observable postconditions.

There is no separate self-check stage (removed 2026-07-16). Construction ends with one compact completion report handed directly to Codex acceptance:

- One line per plan evidence row: `E0x: <command> -> exit <n>, <observed one-line result>`.
- Changed files list; status `CONSTRUCTION_DONE` or `CONSTRUCTION_BLOCKED <reason>`.

Do not write a separate evidence-package document, do not re-run passing checks a second time, and do not report "all complete" while any required row remains open.
