# Task Contract

Use this compact contract for every task. It is written and **committed before any production code** — that commit is what makes self-acceptance meaningful (see SKILL.md).

```markdown
## <task-id> <title>

Status: pending
Goal: <one observable result>

Evidence:
- <source path:line and current behavior>

Architecture:
- Abstraction: <capability>
- Hidden state: <state/resources>
- Owner layer: <Driver|Middleware|Service|Task>
- Allowed dependency direction: <caller -> callee>

Scope:
- allowed_files: <exact paths>
- forbidden_files: <exact paths>
- preserved_behavior: <what cannot change>

Preconditions:
- <earlier task, hardware fact, or baseline>

Steps:
1. Add or run a failing reproduction.
2. Implement the smallest change.
3. Refactor only code introduced by this task.
4. Update topology after verification.

Verification:
- E01 command: <exact command>
- E01 expected_exit: <number>
- E01 postcondition: <observable file/symbol/value/state>
- E01 negative_check: <what must not exist or happen>


Stop conditions:
- <architecture conflict, unknown hardware fact, scope expansion, baseline drift>
```

Do not combine a command and its postcondition into one vague sentence. For destructive or cleanup commands, create the artifact first, run the command, then assert that the artifact is absent.

Do not use glob patterns in file boundaries. For a new file, write its exact intended path. When referencing topology debt, copy the exact violation ID from `agent/api_architecture_topology.md`; a phase label is not a violation ID.

The standing rule for the BUILD phase, which now applies to yourself:

```text
Execute only this task. Your own report is a claim, not proof. Do not mark complete until every command and observable postcondition has been reproduced. Never ignore command errors. Do not close topology items without direct evidence.
```

Evidence row IDs (`E01`, `E02`, ...) are assigned in DECIDE and are stable: BUILD reports against them and ACCEPT reproduces them under the same IDs. Budget: at most 6 rows per task, one command per row, one firmware-build row per task. The build report is one line per row plus changed files — no separate evidence-package document, no self-check stage.

Because one agent now owns all three phases, the rows are only binding if they were committed before the code. If a row turns out to be wrong, amend it in its own commit that says so — never silently, and never in the commit that satisfies it.
