---
name: reasonix-embedded-plan
description: Generate a bounded REASONIX construction plan for NUEDC embedded module rewrites. Use when planning or revising Driver, Middleware, App Service, App Task, scheduler, ISR, hardware ownership, or architecture-debt migration work before any production code is changed.
---

# REASONIX Embedded Plan

This is a **Codex-owned** skill. Create a small, evidence-backed construction order for REASONIX. Do not modify production code while this skill is active, and do not delegate final plan authority to the construction worker.

## Gather Evidence

1. Read root `AGENTS.md` and `agent/api_architecture_topology.md`.
2. Read the target implementation, public header, initialization path, all callers, upstream producers, downstream consumers, tests, and build metadata.
3. Record the current dependency chain and data transformations. Do not infer behavior from names or headers alone.
4. Save a baseline build, test, and dependency-scan result. Separate existing debt from new violations.

## Define The Boundary

Answer before creating tasks:

- What is the abstraction?
- What must remain hidden?
- Which layer owns it?
- What hardware resource, state, buffer, ISR, or data transformation has one owner?
- What behavior must remain unchanged?

Stop if the answer requires a lower layer to call upward, a Task to touch Driver/HAL, Middleware to know hardware, or one module to edit another module's state.

## Build The Construction Order

Create 1 to 3 independently verifiable tasks. Use the exact fields in [construction-order.md](references/construction-order.md).

For every task:

- Limit `allowed_files` and `forbidden_files` to concrete file paths. Do not use `**`, directory wildcards, or inferred future paths without first marking them as files to create.
- Put tests or a failing reproduction before implementation.
- Provide commands and observable postconditions. A zero exit code alone is never acceptance.
- State topology nodes, API edges, violation entries, and status text that may change.
- Copy every topology violation ID and description from the current table. Never infer or rename an ID from a phase number.
- Do not plan hardware validation rows or hardware gates (user decree 2026-07-16: all board bring-up is user-owned and outside acceptance). For motor/power tasks, still plan software-verifiable safety rows: init-to-safe-output, limits, slew, reversal-through-zero, timeout-stop — provable by host tests.
- Define the evidence rows REASONIX must report and Codex will verify. Do not invent new acceptance requirements after construction begins unless the baseline changed.

Evidence-row budget (since 2026-07-16, to cap token/time cost):

- At most 6 E rows per task; one command per row. Merge dependency scans into one `rg` alternation per row.
- Exactly one firmware-build row per task, executed once by the builder; Codex reuses it unless the diff touches build metadata.
- Flow is three stages only: Codex plan → REASONIX construct (reports `CONSTRUCTION_DONE`/`CONSTRUCTION_BLOCKED` with one line per E row) → Codex lean acceptance (`CODEX_ACCEPTED`/`CODEX_SOFTWARE_ACCEPTED`/`CODEX_REJECTED`). Do not plan a self-check stage or evidence-package document.

## Review The Plan

Reject the plan when it:

- Bundles unrelated modules or skips consumers.
- Copies a known architecture violation for compatibility.
- Claims hardware direction, timing, PPR, PWM, or safety without measurement.
- Uses broad acceptance such as "build passes" without behavior checks.
- Allows ignored errors, stale binaries, mock-only proof, or topology claims without source evidence.

Write the plan under the existing `agent/<phase>/` structure and update topology only to record the approved future work, not to mark it complete.
