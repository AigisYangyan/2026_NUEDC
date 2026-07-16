# 计划：P7 —— IMU 解耦 DL_HAL + emm42 残渣清理

状态：`ACCEPTED`（2026-07-17 自闭环施工 + 验收，**范围经用户裁定收窄至 step_motor**）
日期：2026-07-17
流程：单 agent 自闭环（`.agents/skills/embedded-closed-loop`）。**本契约在写任何生产代码前提交**（`16e0c96`），验收比对本文冻结的 E 行。

## 0. 用户裁定：IMU 部分推迟（2026-07-17）

**施工过程中用户裁定：IMU 与 12 路灰度暂不编写，后续会修改。**

裁定到达时，IMU 部分已施工完成且 E01–E06 全过。按用户选择，**已施工的 IMU 改动整体回退**
（`git checkout HEAD -- hc-team/driver/imu/`），本次仅交付 emm42 残渣清理。

理由（用户选定）：IMU 后续要重写，保留本次改动会与重写冲突，且届时基线不干净。
本次 IMU 改动极小（删 1 行 include + 3 处延时调用），重写时无保留价值。

**因此推迟、保持原样的项：**

- 缺陷 A（`IMU.h:5` 冗余 `ti_msp_dl_config.h`）→ 违规边 `IMU_API --> DL_HAL : exposed TI header`
  **保持 open**。拓扑未改该边，因为它仍是真实现状。
- 缺陷 B（三处延时按 32MHz 算，实际 `CPUCLK=80MHz`，时长仅标称 40%）→ **未修，缺陷仍在**。
  IMU 是死代码，不会被触发；但 **IMU 重写时必须一并处理**，否则一启用即暴露。
  正解已查明：改用 `Mspm0Runtime_DelayMs()`（基于 `Clock_NowMs()`，不含频率假设），
  `IMU.c` 已包含该头，无需新增依赖。
- 12 路灰度：本次全程零触碰（灰度是 `plan_qei_gray_pinmux.md` 的范围，已于 `1f182f3` 完成）。

## 0.1 验收记录（2026-07-17，收窄后）

契约冻结于 `16e0c96`，早于任何 `hc-team/**` 改动 —— E 行未被事后调整以迁就实现。

| ID | 结果 | 观察到的后置条件 |
|---|---|---|
| E01 | PASS | clean 固件构建 exit 0；warning/error/remark 命中 1 行，核查为链接器命令行中的 `--warn_sections` **旗标名**，非告警。实际 0 warning / 0 error |
| E02 | **N/A** | IMU 部分经用户裁定推迟并回退，本行不适用。IMU 层 TI 依赖**仍在** |
| E03 | PASS | `g_emm42_default` / `Emm42_RunCommandTask` 全工程 **0 命中** |
| E04 | **N/A** | 同 E02。`IMU.c` 的 32MHz 频率假设**仍在** |
| E05 | PASS | 主机 8 套件 **76 PASS / 0 FAIL**，gcc `-Wall -Wextra` 0 告警 |
| E06 | PASS | `emm42.h` 仅删 1 行声明，`Emm42_Build*Frame` 全部签名逐字未变；`IMU.h` 已回退，与 HEAD 逐字节相同 |

**E02/E04 标 N/A 而非 PASS**：二者曾在回退前实测通过，但当前工作区不满足。
标 PASS 会让后来者以为违规边已消除 —— 那是假的。

验收中发现并处理的契约外问题：

1. **E05 统计口径再次误报**。正则 `FAIL|Error|error:` 命中 2 行，核查为 PID 测试名
   `test_signed_error_direction` / `test_zero_error_steady_state` 中的 "error" 子串，
   两行本身都是 PASS。**这是 `plan_qei_gray_pinmux.md` §0.1（44/76 误报）的同类错误第二次发生**
   —— 统计口径而非被测对象出错。教训：断言必须锚定行首（`^\s*PASS:`），不可子串匹配。
2. **`git show HEAD:` 不能用于判断工作区行尾**。`core.autocrlf=false` 且无 `.gitattributes`，
   但 `git show` 输出的是仓库 blob。本次据此得出「HEAD 是 LF、Edit 写了 CRLF」的**错误结论**，
   实为 `IMU.c` 原本就是**混合行尾**（约 13 行 LF 夹在 CRLF 中）。
   **判定行尾只能用 `cat -A` 看工作区文件本身**。
   附带效果：IMU 回退后，该行尾问题随之消失，无残留。

## 0. 巡查结论：P7 的实际范围远小于原计划

原 P7 派工写的是「剩余 Driver 拆分：IMU、Step Motor」。实测两项都不成立：

| 原计划项 | 实测 | 处置 |
|---|---|---|
| Step Motor 拆分 | **已完成**。`emm42.c` 已是纯组包，零 HAL、零 App 依赖；`Emm42_Send*` 包装在 `stepmotor_bus.c` | 不再拆，仅清残渣 |
| IMU 拆分 | 不需要「拆」。IMU 无外部调用者，问题不是耦合过密而是**一行多余的头文件包含** | 只斩该依赖 |

**IMU 死到什么程度（证据）：**

1. 全工程 `#include "IMU.h"` 仅 `IMU.c` 自身（`grep -rln`，排除 `Debug/`）。
2. 全工程无任何 `IMU_UART_*` / `IMU_Get*` / `Send_IMU_*` / `imu_measurement_t` 引用（同上）。
3. `mspm0_runtime.c` 中 **IMU 字样 0 处** —— RX 未接线，`IMU_UART_RxByte()` 永远收不到字节，
   整条解析链路物理上不可能被触发。
4. `imu_uart.c:13` 注释自述：TX role 存在只为「保持符号被链接而不触碰硬件状态」。

## 1. 查实的缺陷

### 缺陷 A：拓扑违规边 `IMU_API --> DL_HAL : exposed TI header`

`IMU.h:5` `#include "ti_msp_dl_config.h"`。查实：**IMU.h 并未使用该头文件的任何东西** ——
公共接口只用到 `uint8_t`/`uint16_t`/`float`/`char`，全部来自 `<stdint.h>`。此包含是纯冗余，
却把 TI 私有头顺着 IMU.h 传染给未来每一个包含者。

### 缺陷 B：延时短 2.5 倍（潜伏，从未暴露）

```c
// IMU.c:121, :430, :543
// MSPM0: delay_cycles (32MHz, 1ms = 32000 cycles)
delay_cycles(interval_ms * 32000);
```

`Debug/ti_msp_dl_config.h:77` `CPUCLK_FREQ = 80000000`。三处延时（校准轮询 1ms、
版本查询 5ms、等待校准 1ms）实际时长均为标称的 **40%**。因模块死代码，缺陷从未被观察到。

**根因不是数字写错，而是延时不该按周期数算。** `Mspm0Runtime_DelayMs()`
（`mspm0_runtime.c:230`）基于 `Clock_NowMs()` 计时，**不含任何频率假设**。改用它
可同时消除缺陷 A 的最后一处 DL 依赖与缺陷 B，且不引入 80000 这类新魔数。
`IMU.c:3-4` 已经包含了 `clock.h` 与 `mspm0_runtime.h`，无需新增依赖。

### 缺陷 C：emm42 残渣

- `emm42.c:25-26` `g_emm42_default_acceleration` / `g_emm42_default_speed`：
  非 static 全局，全工程零引用。跨模块全局是 AGENTS.md 明令禁止的耦合面，
  留着就是邀请下一个人去用。
- `emm42.c:227` `Emm42_RunCommandTask(void) {}` 空函数 + `emm42.h:111` 声明：
  全工程零调用者。队列编排已归 `stepmotor_bus`，此壳是拆分遗留。

## 2. 三个设计问题

- **抽象是什么？** IMU 对外是「读九轴/姿态测量值」。它不该、也不需要向调用者暴露
  自己跑在哪颗 MCU 上。
- **什么必须隐藏？** TI 寄存器层。延时实现（周期数 vs 计时）同属实现细节，
  正确的边界是 `Mspm0Runtime_DelayMs`。
- **代码属于哪一层？** IMU 是协议解析，纯 C；板级时间/串口经 `mspm0_runtime`/`board_uart`
  两个既有边界。本次不新建任何抽象。

## 3. 明确不做

- **不接 IMU RX 线路。** P5 修订 4 把 RX FIFO 推迟到 P7，但当前**无任何消费者**
  （Service 层为空）。为不存在的调用者接线属无需求预建，违反 AGENTS.md §8.3。
  接线的正确时机是 Service 层真要读姿态时，与该需求同批施工。
- **不删除 IMU 模块。** 虽是死代码，引脚表已为陀螺仪保留 PA25/PA26，硬件侧确认要装。
  删掉 616 行可用协议实现、日后重写，是拿返工换行数。
- **不重写 IMU 协议/接口。** 本次只斩依赖、修延时、清残渣。15 个公共函数签名一律不动。
- **不动 `s_direction_sign`**（承接 `plan_qei_gray_pinmux.md` §7.1，仍待实测）。

## 4. 目标变更

| 文件 | 变更 | 缺陷 |
|---|---|---|
| ~~`hc-team/driver/imu/IMU.h`~~ | ~~删 `#include "ti_msp_dl_config.h"`~~ | **推迟（§0）** |
| ~~`hc-team/driver/imu/IMU.c`~~ | ~~删 `delay_cycles` 宏；3 处调用改 `Mspm0Runtime_DelayMs(ms)`~~ | **推迟（§0）** |
| `hc-team/driver/step_motor/emm42.c` | 删 2 个未引用全局；删 `Emm42_RunCommandTask` 空实现 | C |
| `hc-team/driver/step_motor/emm42.h` | 删 `Emm42_RunCommandTask` 声明 | C |
| `agent/api_architecture_topology.md` | 类图去 `Emm42_RunCommandTask`。**违规边 `IMU_API --> DL_HAL` 保持不动** | — |

## 5. 范围

- `allowed_files`：`hc-team/driver/imu/**`、`hc-team/driver/step_motor/**`、
  `agent/api_architecture_topology.md`、`agent/README.md`、本文件
- `forbidden_files`：`board.syscfg`、`Debug/**`、`hc-team/app/**`、`hc-team/driver/`（其余）、
  `docs/**`
- `preserved_behavior`：IMU 15 个公共函数签名、`Emm42_Build*Frame` 全部行为、
  `stepmotor_bus` 对 emm42 的调用面

## 6. 证据行（冻结）

| ID | 命令 | 期望 | 后置条件（可观察） |
|---|---|---|---|
| E01 | `rtk make -C Debug all`（clean 后） | exit 0 | 0 warning / 0 error |
| E02 | `grep -rn "ti_msp_dl_config\|DL_" hc-team/driver/imu/` | — | **输出为空** —— IMU 层零 TI 依赖 |
| E03 | `grep -rn "g_emm42_default\|Emm42_RunCommandTask" --include=*.c --include=*.h`（排除 Debug/） | — | **输出为空** —— 残渣清尽 |
| E04 | `grep -rn "delay_cycles\|32000\|160000" hc-team/driver/imu/IMU.c` | — | **输出为空** —— 频率假设消除 |
| E05 | `make.bat -C tests/host all`（PowerShell 工具执行） | exit 0 | 76 项全绿，无回归。计数口径：数 `^\s*PASS:` 行（见 `plan_qei_gray_pinmux.md` §0.1 教训） |
| E06 | `git diff` 复核 IMU 公共接口 | — | `IMU.h` 中 15 个函数签名逐字未变；`emm42.h` 除删 1 行声明外未变 |

E 行预算 6/6。无硬件行（IMU 为死代码，无法上板观测）。

## 7. 风险与移交

1. **IMU 仍是死代码，本次不改变这一事实。** 交付物是「违规边消失 + 潜伏缺陷消除」，
   不是「IMU 可用」。若期望是后者，需另开派工：接 RX 线路 + 写 Service 层消费者，
   且必须有实物 IMU 上板实测。
2. **缺陷 B 的修复无法被验证。** 死代码跑不起来，E 行只能证明「频率假设已消除」，
   不能证明「延时现在准了」。IMU 首次真正启用时，须把三处延时纳入实测。
3. 承接未结项：编码器方向实测、核心板晶振书面确认（见 `plan_qei_gray_pinmux.md` §7）。
