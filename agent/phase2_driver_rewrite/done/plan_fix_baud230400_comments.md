# FIX-BAUD：UART 波特率统一为 230400 后的注释与文档同步

计划所有者：Codex  
制定与执行日期：2026-07-16  
状态：done（软件项）  
性质：纯注释/文档同步，零行为变更。经用户明确授权由 Codex 直接施工并自验收，不走 REASONIX 四阶段流程；生产代码的可执行语句零改动。

## 1. 触发事实

用户于 2026-07-16 将三个 UART 角色的波特率统一修改为 230400。Codex 已核实唯一配置源：

- `project/mspm0/board.syscfg:226`：`UART1`（`UART_STEPMOTOR_HORIZON`，物理 UART2，PB15/PB16）= 230400。
- `project/mspm0/board.syscfg:260`：`UART2`（`UART_VOFA`，物理 UART3，PA25/PA26）= 230400。
- `project/mspm0/board.syscfg:293`：`UART3`（`UART_Vision`，物理 UART1，PA9）= 230400。

注释中的物理实例号（StepMotor=UART2、Vision=UART1）与 syscfg `peripheral.$assign` 一致，无需改动；只有波特率数字过时。已确认 `uart_stress.c`、`stepmotor_bus.c` 中不存在由旧波特率推导的数值常量（超时、字节时间等），改动范围确为注释与文档。

## 2. 改动清单

代码注释（6 文件 7 行，均为 921600/115200 → 230400）：

| 文件 | 行 | 内容 |
|---|---|---|
| `hc-team/app/tasks/platform_2d/stepmotor_bus.h` | 5 | 模块头说明 |
| `hc-team/app/tasks/platform_2d/vision_bus.h` | 5 | 模块头说明 |
| `hc-team/app/tasks/uart_stress/uart_stress.h` | 3、5 | @brief 与模块说明 |
| `hc-team/app/tasks/uart_stress/uart_stress.c` | 5 | 文件头说明 |
| `hc-team/app/scheduler/run_registry.c` | 71 | 运行项注释 |
| `hc-team/app/scheduler/task_scheduler.h` | 85 | 枚举注释 |

计划文档（Codex 所有）：

- `plan5_uart_role_drivers.md`：头部新增“修订 2”；§0.1 数字更新（230400 满线速 ≈115.2 B/5 ms，线速缺口不复存在，drain-until-empty 等裁定不变）；§2 UART2 归属证据措辞更新；P5.T3 压测等待上限公式改按 230400 并声明随 syscfg 取值。
- `plan1_mspm0_runtime_rewrite.md` §2：追加 2026-07-16 更正注记（历史证据保留原文）。
- `agent/phase1_DL_HAL/plan5_uart_dma_migration.md`：表格前追加存档更正注记，不改写历史表格。

未改动（有意保留）：

- `agent/api_architecture_topology.md:684` 日志行中的“921600×5ms 吞吐缺口”：历史发现记录，当时为真。
- P5 各 FIFO 容量算式要求不变：施工时按 syscfg 当时值现场抄录计算，不写死波特率。

## 3. 验收证据

- E01 command: `rg '921600|115200|460800' hc-team`
- E01 expected_exit: 1
- E01 observed: 零命中。
- E02 command: `rg 'targetBaudRate' project/mspm0/board.syscfg`
- E02 observed: 三处均为 230400。
- E03 command: `rtk make -C Debug all`
- E03 expected_exit: 0
- E03 observed: 见拓扑日志行（注释改动不参与代码生成，构建作防误伤检查）。
- E04 command: `git diff --stat`
- E04 observed: 改动仅落在 §2 清单文件；无关未提交修改未被触碰。

## 4. 遗留

- 波特率变更本身的硬件验证（逻辑分析仪实测 230400）不属于本 fix，归 P5 硬件行 E14。
- 若上位机/视觉/VOFA 端尚未同步改为 230400，联调前必须先对齐；本仓库内无对端配置可改，风险登记于此。
