# REASONIX And AI Failure Patterns

## Verification Failures

- **Exit-code substitution**: command returns 0 but the intended file, state, or output did not change. Always check the postcondition.
- **Ignored failure**: Make `-` prefixes, `|| true`, silent exceptions, or fallback branches hide the real error. Acceptance paths must fail loudly.
- **Shell mismatch**: Bash syntax, PowerShell syntax, and `cmd.exe` switches are mixed. Test the exact command in the active shell.
- **Stale artifact**: an old executable passes after source changed. Clean or force rebuild, then confirm its timestamp/path.
- **Mock substitution**: a fake snapshot proves Encoder behavior but not Runtime atomicity. Inspect or test the production boundary separately.
- **Narrow assertion**: a wrap test checks only one wheel or direction while both paths execute.
- **Status ahead of evidence**: plan/topology says complete while hardware checkboxes or integration tests remain open.

## Architecture Failures

- Read only the edited module and miss upstream transformations or downstream assumptions.
- Add a second sign correction, filter, scale, limit, integral, or sample-period assumption.
- Preserve a lower-to-upper callback as a compatibility path.
- Add a bridge that creates a new bidirectional class-diagram edge.
- Move code without moving resource ownership, ISR state, or initialization order.
- Test headers and API names instead of implementations and all callers.

## Embedded Failures

- Claim PWM frequency, direction, PPR, timing, or motor safety from source alone.
- Start motor output before zeroing compare/direction state.
- Allow rapid positive-to-negative output without zero crossing and dead time.
- Use `volatile` as a substitute for pair consistency or a critical section.
- Use signed arithmetic at wrap boundaries without a defined mapping.
- Add many defensive branches with no real fault model or caller action.

## Completion Rule

For each claim, record this tuple:

```text
claim -> source evidence -> command -> observable postcondition -> remaining limitation
```

If any element is missing, do not mark the claim complete.
