# P4：Key Driver 拉取化与共享 GPIO IRQ 解耦计划

计划所有者：Codex（本文件即验收契约，REASONIX 不得修改验收条目）  
制定日期：2026-07-16  
状态：done（2026-07-16 T1/T2 CODEX_ACCEPTED；T3 已随硬件验收取消删除）  
修订：2026-07-16 响应 REASONIX `SELF_CHECK_BLOCKED`——前置条件由“P1/P2 已验收”改为下列可验证条件。本计划即 plan1 P1.5.2 的移交承接（见 `plan1_fix_runtime_closeout.md` §1）。  
前置条件：`plan1_fix_runtime_closeout.md` P1F.T1（E01–E05）已获 Codex 验收（两计划都修改 `mspm0_runtime.c`，必须顺序施工，禁止并行改同一文件）；P3 可与本计划并行，但两者不得共用一次提交。  
流程（2026-07-16 简化）：Codex 计划 → REASONIX 施工并提交行级完成报告 → Codex 精简验收（自检阶段已撤销）。

REASONIX 施工时必须携带以下指令：

```text
Execute only this task. Reports are claims, not proof. Do not mark complete until every command and observable postcondition has been reproduced. Never ignore command errors. Do not close topology items without direct evidence.
```

施工报告状态只允许：`CONSTRUCTION_DONE`、`CONSTRUCTION_BLOCKED`。  
Codex 验收结论只允许：`CODEX_ACCEPTED`、`CODEX_REJECTED`（2026-07-16 起硬件验收取消，SOFTWARE 分级作废）。  
证据行 ID（E01…E08）由本计划固定分配，施工与验收双方引用同一 ID。

## 0. 基线（Codex 于 2026-07-16 记录）

- 主机测试框架 `tests/host/` 可复用（`fake_board_gpio.c` 已存在，只覆盖编码器快照，需扩展按键假体）。
- 固件构建入口：`rtk make -C Debug all`。
- 工作区有大量未提交修改，禁止波及无关文件。

## 1. 三个设计问题

1. **抽象是什么？** 去抖确认后的按键稳定状态与单次按下事件，由上层周期拉取。
2. **必须隐藏什么？** 引脚/端口映射、有效电平、去抖计数、事件锁存、IRQ 边沿位图。公共头只暴露 `Key_Id_e` 与查询接口。
3. **代码属于哪里？** GROUP1 共享 GPIO IRQ 的原始边沿采集属于板级 IRQ 所有者（当前过渡期为 `mspm0_runtime`，对外由 `board_gpio` 暴露）；去抖与事件语义属于 Key Driver；菜单响应属于 App（V14 存量不在本计划扩大）。

## 2. 当前问题证据（Codex 已逐条核实）

- `hc-team/driver/mspm0_runtime/mspm0_runtime.c:209-242` `runtime_handle_key_irqs()` 在 ISR 中直接调用 `Key_NotifyIrq()` 共 8 处，对应拓扑边 `Runtime_API ..> Key_API : VIOLATION IRQ notifies Driver peer (until P1.5)`。
- `hc-team/driver/key/key.h:57` 为此暴露公共 `Key_NotifyIrq()`，把 ISR 通知机制泄漏进公共契约。
- `hc-team/driver/key/key.c:29-30` 包含 `ti_msp_dl_config.h` 与 `dl_gpio.h` 直接读引脚，Key 逻辑无法主机测试。
- `hc-team/driver/key/key.c:58` `s_key_irq_pending[]` 由 ISR 与 `Key_Scan()` 双写，仅靠 `volatile bool`，无一致性说明。
- `hc-team/driver/board_gpio/board_gpio.h:9-10` 注释已声明 P1.5 将扩展按键事件消费——本计划即该扩展的落地。
- 消费者：`hc-team/app/tasks/task_groups.c:210-212`（`Key_Scan()` + `Key_PollPressEvent()`）；`menu_core/menu_pages` 经 `Key_Id_e` 使用事件（V14 存量，接口名不变即不受影响）。

## 3. 唯一数据处理链（P4 之后）

```text
K1..K4 下降沿 -> GROUP1 ISR（板级 IRQ 所有者）只置私有边沿位图
 -> BoardGpio_ConsumeKeyIrqEdges() 原子读取并清除位图
 -> BoardGpio_GetKeyRawLevels() 短临界区读取四键原始“按下”位图（有效电平在此转换一次）
 -> Key_Scan() 去抖状态机（按下 4 拍确认 / 释放 4 拍解锁，仅此一处去抖）
 -> Key_IsPressed / Key_GetPressEvent / Key_PollPressEvent 拉取
```

- 有效电平（低=按下）只在 BoardGpio 转换一次；Key 状态机只见“按下/未按下”布尔位图。
- 去抖只在 Key 一处；BoardGpio 不做任何滤波。
- 事件锁存语义保持现状：一次按压一个事件，需稳定释放后才能再次触发。

## 4. 最小目标接口

`board_gpio.h` 新增（bit i 对应 `KEY_ID_K1 + i`）：

```c
uint8_t BoardGpio_ConsumeKeyIrqEdges(void); /* 原子读取并清除下降沿位图 */
uint8_t BoardGpio_GetKeyRawLevels(void);    /* bit=1 表示该键当前为按下电平 */
```

`key.h` 变更：删除 `Key_NotifyIrq()`；其余 `Key_Init/Key_Scan/Key_IsPressed/Key_GetPressEvent/Key_PollPressEvent` 签名不变。`key.c` 移除全部 TI 头包含。

## 5. 施工任务

### P4.T1 Key 去抖状态机主机测试化并改为拉取模型

Status: done（2026-07-16 CODEX_ACCEPTED：Key 6 项 + 回归 32 项全绿，TI 头零命中）
Goal: `key.c` 不含任何 DL HAL 包含，去抖/事件行为在主机测试下逐项证明，固件行为不变。

Evidence:
- `key.c:29-30` TI 包含；`key.c:118-176` 去抖状态机；`key.c:183-193` IRQ 通知入口。

Architecture:
- Abstraction: 去抖后的按键状态与单次事件。
- Hidden state: 稳定状态、事件锁存、双向去抖计数、边沿观察标志。
- Owner layer: Driver（Key）；原始位图 Owner 为 BoardGpio。
- Allowed dependency direction: `Key -> BoardGpio`（Driver 同层受控，单向）；禁止任何模块调用 Key 的写接口。

Scope:
- allowed_files: `hc-team/driver/key/key.c`、`hc-team/driver/key/key.h`、`hc-team/driver/board_gpio/board_gpio.c`、`hc-team/driver/board_gpio/board_gpio.h`、`hc-team/driver/mspm0_runtime/mspm0_runtime.c`、`tests/host/test_key.c`（新建）、`tests/host/fake_board_gpio.c`、`tests/host/Makefile`
- forbidden_files: `hc-team/app/ui/oled/menu_core.c`、`hc-team/app/ui/oled/menu_pages.c`、`hc-team/app/tasks/task_groups.c`（消费接口签名不变，无需改动）
- preserved_behavior: 按下确认 4 拍、释放解锁 4 拍、一次按压单事件、K1→K4 轮询顺序、菜单按键行为不变。

Preconditions:
- 基线 `make -C tests/host all` 通过。

Steps:
1. 先写 `test_key.c`（RED：`key.c` 仍包含 TI 头无法进入主机构建，或新 BoardGpio 假体接口不存在）。
2. 实现 BoardGpio 两个新接口（过渡期内部仍读取 runtime 维护的原始状态，与编码器快照同模式）；`key.c` 改为拉取位图；GROUP1 ISR 改置位图、删除 `Key_NotifyIrq` 调用。
3. 只重构本任务引入的代码。
4. 验证通过后更新拓扑（见 §7）。

Verification:
- E01 command: `make -C tests/host run_key`
- E01 expected_exit: 0
- E01 postcondition: 通过用例至少包含——无边沿且释放态时 `Key_Scan()` 不读电平（假体记录读取次数为 0）；边沿后连续 4 拍低电平才产生一次事件；第 3 拍出现高电平则取消候选且不产生事件；按住期间重复边沿不产生第二事件；释放需连续 4 拍高电平后才允许下一次事件；`Key_PollPressEvent()` 按 K1→K4 顺序取出且取后即清；非法 `Key_Id_e` 查询返回 false。
- E01 negative_check: `rg 'ti_msp_dl_config|ti/driverlib' hc-team/driver/key` 零命中。
- E02 command: `make -C tests/host all`
- E02 expected_exit: 0
- E02 postcondition: encoder（及已存在的 motor）测试无回归。
- E02 negative_check: `fake_board_gpio.c` 的编码器假体行为未被改动（encoder 测试内容不变即为证据）。

Hardware gate:
- 本任务不执行硬件操作。

Stop conditions:
- 拉取模型无法保持“边沿唤醒、低开销”语义；BoardGpio 与 runtime 的过渡期归属需要新增反向调用；需要改 UI/task 层才能编译。

### P4.T2 删除 Key_NotifyIrq 公共接口并收口依赖

Status: done（2026-07-16 CODEX_ACCEPTED：Key_NotifyIrq 全仓零命中已随 T1 完成；E04 复核通过；extern 声明由 Codex 归位到 mspm0_runtime.h）
Goal: 全仓库不存在 `Key_NotifyIrq` 符号；runtime ISR 与 Key 之间无同层反向通知；clean build 通过。

Evidence:
- `mspm0_runtime.c:209-242` 8 处调用；`key.h:57` 公共声明。

Architecture:
- Abstraction/Owner: 同 T1。
- Allowed dependency direction: `Key -> BoardGpio`；GROUP1 ISR 只写本模块私有位图。

Scope:
- allowed_files: `hc-team/driver/key/key.h`、`hc-team/driver/key/key.c`、`hc-team/driver/mspm0_runtime/mspm0_runtime.c`
- forbidden_files: `hc-team/app/**`、`hc-team/middleware/**`
- preserved_behavior: 菜单/任务层按键行为与 T1 后一致。

Preconditions:
- P4.T1 E01/E02 通过。

Steps:
1. 先运行 E03 扫描确认旧代码上非零命中（RED 基线记录）。
2. 删除声明、实现与全部调用；ISR 内只保留位图置位。
3. 验证通过后更新拓扑。

Verification:
- E03 command: `rg 'Key_NotifyIrq' hc-team tests`
- E03 expected_exit: 1
- E03 postcondition: 全仓库零命中。
- E03 negative_check: 不得以注释形式保留死调用。
- E04 command: `rg '#include "driver/key|Key_' hc-team/driver/mspm0_runtime`
- E04 expected_exit: 1
- E04 postcondition: runtime 不再包含 key.h、不引用任何 Key 符号。
- E04 negative_check: 不得改为 `extern` 直接声明绕过头文件扫描。
- E05 command: `rtk make -C Debug clean` 后 `rtk make -C Debug all`
- E05 expected_exit: 0
- E05 postcondition: 新鲜 `Debug/*.out` 产物；无新增 warning（对比基线）。
- E05 negative_check: 不使用旧目标文件缓存。
- E06 command: `git diff --stat`
- E06 expected_exit: 0
- E06 postcondition: 改动仅落在 T1/T2 allowed_files。
- E06 negative_check: forbidden_files 零改动；无关未提交修改未被触碰。

Hardware gate:
- 见 T3 汇总硬件行；本任务软件收口不关闭硬件行。

Stop conditions:
- 删除后发现除 runtime 外的隐藏调用者；ISR 位图与 Consume 接口出现竞态无法用短临界区解决。

### P4.T3 板上按键行为验收（已删除：2026-07-16 用户裁定取消硬件验收，板上行为由用户自测，E07/E08 作废）

Status: removed（2026-07-16 用户裁定取消硬件验收，板上行为由用户自测，E07/E08 作废）
Goal: 真实板卡上确认单击、连按、按住、快速抖动四类输入的事件行为与迁移前一致。

Evidence:
- `task_groups.c:210-212` 5 ms UI 任务消费路径。

Scope:
- allowed_files: 无代码改动预期；仅执行与记录（记录写入本文件执行日志段）
- forbidden_files: 全部源码（发现缺陷必须回到 T1/T2 走返工流程）
- preserved_behavior: 全部。

Preconditions:
- T1/T2 全部软件行通过；OLED 菜单可用。

Steps:
1. 烧录 T2 后固件。
2. 按 E07/E08 逐项操作并记录现象。

Verification:
- E07 postcondition: K1..K4 各单击 10 次，菜单每次恰好响应一次（40/40）；按住 3 秒不产生重复事件；释放后再按能再次响应。
- E07 negative_check: 不得出现一次按压双事件或丢事件。
- E08 postcondition: 快速连点每键 10 次（约 3~5 Hz），响应次数等于按压次数；期间系统无卡死、调度正常（OLED 刷新持续）。
- E08 negative_check: 不得借调试器观察；只允许 OLED/串口现象。

Hardware gate:
- 无功率器件参与；Motor P0 不受影响。（作废：2026-07-16 硬件验收取消。）

Stop conditions:
- 板卡不可用；实测丢事件或双事件（回 T1 补失败用例后返工）。

## 6. 全任务禁止事项

- 禁止保留 `Key_NotifyIrq` 或新增任何 ISR→Key 的直接调用/回调。
- 禁止在 BoardGpio 里做去抖、事件锁存或时间窗口逻辑。
- 禁止为“更快响应”把事件产生移回 ISR。
- 禁止顺手迁移 V14（UI 直接调 Key/OLED）；该项属于后续 UI/Service 阶段。

## 7. 拓扑更新契约（验证通过后才允许改）

- 类图：`Key_API` 删除 `+Key_NotifyIrq()`；删除 `Runtime_API ..> Key_API` VIOLATION 边；`BoardGpio_API` 新增 `+BoardGpio_ConsumeKeyIrqEdges()`、`+BoardGpio_GetKeyRawLevels()`；新增 `Key_API --> BoardGpio_API` 实线。
- 5.3 数据流图：`RuntimeKey` 违规节点改为 `GROUP1 ISR -> BoardGpio 位图 -> Key_Scan 拉取`。
- 登记表：`Runtime..>Key` 对应条目（V02 关联注记 “until P1.5”）改 `closed + 日期 + E03/E04 证据`。
- 源文件覆盖清单：无新增模块；日志新增一行。
