# 计划：P7 —— IMU 解耦 DL_HAL + emm42 残渣清理

状态：`FROZEN`（待施工）
日期：2026-07-17
流程：单 agent 自闭环（`.agents/skills/embedded-closed-loop`）。**本契约在写任何生产代码前提交**，验收比对本文冻结的 E 行。

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
| `hc-team/driver/imu/IMU.h` | 删 `#include "ti_msp_dl_config.h"`；补 `<stdint.h>` 确认 | A |
| `hc-team/driver/imu/IMU.c` | 删 `delay_cycles` 宏定义（:11-13）；3 处调用改 `Mspm0Runtime_DelayMs(ms)`；删 `ti_msp_dl_config.h` 包含（若无其它用途） | A+B |
| `hc-team/driver/step_motor/emm42.c` | 删 2 个未引用全局；删 `Emm42_RunCommandTask` 空实现 | C |
| `hc-team/driver/step_motor/emm42.h` | 删 `Emm42_RunCommandTask` 声明 | C |
| `agent/api_architecture_topology.md` | 删违规边 `IMU_API --> DL_HAL`；类图去 `Emm42_RunCommandTask` | — |

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
