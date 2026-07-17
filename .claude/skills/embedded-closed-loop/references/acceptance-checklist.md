# Lean Acceptance Checklist

Run the six protocol steps from SKILL.md. Use this list only to decide what the focused code read (step 5) must cover for each change type:

- **New/changed public API**: signature matches plan contract; header exposes no DL HAL types, private structs, or writable globals.
- **Dependency removal**: the scan row is the proof; read one former call site to confirm the replacement is real, not a rename.
- **ISR changes**: ISR body is capture/count/flag only; shared state has a critical-section or atomic story.
- **Data-chain changes**: unit, sign, and scaling appear exactly once end to end; no second filter/limit/inversion appeared.
- **Motor/power output**: init is zero-output; limits single-owner; reversal passes through zero; timeout stops output; P0 gate untouched.
- **Deletion tasks**: artifact existed before, absent after (`assert-paths.ps1` when file existence is the postcondition).
- **Tests**: new cases actually fail without the fix (RED evidence in report) and call the production entry point, not only a fake.

Everything else (full Mermaid audit, re-running every boundary test, duplicate clean builds) is intentionally dropped from routine acceptance. Escalate to a full audit only when a row rerun contradicts the report.
