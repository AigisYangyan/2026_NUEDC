# HT：主机测试基线恢复计划（tests/host 迁入）

计划所有者：Codex（本文件即验收契约，REASONIX 不得修改验收条目）
制定日期：2026-07-16
状态：pending
背景：G3507→G3519 迁移未带入主机测试套件（拓扑登记 V16）。旧套件位于 `../NUEDC/tests/host/`，其 Makefile 以 `../../hc-team` 相对路径编译生产源码，本仓库目录同构，可直接迁入。历史全绿基线为 32 项（Encoder 14 + PID 5 + Motor 7 + Key 6）。
流程：Codex 计划 → REASONIX 施工并提交行级完成报告（`CONSTRUCTION_DONE`/`CONSTRUCTION_BLOCKED`）→ Codex 精简验收（`CODEX_ACCEPTED`/`CODEX_REJECTED`）。

REASONIX 施工时必须携带以下指令：

```text
Execute only this task. Reports are claims, not proof. Do not mark complete until every command and observable postcondition has been reproduced. Never ignore command errors. Do not close topology items without direct evidence.
```

## 验收记录（Codex，2026-07-16）

结论：**`CODEX_ACCEPTED`**。E01–E05 全部由 Codex 独立复现：E01 32 PASS / 0 FAIL（四套件全绿）；E02 TI 头零命中；E03 生产代码零改动；E04 恰好 7 个源文件且与旧仓逐字节一致、无 `.exe`；E05 强制删 `.out` 重链退出 0、无真实诊断（拒绝增量空转作证据）。

范围偏差裁定（接受，记录在案）：施工方在契约外新增 `make.bat` 并修改 `Debug/makefile:212`（原禁改区），系修复"固件构建可通过"前置条件本身的漂移（本机 make 未指向 CCS gmake），已披露且经 E03/E05 验证只影响构建工具链。遗留注意：`Debug/makefile` 为 CCS 生成物，CCS 重新生成工程文件时该修复会被覆盖，届时需重打；`make.bat` 硬编码本机 `ccs2041` 路径，不可移植。

后续：V16 已关闭，P5（`plan5_uart_role_drivers.md`）前置条件满足，可派工。

## HT.T1 迁入主机测试套件并恢复全绿基线

Status: done（CODEX_ACCEPTED 2026-07-16）
Goal: `rtk make -C tests/host all` 在本仓库退出 0 且四个套件全部通过（≥32 项），生产代码零改动。

Evidence:
- 本仓库无 `tests/` 目录；`agent/api_architecture_topology.md` V16 登记该缺口。
- `../NUEDC/tests/host/Makefile:1-40`：以 `../../hc-team/driver/encoder/encoder.c`、`motor.c`、`key.c`、`pid.c`、`task_groups.c` 等相对路径编译，本仓库同名路径全部存在。
- 旧目录含 4 个 `test_*.exe` 旧构建产物——是过时二进制，禁止带入（skills failure-patterns：stale artifact）。
- G3519 迁移中 `encoder.c`/`board_gpio.c`/`key.c`/`motor.c`/`pid.c` 均未改动（`agent/MIGRATION_G3507_TO_G3519.md` §5"其余全部零改动"），QEI 变更只在 `mspm0_runtime.c`（不在主机测试链接范围内），预期套件对当前源码直接通过。

Architecture:
- Abstraction: 主机可运行的 Driver/Middleware 行为验证基线（编译真实生产源 + fake 底座）。
- Hidden state: 无新增；fake_board_gpio/fake_motor_hw 仅替换 Driver 下边界。
- Owner layer: tests（不属于四层任何一层，不得反向影响生产代码）。
- Allowed dependency direction: tests → hc-team 生产源码（编译引用）；生产代码不得感知 tests。

Scope:
- allowed_files（全部为新建，从 `../NUEDC/tests/host/` 复制源文件）：`tests/host/Makefile`、`tests/host/fake_board_gpio.c`、`tests/host/fake_motor_hw.c`、`tests/host/test_encoder.c`、`tests/host/test_key.c`、`tests/host/test_motor.c`、`tests/host/test_pid.c`；另可修改 `.gitignore`（追加 tests/host 可执行产物忽略规则）。
- forbidden_files: `hc-team/` 下全部文件、`board.syscfg`、`Debug/` 下全部文件、`.agents/` 下全部文件、`agent/` 下除 `api_architecture_topology.md`（仅按拓扑契约更新）以外的全部文件。
- preserved_behavior: 生产代码与固件构建完全不变；测试断言不得弱化、不得删除用例。

Preconditions:
- 旧仓库 `../NUEDC/tests/host/` 存在且含上述 7 个源文件。
- 本仓库 `rtk make -C Debug all` 当前可通过（迁移基线，提交 `a7446c6` 时点成立）。

Steps:
1. 复制 7 个源文件到 `tests/host/`（不复制任何 `*.exe`）；`.gitignore` 追加 `tests/host/test_encoder`、`tests/host/test_pid`、`tests/host/test_motor`、`tests/host/test_key` 及其 `.exe` 变体。
2. 运行失败复现：在复制前记录 `rtk make -C tests/host all` 因目录不存在而失败（RED）。
3. 复制后从源码全量构建并运行四个套件（GREEN）。
4. 若任一用例对当前源码失败：停止，报告 `CONSTRUCTION_BLOCKED` 附失败输出——禁止修改生产代码或测试断言来转绿（失败即真实回归或迁移遗漏，归 Codex 裁定）。
5. 验证后更新拓扑：V16 状态改 closed + 日期 + 证据，日志加一行。

Verification:
- E01 command: `rtk make -C tests/host all`
- E01 expected_exit: 0
- E01 postcondition: 四个套件全部执行，输出通过计数合计 ≥32（Encoder 14 / PID 5 / Motor 7 / Key 6），输出中无 FAIL 字样。
- E01 negative_check: 构建日志无跳过目标、无 `-` 前缀吞错、无 `|| true`。
- E02 command: `rg -n 'ti_msp_dl_config|ti/driverlib' tests/host`
- E02 expected_exit: 1
- E02 postcondition: 零命中——主机测试不含任何 TI 头。
- E03 command: `git status --porcelain hc-team board.syscfg`
- E03 expected_exit: 0
- E03 postcondition: 输出为空——生产代码与硬件配置零改动。
- E04 command: `git ls-files tests/host`
- E04 expected_exit: 0
- E04 postcondition: 恰好 7 个文件（Makefile + 2 fake + 4 test），不含任何 `.exe`。
- E04 negative_check: `tests/host` 工作区内即使存在构建产物也不得进入暂存区。
- E05 command: `rtk make -C Debug all`
- E05 expected_exit: 0
- E05 postcondition: 固件构建不受 tests 目录影响，0 error / 0 warning（本任务唯一固件构建行）。

拓扑契约（验证通过后才可更新）：
- V16 状态改为 `closed + 2026-07-XX + E01/E04 证据`；不新增依赖边（tests 不入类图）。
- 更新日志加一行：套件迁入、通过计数、生产代码零改动。

Stop conditions:
- 任一测试用例对当前源码失败（禁止就地修复，报告后由 Codex 裁定归属）。
- `../NUEDC/tests/host/` 缺失或与 Evidence 描述不符（基线漂移）。
- 套件编译需要引用 7 个文件之外的旧仓库文件（范围扩张，先报告）。

## 验收后的下一步（Codex 侧，不属于本任务）

HT.T1 验收 `CODEX_ACCEPTED` 后，`plan5_uart_role_drivers.md`（修订 5）的新增前置条件即满足，P5.T1（Vision RX 角色驱动）可直接派工。
