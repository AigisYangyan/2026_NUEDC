## T-VTXQ1 VOFA 传输层 TX 软件队列（drop→enqueue，消除遥测 TX 丢帧）

Status: pending
Goal: `VofaUart_TryWrite` 在上一段 TX DMA 在途时不再丢弃整帧，而是把字节存入有界软件 TX FIFO；
`VofaUart_IsrTxDone` 在 DMA 完成时自动搬运下一段，从而一个 DMA 窗口内背靠背提交的 VOFA 遥测按序发出、
不丢；仅当 TX FIFO 满才拒绝并计数（`VofaUart_GetTxOverflowCount`），不静默丢。

Evidence（现状，源码实测）:
- `hc-team/driver/board_uart/vofa_uart.c:116-157` `VofaUart_TryWrite`：`s_vofa_uart_tx_busy` 或
  `vofa_uart_dma_tx_busy()` 为真时 `return false` —— 整帧被丢，不排队（真机 TX 丢帧根因）。
- `hc-team/driver/board_uart/vofa_uart.c:90-93` `VofaUart_IsrTxDone`：只 `s_vofa_uart_tx_busy = false`，
  不启动下一段 —— 队列为空时即空转，无连续排空能力。
- `hc-team/driver/uart_vofa/uart_vofa.c:536` 唯一调用者 `vofa_driver_hal_send`：`(void)VofaUart_TryWrite(...)`
  —— 忽略返回值，故 TryWrite 语义放宽为超集不会使任何调用者回退。
- `hc-team/driver/mspm0_runtime/mspm0_runtime.c:348-352` `DMA_IRQHandler` VOFA_TX 完成 → `VofaUart_IsrTxDone()`
  —— 既有 ISR 边，本任务不动其接线，只改 IsrTxDone 内部行为（仍在 vofa_uart.c 内）。
- `tests/host/test_uart_fifo.c`：仅覆盖三条链的 RX FIFO，**零 TX 断言**；`test_uart_fifo` 目标已链
  `vofa_uart.c` + `fake_uart_port.c`（`tests/host/Makefile:121`）；钩子 `VofaUart_TestCompleteTx` /
  `VofaUart_TestCopyLastTx` 已存在于 `fake_uart_port.c`。→ 改 drop→queue 不破坏任何既有断言，纯增量覆盖。

Architecture:
- Abstraction: 有界、保序的 VOFA 字节发送——TryWrite 收整帧入软件 TX FIFO；忙时不丢；DMA 完成 ISR 自动排空
  下一段；FIFO 满才拒绝并计数。（VOFA JustFloat/FireWater 为按尾标重同步的字节流，跨 DMA 段拆分无害，
  故用与 RX 对称的字节环，不做帧槽保序。）
- Hidden state: TX 字节环 FIFO（`data[]/head/tail/count/overflow_count`）、在途长度、`busy` 标志、
  DMA 源缓冲 `s_vofa_uart_tx_buf` —— 全部 `vofa_uart.c` 文件内 `static`；头文件只增一个 getter 原型。
- Owner layer: Driver（board_uart 角色驱动）。TX 缓冲/在途/ISR-搬运唯一所有者仍是 vofa_uart.c。
- Allowed dependency direction: `uart_vofa`(Driver) → `vofa_uart`(Driver) 同层受控；`vofa_uart` → DL HAL/DMA；
  `mspm0_runtime` → `VofaUart_IsrTxDone`（既有边，不新增、不加回调注册）。

Scope:
- allowed_files:
  - hc-team/driver/board_uart/vofa_uart.c
  - hc-team/driver/board_uart/vofa_uart.h
  - tests/host/test_uart_fifo.c
- forbidden_files:
  - hc-team/driver/board_uart/vision_uart.c
  - hc-team/driver/board_uart/vision_uart.h
  - hc-team/driver/board_uart/stepmotor_uart.c
  - hc-team/driver/board_uart/stepmotor_uart.h
  - hc-team/driver/board_uart/imu_uart.c
  - hc-team/driver/board_uart/imu_uart.h
  - hc-team/driver/uart_vofa/uart_vofa.c
  - hc-team/driver/uart_vofa/uart_vofa.h
  - hc-team/driver/mspm0_runtime/mspm0_runtime.c
  - hc-team/driver/mspm0_runtime/mspm0_runtime.h
  - tests/host/fake_uart_port.c
  - tests/host/Makefile
  - board.syscfg
- preserved_behavior:
  - RX 面完全不变：`VofaUart_Read` / `VofaUart_IsrPushByte` / `VofaUart_GetRxOverflowCount` / RX FIFO(512) —
    `test_uart_fifo` 三条链既有 RX 用例断言逐字不变。
  - `VofaUart_Init` / `VofaUart_TryWrite` 原型不变；TryWrite 语义为超集：可接收即返回 true（含忙时入队），
    仅 FIFO 满 / 参数非法（NULL/len==0/len>FIFO 容量）返回 false。唯一调用者忽略返回值，固件行为不回退。
  - ISR 仅搬运：`IsrTxDone` 只清 busy + 触发下一段 DMA，无解析（V09）。
  - 无新增跨层 `#include`；不重开 Runtime 回调（V02）；不复用冻结 `stepmotor_bus`/`vision_bus` 队列（V07）；
    Emm42/uart_vofa 组包解析层零改动（V08）。

Preconditions:
- board.syscfg 已将 VOFA=UART5 TX 绑 `DMA_CH1`（`VOFA_UART_DMA_TX_CHANNEL`），`mspm0_runtime` 的
  `DMA_IRQHandler` 已在 VOFA_TX 完成时调 `VofaUart_IsrTxDone` —— 既有事实，不改。
- 主机测试基线绿（`rtk make -C tests/host all` 现全绿）。

Steps:
1. 先在 `test_uart_fifo.c` 加失败复现用例（忙时入队、按序排空、满则拒绝并计数）；旧码「忙时 return false」
   使「入队后完成 A→在途为 B」断言失败。
2. `vofa_uart.c`：加 TX 字节环 FIFO + `vofa_uart_tx_kick()`；TryWrite 改为入队 + idle 时 kick；
   IsrTxDone 改为清 busy + kick 下一段；加 `VofaUart_GetTxOverflowCount`。`vofa_uart.h` 加该 getter 原型。
3. 只重构本任务引入的代码，不碰其它角色驱动。
4. 验收后由 topo-updater 同步拓扑（driver.md VOFA UART 节点补「TX FIFO」事实；索引 §10 追加日志）。

Verification:
- E01 command: `rtk make -C tests/host all`（PowerShell 工具执行，输出重定向到日志文件）
- E01 expected_exit: 0
- E01 postcondition: 日志末行 `All UART FIFO tests passed (<n> tests).`，且全套 test_* 目标编译链接+运行通过；
  `run_uart_fifo` 段新增 PASS 行 `vofa tx queue holds frame while busy`、`vofa tx queue drains in order`、
  `vofa tx overflow rejects and counts`。
- E01 negative_check: 日志无 `FAIL:`、无 `error:`。
- E02 command: `rtk make -C Debug all`（PowerShell 工具执行）
- E02 expected_exit: 0
- E02 postcondition: `vofa_uart.c` 重新编译，链接产出固件；`vofa_uart.o` 进链。
- E02 negative_check: 无 `error:`、无 `vofa_uart.c` 相关新 `warning:`。
- E03 command: `git diff --stat HEAD`（施工后、验收时）
- E03 expected_exit: 0
- E03 postcondition: 仅 `hc-team/driver/board_uart/vofa_uart.c`、`hc-team/driver/board_uart/vofa_uart.h`、
  `tests/host/test_uart_fifo.c` 三个 allowed_files 出现在改动集。
- E03 negative_check: 任一 forbidden_files 出现即拒绝。
- E04 command: `Grep` 于 `hc-team/driver/board_uart/vofa_uart.c` 搜 `app/|middleware/|Scheduler|register.*callback|extern .*(App|Task)`
- E04 expected_exit: （零命中）
- E04 postcondition: 零命中 —— 无跨层依赖、无回调注册（V02）、无对上层 extern（层级单向保持）。
- E04 negative_check: 出现任一命中即违规。

Stop conditions:
- 若基线 `rtk make -C tests/host all` 施工前非全绿 → 停，先查基线漂移再动。
- 若发现 VOFA TX DMA 完成中断在真机不可靠（IsrTxDone 不必然触发）→ 停，报告并改由上层节拍 kick 的替代方案，
  不在 ISR 里加轮询/无界等待。
- 若需求牵连到 vision/stepmotor/imu 任一其它链路或 mspm0_runtime ISR 接线 → 超范围，停并报告。
