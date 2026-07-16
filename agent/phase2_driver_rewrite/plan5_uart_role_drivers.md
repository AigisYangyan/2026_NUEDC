# P5：UART 角色驱动迁移计划（Vision / VOFA / StepMotor）

计划所有者：Codex（本文件即验收契约，REASONIX 不得修改验收条目）  
制定日期：2026-07-16  
状态：**`CODEX_ACCEPTED`（2026-07-16 验收，提交 `b24a456`）**——R01 主机测试 61 项全绿（clean 重建；Encoder 14 + PID 5 + Motor 7 + Key 6 + UART FIFO 13 + VOFA RX 6 + StepMotor UART 10）；R02–R05 负面扫描全部 exit 1 零命中；R06 固件 clean 构建 exit 0、map 含四角色符号、0 warning。契约外补充复核：§0 容量算式四处在位且与 `board.syscfg` 230400 一致、§0.1 第 1 条 drain-until-empty 三角色均为 `do{Read}while(>0)`、§6 有界等待 `STEPMOTOR_BUS_EXCLUSIVE_WAIT_MS=9u` 算式在位、`vofa_parse_rx_frame` 唯一调用链 `vofa_run():225 → vofa_process_rx_byte → :599` 确在任务态、`IMU.c` 仅两处发送调用点替换且协议零改动。验收方补齐 `.gitignore` 三个新测试二进制条目（施工遗漏，不属 R 行）。  
修订：2026-07-16 响应 REASONIX `SELF_CHECK_BLOCKED`——前置条件由“P1/P2 已验收”改为下列可验证条件，并在 §0.1 回应吞吐量发现（发现时 StepMotor 为 921600 baud × 5 ms ≈ 461 B/周期 vs 256 B 处理预算）。本计划即 plan1 P1.3/P1.4 的移交承接（见 `plan1_fix_runtime_closeout.md` §1）。  
修订 2：2026-07-16 用户将三个 UART 波特率统一改为 230400（`board.syscfg:226,260,293`），§0.1 数字与 T3 等待上限公式已按新波特率更新，全部涉及旧波特率的代码注释已由 FIX-BAUD 清理（见 `plan_fix_baud230400_comments.md`）。  
修订 3：2026-07-16 用户重命名全部 syscfg 外设组并**新增 `UART_IMU` 专用端口**（230400，无 DMA）。影响：角色名同步为 `UART_STEPPER_BUS`/`UART_HOST_LINK`/`UART_VISION`（基线提交 `5131f6e`）；**P5.T3 的 UART2 共享归属问题获得配置级解法**——IMU 应迁移到自己的 `UART_IMU` 端口而非继续借用步进发送 API，P5.T3 已按此修订（修订 4）：IMU 迁移到最小 `imu_uart` TX 角色，DMA 角色通道表已在基线提交 `5131f6e` 核对。  
修订 4：2026-07-16 Codex 落实修订 3——P5.T3 的 IMU 处置改为迁移到最小 `imu_uart` TX 角色（§4 新增契约；E11、前置条件、stop conditions 同步）；"UART2 设备归属确认"前置条件作废（IMU 已有专用端口，且 Codex 核实 IMU 模块当前零外部调用者）。  
修订 5（G3519 本地适配）：2026-07-16 工程移植到 `2026_Diansai`（MSPM0G3519，见 `docs/MIGRATION_G3507_TO_G3519.md`）。本计划内以下环境事实按新工程解读：唯一硬件配置源为仓库根 `board.syscfg`（原 `project/mspm0/board.syscfg` 为旧仓库路径）；步进总线物理实例为 **UART7**（G3519 无 UART2；引脚 PB15/PB16、230400 不变，文中"物理 UART2"字样按 UART7 解读）；`UART_IMU` 为物理 UART3。**新增前置条件：`tests/host/` 套件未随迁（拓扑 V16），P5 开工前必须先从 `../NUEDC/tests/host/` 迁入并复跑恢复全绿基线**，否则本计划全部 `make -C tests/host` E 行不可执行。  
前置条件：P1F.T1 与 P4 均已验收合入（`mspm0_runtime.c` 无并行冲突）；`tests/host` 基线已在本仓库恢复全绿（修订 5）。（原 UART2 共享禁令因 `UART_IMU` 专用端口出现而作废，见修订 4。）  
流程（2026-07-16 简化）：Codex 计划 → REASONIX 施工并提交行级完成报告 → Codex 精简验收（自检阶段已撤销）。

REASONIX 施工时必须携带以下指令：

```text
Execute only this task. Reports are claims, not proof. Do not mark complete until every command and observable postcondition has been reproduced. Never ignore command errors. Do not close topology items without direct evidence.
```

施工报告状态只允许：`CONSTRUCTION_DONE`、`CONSTRUCTION_BLOCKED`。  
Codex 验收结论只允许：`CODEX_ACCEPTED`、`CODEX_REJECTED`（2026-07-16 起硬件验收取消，SOFTWARE 分级作废）。  
证据行 ID（E01…E16）由本计划固定分配，施工与验收双方引用同一 ID。

## 0. 基线（Codex 于 2026-07-16 记录）

- 三个 UART 角色的 RX 均经 `mspm0_runtime` 回调分发：`sys_init.c:83-85` 注册 `StepmotorBus_RxISR`、`vofa_rx_isr`、`VisionBus_RxISR`。
- ~~主机测试框架 `tests/host/` 可复用~~（修订 5：套件未随 G3519 迁移带入，开工前先迁入恢复基线）；固件构建入口 `rtk make -C Debug all`；唯一硬件配置源为仓库根 `board.syscfg`（修订 5 更新路径）。
- 波特率、最长帧和调度周期是缓冲容量计算的输入。REASONIX 施工第一步必须从 `board.syscfg` 抄录三个 UART 的实际波特率并写入执行日志；容量必须按“波特率 × 最大调度延迟 × 安全系数 2”计算并把算式写进实现注释，禁止拍脑袋取 2 的幂。

## 0.1 吞吐量契约澄清（Codex 对 2026-07-16 REASONIX 发现的裁定）

REASONIX 发现属实（发现时 StepMotor 为 921600 baud，满线速每 5 ms 约 460.8 B，超过 256 B/周期预算）。2026-07-16 用户已将三路统一为 230400 baud（满线速每 5 ms 约 115.2 B），线速缺口不复存在，但下列裁定与波特率无关，继续作为本计划验收口径：

1. **消费模型统一为 drain-until-empty**：每个角色的 Service 周期内必须循环 `Read()` 直到返回 0，禁止任何固定字节数/固定次数的处理上限。处理预算的正确性用 E14 期间记录的“单周期最大消费字节数与最大耗时”证明，而不是预设常数。
2. **StepMotor 流量模型按协议事实建模**：EMM42 为命令-应答协议，从机不主动发送；最坏 RX 速率由我方命令速率决定（当前 stepmotor_bus 单帧在途、应答帧短于命令帧）。因此 StepMotor 的 E14 不使用“满线速连续输入”，改用两个可复现场景：(a) 最大真实命令-应答循环速率下 ≥60 秒 soak；(b) 一次性突发注入 ≥ FIFO 容量 80% 的合法帧串，验证不丢帧、不覆盖旧数据、overflow 为 0。若未来出现从机主动上报模式，必须先修订本契约再施工。
3. **Vision/VOFA 保持满线速要求**：外部设备可能主动连续发送，E14 对这两个角色维持 plan1 §7 的满线速 60 秒/10000 帧口径；FIFO 容量按“波特率 × 2×服务周期 × 2 安全系数”计算。若实测 CPU 无法在服务周期内 drain 完成，REASONIX 必须提交测量数据并停止，由 Codex 决定降波特率或调周期，禁止悄悄降低测试输入速率。

## 1. 三个设计问题

1. **抽象是什么？** 每个 UART 角色一个 Driver：非阻塞读取私有 RX FIFO、非阻塞尝试发送、溢出计数、TX 完成事件（仅 StepMotor 需要）。
2. **必须隐藏什么？** UART/DMA 实例、IRQ 名称、FIFO 缓冲与索引、busy/done 标志、临界区实现、回调机制本身。
3. **代码属于哪里？** 字节搬运与 FIFO 属于各角色 UART Driver；帧解析（vision 帧、VOFA 文本、EMM42 应答）属于 App/上层任务上下文；EMM42 帧组包属于 step_motor Driver（纯组包，不发送）。

## 2. 当前问题证据（Codex 已逐条核实）

- V02：`mspm0_runtime.h:19-20` 定义上层回调类型，`:49-52` 暴露 4 个注册接口；`mspm0_runtime.c:38-41` 保存回调，`:82-89` ISR 链中 `runtime_dispatch_rx()` 直接调用，`:477` TX 回调同样在 ISR 调用。
- V03：`vision_bus.c:20` 包含 `ti_msp_dl_config.h`，`:52,59` 在 App 内用 `__disable_irq/__enable_irq` 保护自建 FIFO；`stepmotor_bus.c:32` 同样包含 TI 头，`:146,153` 同样使用 IRQ 原语；`uart_stress.c:26` 包含 TI 头。
- V08：`emm42.c:61-66` `extern` App 符号 `Emm42_TransportSendMgmtFrame/SubmitControlFrame`，`:75,87` 调用，Driver 反向依赖 App。
- V09：`uart_vofa.c:573-586` `vofa_rx_isr()` 在 ISR 链内完成整帧解析（`vofa_parse_rx_frame` 写绑定变量）。
- 回调运行时替换：`uart_stress.c:268` 换成 `uart_stress_rx_isr`，`:282` 换回 `StepmotorBus_RxISR`，存在切换窗口与在途字节归属问题（plan1 §2）。
- TX 回调注册：`stepmotor_bus.c:881` `Mspm0Runtime_SetStepmotorTxCallback(stepmotor_bus_uart_tx_callback)`。
- IMU 借用步进发送 API：`IMU.c:553,563` 经 `Mspm0Runtime_SendStepmotor*` 发送；syscfg 已新增专用 `UART_IMU`（230400，无 DMA），且 IMU 模块当前零外部调用者（休眠代码，仅参与编译）。

## 3. 唯一数据处理链（P5 之后）

```text
UART/DMA IRQ（板级分发，mspm0_runtime 保留最小 IRQ 入口）
 -> 按编译期固定的角色归属直接调用对应角色 Driver 的内部 IsrPushByte/IsrTxDone（同层受控、非函数指针）
 -> 角色 Driver 私有环形 FIFO（满则丢新字节 + overflow 只增计数，不覆盖旧数据）
 -> App 任务上下文 Read() 批量拉取
 -> 解析：VisionBus 帧解析 / vofa_run 文本解析 / StepmotorBus 应答解析
 -> 发送：App 组帧后 TryWrite()；busy 立即 false，无界阻塞禁止
```

- 过渡期妥协（必须写入拓扑）：DMA/UART IRQ 入口仍在 `mspm0_runtime`，但下游改为对三个角色 Driver 的直接函数调用（Driver→Driver 同层受控、方向固定、无函数指针），彻底删除回调类型与注册接口。runtime 的最终瘦身归 P7。
- FIFO 一致性用短临界区（PRIMASK 模式，同 P2 快照做法），禁止只靠 `volatile`。
- 每角色一次解析、一处 FIFO；App 层现存的自建 FIFO 全部删除。

## 4. 最小目标接口

新目录 `hc-team/driver/board_uart/`，三个独立模块（契约沿用 plan1 §3，不合并为泛型类）：

```c
/* vision_uart.h —— 当前无发送需求，不提供 TX */
void     VisionUart_Init(void);
uint32_t VisionUart_Read(uint8_t *out, uint32_t capacity);
uint32_t VisionUart_GetRxOverflowCount(void);

/* vofa_uart.h */
void     VofaUart_Init(void);
uint32_t VofaUart_Read(uint8_t *out, uint32_t capacity);
bool     VofaUart_TryWrite(const uint8_t *data, uint32_t length);
uint32_t VofaUart_GetRxOverflowCount(void);

/* stepmotor_uart.h */
void     StepmotorUart_Init(void);
uint32_t StepmotorUart_Read(uint8_t *out, uint32_t capacity);
bool     StepmotorUart_TryWrite(const uint8_t *data, uint32_t length);
bool     StepmotorUart_IsTxIdle(void);
bool     StepmotorUart_ConsumeTxDone(void);
uint32_t StepmotorUart_GetRxOverflowCount(void);

/* imu_uart.h —— 修订 4 新增：最小 TX 角色（UART_IMU 无 DMA；IMU 模块休眠，RX 归 P7） */
void     ImuUart_Init(void);
bool     ImuUart_TryWrite(const uint8_t *data, uint32_t length);
```

语义遵循 plan1 §3 契约：`Read` 无数据返回 0；`TryWrite` busy/超长/空输入立即 false 且调用者保留缓冲区所有权；overflow 只增不清零；`ConsumeTxDone` 原子读清一次性事件。

最终删除：`Mspm0Runtime_UartRxCallback/UartTxCallback` 类型、4 个 `Set*Callback`、`Mspm0Runtime_Send*`/`Send*Byte`/`Is*TxBusy` 全部 9 个发送与查询接口（调用者改用角色 Driver）。

## 5. 施工任务

### P5.T1 Vision UART 拉取驱动

Status: pending
Goal: Vision RX 走 `VisionUart` 私有 FIFO，`vision_bus.c` 不再包含 TI 头、不再自建 FIFO、不再注册回调。

Evidence:
- `vision_bus.c:20,52,59`；`sys_init.c:85`；`mspm0_runtime.c:40`（`s_vision_rx_cb`）。

Architecture:
- Abstraction: Vision 串口字节的非阻塞批量读取。
- Hidden state: RX FIFO、读写索引、overflow 计数。
- Owner layer: Driver（board_uart/vision_uart）。
- Allowed dependency direction: `runtime IRQ 入口 -> vision_uart（同层直接调用）`；`VisionBus(App) -> vision_uart`。

Scope:
- allowed_files: `hc-team/driver/board_uart/vision_uart.c`（新建）、`hc-team/driver/board_uart/vision_uart.h`（新建）、`hc-team/driver/mspm0_runtime/mspm0_runtime.c`、`hc-team/driver/mspm0_runtime/mspm0_runtime.h`、`hc-team/app/tasks/platform_2d/vision_bus.c`、`hc-team/app/tasks/platform_2d/vision_bus.h`、`hc-team/app/system/sys_init.c`、`tests/host/test_uart_fifo.c`（新建）、`tests/host/fake_uart_port.c`（新建）、`tests/host/Makefile`、构建元数据登记新文件的最小修改
- forbidden_files: `hc-team/app/tasks/platform_2d/vision_coord.c`、`hc-team/app/tasks/platform_2d/2DPlatform_LaserStrike.c`、`hc-team/driver/uart_vofa/*`、`hc-team/app/tasks/uart_stress/*`
- preserved_behavior: Vision 帧协议与 `VisionCoord_HandleFrame` 语义不变；`VisionBus_Service5ms` 周期不变。

Preconditions:
- 从 `board.syscfg` 抄录 Vision UART 波特率并完成容量计算（记入执行日志与实现注释）。

Steps:
1. 先写 FIFO 主机测试（RED）：空读、单字节、批量、wrap、恰好满、满后丢新字节且 overflow+1、读写交错、`capacity` 小于可读量时的截断。
2. 实现 `vision_uart`；runtime 的 Vision RX 分发改为直接调用其 IsrPushByte；`vision_bus.c` 改为 `VisionUart_Read()` 拉取并删除自建 FIFO 与 IRQ 原语；`sys_init.c` 删除 `SetVisionRxCallback` 注册。
3. 只重构本任务引入的代码。
4. 验证通过后更新拓扑。

Verification:
- E01 command: `make -C tests/host run_uart_fifo`
- E01 expected_exit: 0
- E01 postcondition: 上述 FIFO 用例全部通过；overflow 语义为“保旧丢新 + 计数只增”。
- E01 negative_check: `rg 'ti_msp_dl_config|ti/driverlib' tests/host/test_uart_fifo.c hc-team/driver/board_uart/vision_uart.h` 零命中（`vision_uart.c` 允许包含 TI 头）。
- E02 command: `rg 'ti_msp_dl_config|ti/driverlib|__disable_irq|__enable_irq|SetVisionRxCallback' hc-team/app/tasks/platform_2d/vision_bus.c hc-team/app/tasks/platform_2d/vision_bus.h`
- E02 expected_exit: 1
- E02 postcondition: 零命中；`VisionBus_RxISR` 符号一并删除（`rg 'VisionBus_RxISR' hc-team` 零命中）。
- E02 negative_check: 不得把 FIFO 挪到 vision_coord 里绕过扫描。
- E03 command: `rtk make -C Debug clean` 后 `rtk make -C Debug all`
- E03 expected_exit: 0
- E03 postcondition: 新鲜产物；map 含 `VisionUart_Read`；无新增 warning。
- E03 negative_check: 无旧目标缓存。

Hardware gate:
- 计划开放（见 T3 后的 E14 汇总硬件行）；本任务软件完成不关闭硬件行。

Stop conditions:
- Vision UART 与其他角色共享 DMA 通道导致无法独立归属；容量计算输入（波特率/帧长）在 syscfg 与代码间矛盾。

### P5.T2 VOFA UART 拉取化与 ISR 解析迁出

Status: pending
Goal: VOFA RX 进入 `VofaUart` FIFO，文本解析只发生在 `vofa_run()` 任务上下文；发送改经 `VofaUart_TryWrite`。

Evidence:
- `uart_vofa.c:573-586`（ISR 解析，V09）；`uart_vofa.c:519`（`Mspm0Runtime_SendVofa`）；`sys_init.c:84`；`mspm0_runtime.c:39`。

Architecture:
- Abstraction: VOFA 串口字节的非阻塞读写；协议解析留在 uart_vofa 模块的任务上下文函数。
- Hidden state: RX/TX FIFO 与 busy 状态（vofa_uart）；解析缓冲与绑定表（uart_vofa，维持现状私有）。
- Owner layer: vofa_uart=Driver（传输）；uart_vofa=Driver（协议，同层受控单向依赖传输层）。
- Allowed dependency direction: `uart_vofa -> vofa_uart`；`runtime IRQ 入口 -> vofa_uart`；App 经既有 `vofa_run()` 不变。

Scope:
- allowed_files: `hc-team/driver/board_uart/vofa_uart.c`（新建）、`hc-team/driver/board_uart/vofa_uart.h`（新建）、`hc-team/driver/uart_vofa/uart_vofa.c`、`hc-team/driver/uart_vofa/uart_vofa.h`、`hc-team/driver/mspm0_runtime/mspm0_runtime.c`、`hc-team/driver/mspm0_runtime/mspm0_runtime.h`、`hc-team/app/system/sys_init.c`、`tests/host/test_vofa_rx.c`（新建）、`tests/host/Makefile`、构建元数据最小修改
- forbidden_files: `hc-team/app/scheduler/vofa_register.h`、`hc-team/app/scheduler/vofa_register.c`（V15 存量，另案）、全部 `app/tasks/**`
- preserved_behavior: VOFA 文本命令格式、绑定变量写入语义、遥测帧格式不变；`vofa_run()` 调用周期不变。

Preconditions:
- P5.T1 的 FIFO 实现模式已验收可复用（允许提取为 board_uart 内共享的私有静态实现，但不得成为公共泛型 API）。

Steps:
1. 先写主机测试（RED）：一帧跨多次 `Read()` 分段到达仍正确解析恰好一次；粘连两帧分别解析；超长无分隔符输入不越界且丢弃后计数；分隔符风暴不产生空帧解析。
2. 实现 `vofa_uart`；`vofa_rx_isr()` 删除，解析入口改为 `vofa_run()` 内先 `VofaUart_Read()` 再喂当前解析器；`uart_vofa.c:519` 发送改 `VofaUart_TryWrite`；`sys_init.c` 删除注册。
3. 只重构本任务引入的代码。
4. 验证通过后更新拓扑。

Verification:
- E04 command: `make -C tests/host run_vofa_rx`
- E04 expected_exit: 0
- E04 postcondition: 上述解析用例全部通过。
- E04 negative_check: 测试不得直接调用已删除的 `vofa_rx_isr`。
- E05 command: `rg 'vofa_rx_isr|SetVofaRxCallback' hc-team`
- E05 expected_exit: 1
- E05 postcondition: 全仓库零命中；解析函数只被 `vofa_run` 路径调用（`rg -n 'vofa_parse_rx_frame' hc-team` 的全部命中位于任务上下文函数内，命中清单写入证据）。
- E05 negative_check: 不得把解析改名后塞回 ISR 调用链。
- E06 command: `rg 'Mspm0Runtime_SendVofa|Mspm0Runtime_IsVofaTxBusy' hc-team`
- E06 expected_exit: 1
- E06 postcondition: 零命中。
- E06 negative_check: 无。
- E07 command: `rtk make -C Debug clean` 后 `rtk make -C Debug all`
- E07 expected_exit: 0
- E07 postcondition: 新鲜产物；map 含 `VofaUart_TryWrite`；无新增 warning。
- E07 negative_check: 无旧目标缓存。

Hardware gate:
- 计划开放（E14/E15 汇总）；VOFA 上位机联调证据在硬件行给出。

Stop conditions:
- 解析器持有跨调用状态导致无法安全从 ISR 语义迁到分段读取语义（必须先报告，不得静默改协议）；vofa_register 层需要被改动才能编译。

### P5.T3 StepMotor UART、EMM42 组包解耦与压测模式收口

Status: pending
Goal: StepMotor RX/TX 走 `StepmotorUart`；emm42 变为纯组包（V08 关闭）；uart_stress 改为 Service 级独占且不再替换回调；runtime 回调机制全部删除（V02 关闭）。

Evidence:
- `emm42.c:61-66,75,87`；`stepmotor_bus.c:32,146,153,665,678,688,881,1006`；`uart_stress.c:26,201,210,268,282`；`IMU.c:552,562`；`mspm0_runtime.h:19-20,49-70`。

Architecture:
- Abstraction: StepMotor 串口非阻塞读写 + TX 完成事件；EMM42 协议帧组包（输入参数、输出填充调用者缓冲区）。
- Hidden state: FIFO/busy/tx_done（stepmotor_uart）；协议帧格式常量（emm42）。
- Owner layer: 均为 Driver；发送编排、应答解析、压测独占属于 App（stepmotor_bus / uart_stress，存量位置不变）。
- Allowed dependency direction: `StepmotorBus(App) -> emm42(组包)`、`StepmotorBus -> stepmotor_uart`、`uart_stress -> stepmotor_uart 的 App 侧编排接口（经 StepmotorBus 提供的暂停/恢复）`；禁止 `emm42 -> App`。

Scope:
- allowed_files: `hc-team/driver/board_uart/stepmotor_uart.c`（新建）、`hc-team/driver/board_uart/stepmotor_uart.h`（新建）、`hc-team/driver/step_motor/emm42.c`、`hc-team/driver/step_motor/emm42.h`、`hc-team/driver/mspm0_runtime/mspm0_runtime.c`、`hc-team/driver/mspm0_runtime/mspm0_runtime.h`、`hc-team/app/tasks/platform_2d/stepmotor_bus.c`、`hc-team/app/tasks/platform_2d/stepmotor_bus.h`、`hc-team/app/tasks/uart_stress/uart_stress.c`、`hc-team/app/tasks/uart_stress/uart_stress.h`、`hc-team/app/system/sys_init.c`、`hc-team/driver/board_uart/imu_uart.c`（新建）、`hc-team/driver/board_uart/imu_uart.h`（新建）、`hc-team/driver/imu/IMU.c`（仅两处发送调用点替换）、`tests/host/test_stepmotor_uart.c`（新建）、`tests/host/Makefile`、构建元数据最小修改
- forbidden_files: `hc-team/app/tasks/platform_2d/2DPlatform_LaserStrike.c`、`hc-team/app/tasks/platform_2d/vision_coord.c`、`hc-team/middleware/**`
- preserved_behavior: EMM42 协议帧字节格式与校验方式不变；stepmotor_bus 的 mgmt/control 队列语义、诊断计数语义不变；压测帧格式不变。

Preconditions:
- ~~UART2 设备归属确认~~ 已作废（修订 4）：`UART_IMU` 专用端口存在，无共享归属问题。
- P5.T1/T2 通过（FIFO 模式与 IRQ 直呼模式已被验收一次）。

Steps:
1. 先写主机测试（RED）：TX busy 时 `TryWrite` 立即 false 且缓冲不变；最大合法长度成功、超长拒绝；`ConsumeTxDone` 只消费一次；RX FIFO 复用 E01 用例集。
2. 实现 `stepmotor_uart`；emm42 改为 `Emm42_Build*Frame(..., uint8_t *out, uint8_t *out_len)` 纯组包并删除 `extern` App 符号；stepmotor_bus 负责组包后入队与 `TryWrite`/`ConsumeTxDone` 编排，删除 TI 头与 IRQ 原语；uart_stress 改为经 StepmotorBus 的“请求暂停→有界等待 TX 空闲→清私有状态→独占→恢复”流程（等待上限 = 当前 syscfg 波特率 230400 下最长合法帧时间 × 2 + 一个 Service 周期，算式写入注释；波特率再变时公式随 syscfg 取值），删除两处回调替换；新建最小 `imu_uart`（仅 `ImuUart_Init()` + `ImuUart_TryWrite()`，UART_IMU 无 DMA，IRQ/有界轮询发送，算式注明上限），`IMU.c:553,563` 两处迁入；IMU RX 接线与业务激活归 P7（当前零调用者，不做推测性 RX FIFO）；最后删除 runtime 全部回调类型、注册接口和 9 个 Send/Busy 接口。
3. 只重构本任务引入的代码。
4. 验证通过后更新拓扑。

Verification:
- E08 command: `make -C tests/host run_stepmotor_uart`
- E08 expected_exit: 0
- E08 postcondition: busy 拒绝、超长拒绝、tx_done 单次消费、FIFO 用例全部通过。
- E08 negative_check: 测试不得绕过公共 API 直接写内部索引。
- E09 command: `rg 'Emm42_Transport|extern' hc-team/driver/step_motor/emm42.c`
- E09 expected_exit: 1
- E09 postcondition: 零命中（emm42 无任何 `extern` 上层符号声明）。
- E09 negative_check: 不得改用弱符号或函数指针注入替代。
- E10 command: `rg 'SetStepmotorRxCallback|SetStepmotorTxCallback|SetVofaRxCallback|SetVisionRxCallback|UartRxCallback|UartTxCallback' hc-team`
- E10 expected_exit: 1
- E10 postcondition: 全仓库零命中（V02 机制整体消失）。
- E10 negative_check: `mspm0_runtime.h` 不得保留已废弃声明。
- E11 command: `rg 'Mspm0Runtime_Send|Mspm0Runtime_Is.*TxBusy' hc-team`
- E11 expected_exit: 1
- E11 postcondition: 零命中；`IMU.c` 两处发送改用 `ImuUart_TryWrite`（专用 UART_IMU 端口）。
- E11 negative_check: IMU 协议内容不得被顺手修改。
- E12 command: `rg 'ti_msp_dl_config|ti/driverlib|__disable_irq|__enable_irq' hc-team/app/tasks/platform_2d/stepmotor_bus.c hc-team/app/tasks/uart_stress/uart_stress.c`
- E12 expected_exit: 1
- E12 postcondition: 零命中（V03 中 stepmotor/stress 两处关闭；track_follow 的 V03 残余不属于本计划）。
- E12 negative_check: 不得把 IRQ 原语挪进其他 App 文件。
- E13 command: `rtk make -C Debug clean` 后 `rtk make -C Debug all`
- E13 expected_exit: 0
- E13 postcondition: 新鲜产物；map 含 `StepmotorUart_ConsumeTxDone`；无新增 warning。
- E13 negative_check: 无旧目标缓存；`git diff --stat` 只落在 allowed_files。

Hardware gate：已随 2026-07-16 硬件验收取消整体作废；以下 E14–E16 仅保留为用户自测参考，不属于验收契约：
- E14 postcondition: 逻辑分析仪确认三个 UART 实际波特率与 syscfg 一致；Vision/VOFA 用带单调序号+校验的最短/典型/最长合法帧满线速连续输入 ≥60 秒或 ≥10000 帧（取大者）；StepMotor 按 §0.1 第 2 条的命令-应答 soak + 80% 容量突发两场景执行。通过条件：序号连续、校验全对、overflow 计数为 0，并记录单周期最大消费字节数与最大处理耗时（§0.1 第 1 条）。
- E14 negative_check: 不得降低 Vision/VOFA 输入速率凑通过；不得给 StepMotor 构造协议上不存在的满线速流量替代 §0.1 场景；速率与帧数写入记录。
- E15 postcondition: 人为构造 FIFO overflow（暂停消费任务），串口/OLED 可见 overflow 计数递增，恢复消费后系统不死锁、数据流恢复。
- E15 negative_check: overflow 期间旧数据不被覆盖（序号证据）。
- E16 postcondition: StepMotor 连续多帧发送时，逻辑分析仪确认下一帧由 Service 周期启动（帧间隔 ≥ 一个 Service 周期），非 ISR 内递归发送；压测模式进入/退出各 10 次，超时路径至少人为触发 1 次并观察到“取消切换、正常消费者保持有效”的报告输出。
- E16 negative_check: 步进电机全程无使能输出（管理帧仅限读类命令）或轴空载，遵守电机安全规程。
- 软件行（E01–E13）全过即 `CODEX_ACCEPTED` 终局；E14–E16 仅为用户自测参考。

Stop conditions:
- 压测独占的有界等待无法满足在途帧完整性；emm42 纯组包化需要改协议字节序才能实现；DMA 通道归属无法按角色分离。

## 6. 全任务禁止事项

- 禁止保留任何函数指针回调作为“过渡兼容”；同一提交内删除旧机制。
- 禁止创建泛型 UART 管理器/Bus Manager；三个角色模块各自独立，允许 board_uart 内部共享 `static` FIFO 实现文件，但不得出现公共泛型头。
- 禁止在 ISR 内解析协议、组包或启动“下一帧业务发送”。
- 禁止无界等待：所有等待必须有按波特率计算的上限并写明算式。
- 禁止给休眠的 IMU 模块添加推测性 RX FIFO 或业务逻辑（归 P7）。

## 7. 拓扑更新契约（验证通过后才允许改）

- 类图：新增 `VisionUart_API`、`VofaUart_API`、`StepmotorUart_API`（`<<driver:board_uart>>`）；`Runtime_API` 删除回调/Send/Busy 接口，新增到三个角色模块的实线（标注 transitional IRQ dispatch，直呼非回调）；`Emm42_API ..> StepMotorBus_API` VIOLATION 边删除，改 `StepMotorBus_API --> Emm42_API : frame packing`。
- 5.2/5.3 数据流图：`RuntimeCB/RuntimeRx/RuntimeRX` 违规节点改为 FIFO 拉取路径；`VofaParse` 移入任务上下文。
- 登记表：V02 改 `closed + 日期 + E10/E11 证据`；V03 改 `partially closed`（补记 vision_bus/stepmotor_bus/uart_stress 关闭，track_follow 残留）；V08 改 `closed + 日期 + E09 证据`；V09 改 `closed + 日期 + E05 证据`。
- 源文件覆盖清单：新增 `driver/board_uart/` 三个模块；`app/tasks/platform_2d` 行更新；日志新增一行。
- 软件行验收后登记表即可写 `closed`（硬件实测由用户自理）。

## 8. 统一派工（修订 6，用户裁定 2026-07-16：一次派完，单报告单验收）

T1/T2/T3 合并为**一次施工**。理由：三个角色改的是同两个共享文件（`mspm0_runtime.c/.h`、`sys_init.c`），单执行端顺序施工时逐段验收只增加往返成本。§5 各任务的 Steps、preserved_behavior、per-task stop conditions **全部保留**，作为施工内部的阶段说明与止损条件；作废的只是"每段单独报告+单独验收"。

施工内部顺序仍为 T1 → T2 → T3（依赖顺序：FIFO 实现模式在 Vision 上定型，T2/T3 复用；runtime 回调机制必须最后统一删除）。**若在第 N 个角色遇到 stop condition：提交 `CONSTRUCTION_BLOCKED`，报告已完成角色及其行状态——已完成的角色不回滚。**

### 合并范围

- allowed_files = §5 三个任务 allowed_files 的并集（含 `hc-team/driver/board_uart/` 8 个新文件、`imu_uart` 两个新文件、`tests/host/test_uart_fifo.c`/`fake_uart_port.c`/`test_vofa_rx.c`/`test_stepmotor_uart.c` 新建、`tests/host/Makefile`、构建元数据最小修改）。
- forbidden_files = §5 三个任务 forbidden_files 的并集（`vision_coord.c`、`2DPlatform_LaserStrike.c`、`vofa_register.*`、`middleware/**` 等）。
- §6 全任务禁止事项原样生效。

### 合并证据行（R01–R06，取代分段的 E01–E13；括号内为原行映射）

- R01 command: `make -C tests/host all`（原 E01+E04+E08，并含既有 32 项回归）
- R01 expected_exit: 0
- R01 postcondition: 既有 32 项 + 新增 FIFO/VOFA 解析/StepMotor TX 用例全部通过（新用例集合按 §5 各任务 Steps 第 1 步定义：wrap、恰满、保旧丢新+overflow 只增、分段/粘连帧解析恰好一次、超长不越界、busy 拒绝、tx_done 单次消费等）；通过总数与分套件计数写入报告。
- R01 negative_check: 测试从源码新构建；不得调用已删除符号；不得绕过公共 API 写内部索引。
- R02 command: `rg 'SetStepmotorRxCallback|SetStepmotorTxCallback|SetVofaRxCallback|SetVisionRxCallback|UartRxCallback|UartTxCallback|Mspm0Runtime_Send|Mspm0Runtime_Is\w*TxBusy|vofa_rx_isr|VisionBus_RxISR' hc-team`（原 E02 后半+E05+E06+E10+E11）
- R02 expected_exit: 1
- R02 postcondition: 全仓库零命中——V02 回调机制与 9 个 Send/Busy 接口整体消失；`vofa_parse_rx_frame` 的全部剩余调用点清单写入报告且均位于任务上下文函数内。
- R02 negative_check: 不得以改名/弱符号/函数指针注入规避。
- R03 command: `rg 'ti_msp_dl_config|ti/driverlib|__disable_irq|__enable_irq' hc-team/app/tasks/platform_2d/vision_bus.c hc-team/app/tasks/platform_2d/vision_bus.h hc-team/app/tasks/platform_2d/stepmotor_bus.c hc-team/app/tasks/uart_stress/uart_stress.c`（原 E02 前半+E12）
- R03 expected_exit: 1
- R03 postcondition: 零命中（V03 的 vision_bus/stepmotor_bus/uart_stress 三处关闭；track_follow 残余不属本计划）。
- R03 negative_check: 不得把 FIFO 或 IRQ 原语挪进其他 App 文件绕过扫描。
- R04 command: `rg 'Emm42_Transport|extern' hc-team/driver/step_motor/emm42.c`（原 E09）
- R04 expected_exit: 1
- R04 postcondition: emm42 纯组包，无任何上层符号声明（V08 关闭）。
- R05 command: `rg 'ti_msp_dl_config|ti/driverlib' hc-team/driver/board_uart/vision_uart.h hc-team/driver/board_uart/vofa_uart.h hc-team/driver/board_uart/stepmotor_uart.h hc-team/driver/board_uart/imu_uart.h tests/host/test_uart_fifo.c tests/host/test_vofa_rx.c tests/host/test_stepmotor_uart.c`（原 E01 negative）
- R05 expected_exit: 1
- R05 postcondition: 角色驱动公共头与主机测试零 TI 头（`.c` 实现文件允许包含）。
- R06 command: `rtk make -C Debug clean` 后 `rtk make -C Debug all`（原 E03+E07+E13，合并为唯一一次干净固件构建）
- R06 expected_exit: 0
- R06 postcondition: 新鲜产物；map 同时含 `VisionUart_Read`、`VofaUart_TryWrite`、`StepmotorUart_ConsumeTxDone`、`ImuUart_TryWrite`；无新增 warning；`git diff --stat` 只落在合并 allowed_files。
- R06 negative_check: 无旧目标缓存。

### 报告与验收

施工报告一份：R01–R06 每行一行 `Rxx: <command> -> exit <n>, <observed>` + 改动文件清单 + 状态（`CONSTRUCTION_DONE`/`CONSTRUCTION_BLOCKED <角色+原因>`）。拓扑按 §7 契约在验证通过后一次性更新（V02/V08/V09 closed，V03 partially closed）。Codex 按精简验收（含 2026-07-16 token 经济条款）一次验收，结论 `CODEX_ACCEPTED`/`CODEX_REJECTED`。
