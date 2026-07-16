---
name: reasonix-embedded-accept
description: Codex-side lean acceptance of REASONIX construction results for NUEDC embedded rewrites. Use when reviewing a construction completion report, migration results, topology updates, or hardware-safety evidence. The former REASONIX self-check stage is removed; this is the only acceptance gate.
---

# REASONIX Embedded Accept (Lean)

This is the **Codex-owned single acceptance gate** (flow since 2026-07-16: Codex plan → REASONIX construct → Codex accept). Input is the construction completion report; it is a claim, not proof.

## Lean Protocol — run exactly this, in order

1. **Diff read**: `git diff`/`git status` against the task's allowed_files. Any touch on forbidden_files or unexplained files → reject immediately, skip the rest.
2. **Scans**: rerun the plan's dependency-scan rows yourself (rg, seconds). Zero-hit rows must be zero; hit lists must match the plan.
3. **Host tests**: run the host suite once (`make -C tests/host all`). Count must be ≥ baseline + the task's new cases. Local fact (2026_Diansai, 2026-07-16): `tests/host/` was not migrated from the old NUEDC repo (topology V16); until a task migrates it and re-establishes the green baseline, any report claiming host-test rows is an automatic reject — the suite the report ran does not exist here.
4. **Build**: reuse the builder's fresh-build evidence (exit code + warning delta stated in the report). Rebuild yourself only when the diff touches build metadata, linker layout, or the report lacks a build line.
5. **Focused code read**: read only the hunks that carry the task's core contract (new signatures, ISR paths, safety states, ownership moves) — not the whole file set.
6. **Topology check**: verify only the edges/violation entries named in the task's topology contract against the code facts you just confirmed.

Do not independently re-derive everything; the plan already fixed what matters as E rows. Reproduce E rows, not vibes.

Token economy (user decree 2026-07-16): run each verification command **at most once** — capture output to a log file and grep/count from that log, never re-run to reformat output. Cheap scans (rg, git status, git ls-files: seconds) are always rerun. Expensive rows (host suite, firmware build) reuse the builder's disclosed output by default; rerun only the specific row whose inputs the diff touches, and rerun it minimally (e.g. a forced relink of the final target, not a clean rebuild). An incremental "up to date" no-op is never evidence — if a rerun is triggered, it must actually exercise the changed path.

## Never Weakened (the essence)

- Exit 0 is never success by itself; every row needs its observed postcondition.
- Ignored errors (`|| true`, Make `-`, silent excepts), stale binaries, and fake-only proof of a production ISR/DMA/Driver boundary are rejection causes.
- Hardware validation is out of acceptance scope entirely (user decree 2026-07-16): the user owns all board bring-up; plans carry no hardware rows and acceptance never waits on instruments. What remains verified is the **software-side safety design** of motor/power code — init-to-safe-output, single-owner limits, slew, reversal-through-zero, timeout-stop — because host tests can prove it.
- Same E-row IDs as the plan; disagreements are settled by rerunning the row, not by comparing confidence.

## Decide

- `CODEX_ACCEPTED`: all rows reproduced. Software rows are the entire contract; this verdict is final.
- `CODEX_REJECTED`: cite the exact row, the rerun command, and the mismatch.

(The former `CODEX_SOFTWARE_ACCEPTED` tier is retired — with hardware rows gone it is identical to `CODEX_ACCEPTED`.)

After acceptance: commit (scope-limited), add one topology log line with measured facts. Keep it to one line per fact — no narrative evidence packages.
