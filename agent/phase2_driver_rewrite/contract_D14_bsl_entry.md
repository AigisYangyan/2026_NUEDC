# 契约 D14 —— UART0 BSL 软件跳转入口 Driver（`driver/bsl_entry`）——冻结

> 关闭 `plan_driver_first_order.md` §5.2 登记的 GAP **D14**（BSL ENTRY 监听器）。
> 闭环铁律：本契约（含全部证据行）先提交冻结，再写第一行生产代码；契约行有错在单独显式提交中修订。
> 架构权威 `AGENTS.md`；现状权威 `agent/api_architecture_topology.md`（拓扑同步由 topo-updater 收尾）。

## 1. 裁定与范围

- **背景**：`board.syscfg` 已把 UART0/PA10/PA11@9600 预留为 `UART_BSL_ENTRY`（无线 BSL 烧录口），
  但中断未使能、无 IRQ handler、无监听器（GAP D14）。配合主机侧 `tools/bsl_flash` 一键烧录脚本，
  需要固件在运行期收到 `0x22` 时软件跳转到官方 ROM BSL。
- **协议机制（照 G3507 权威参考）**：SDK `LP_MSPM0G3507/bsl/bsl_software_invoke_app_demo_uart/main.c`
  ——UART RX 收到 `0x22` → 调 `invokeBSLAsm()`：先按 `SRAMFLASH` 报告的大小清空整片 SRAM
  （ECC 区 `0x20300000` + 非 ECC 区 `0x20200000`，**BSL_ERR_01 勘误绕行**），再写
  `DL_SYSCTL_RESET_BOOTLOADER_ENTRY` 到 `SYSCTL->SOCLOCK.RESETLEVEL` + `KEY|GO` 到 `RESETCMD`，复位进 BSL，永不返回。
- **触发时机裁定（用户 2026-07-18，两选一）**：**ISR 内直接触发**。理由：唯一活的主循环在冻结的旧
  `SysRun()` 里，无干净轮询点；ISR 内跳 BSL 是**对 §8.1/V09「ISR 只做置位」规则的一处显式豁免**，
  正当性 = 跳转即复位、永不返回、无返回栈、无共享态竞争（一切即将被擦除+复位），且照 TI C1104 参考先例。
  本豁免在此契约冻结，arch-auditor 据此放行；不得据此在别处放宽 ISR 纪律。
- **接口辩护（器件能做什么）**：BSL 入口能在运行期监听触发字节并把设备交给官方 ROM BSL。
  仅此成为 Driver 边界能力。判字节与跳转的逻辑本体留在 Driver 边界文件；runtime 只做 ISR 分发（V02/V09）。

## 2. 层次与所有权

- 新 Driver `driver/bsl_entry`，Driver 层。依赖方向：`bsl_entry → DL HAL`（仅 invoke 边界文件，允许）；
  `mspm0_runtime → bsl_entry`（同层受控 ISR 分发，照 `VisionUart_IsrPushByte` 先例）；
  `board.c → NVIC_EnableIRQ(UART_BSL_ENTRY)`（board 是唯一碰 NVIC 的项目代码，sys_init 已声明）。
- **单一所有者声明**：
  - 触发字节常量 `BSL_ENTRY_TRIGGER_BYTE = 0x22u` 唯一定义在 `bsl_entry.c`；他处不得再写 `0x22` 魔数判定。
  - SRAM 擦除 + BSL 复位序列（invokeBSLAsm）唯一在 `bsl_entry_invoke.c`；他处不得再造第二条复位进 BSL 路径。
  - UART0 外设/引脚/波特率唯一在 `board.syscfg`；NVIC 使能唯一在 `board.c`；ISR 分发唯一在 `mspm0_runtime.c`。
- **不复做**：无 RX FIFO/缓冲（本口不搬运字节流，只逐字节判触发即跳），不设超时、不设重试、不暴露电平态。

## 3. 文件层级与 allowed_files（无 glob）

| 文件 | 动作 |
|---|---|
| `hc-team/driver/bsl_entry/bsl_entry.h` | 新建（公共面：`BslEntry_IsrOnByte`；边界 seam：`BslEntry_InvokeBsl`） |
| `hc-team/driver/bsl_entry/bsl_entry.c` | 新建（可移植逻辑：触发常量 + 判字节→调 InvokeBsl；不含 DL HAL/asm，可主机链） |
| `hc-team/driver/bsl_entry/bsl_entry_invoke.c` | 新建（**target-only**：invokeBSLAsm 内联汇编 + 复位；含 DL HAL，主机不链） |
| `board.syscfg` | 修改（UART5/UART_BSL_ENTRY：加 `enabledInterrupts=["RX"]` + `rxFifoThreshold=ONE_ENTRY`；更新注释） |
| `hc-team/driver/board/board.c` | 修改（`Board_Init` 加一行 `NVIC_EnableIRQ(UART_BSL_ENTRY_INST_INT_IRQN)`，仿 IMU） |
| `hc-team/driver/mspm0_runtime/mspm0_runtime.c` | 修改（加 `UART_BSL_ENTRY_INST_IRQHandler` + include `bsl_entry.h`） |
| `Debug/makefile` | 修改（ORDERED_OBJS 加 2 个 .o；加 `-include hc-team/driver/bsl_entry/subdir_vars.mk`） |
| `tests/host/test_bsl_entry.c` | 新建 |
| `tests/host/fake_bsl_invoke.c` | 新建（假 `BslEntry_InvokeBsl`：计数，替代 asm 硬件边界） |
| `tests/host/Makefile` | 追加 test_bsl_entry 目标/clean/.PHONY |
| `.gitignore` | 追加 test_bsl_entry / test_bsl_entry.exe |
| `agent/phase2_driver_rewrite/contract_D14_bsl_entry.md` | 本契约 |
| `agent/phase2_driver_rewrite/plan_driver_first_order.md` | D14 状态回写（topo-updater 收尾） |

**本地生成物（不入库，§4 规则）**：`Debug/hc-team/driver/bsl_entry/subdir_vars.mk` + `subdir_rules.mk`
（仿 board_uart 复制，令增量构建真正编译新文件——规避「增量空转+陈旧 linkInfo 假证据」陷阱）。

forbidden_files：`hc-team/app/**`、`hc-team/middleware/**`、`hc-team/driver/**` 其余全部
（除上表三处显式修改）、tests/host 既有 `test_*.c` 与 `fake_*.c`。

## 4. 公共接口（最小面）

```c
/* bsl_entry.h */
#include <stdint.h>

/* ISR 上下文：runtime 把 UART_BSL_ENTRY 收到的每个字节喂进来。
 * 命中触发字节 0x22 → 直接 BslEntry_InvokeBsl()（永不返回，设备复位进 ROM BSL）；
 * 否则立即返回。签名匹配 runtime_handle_uart_irq 的 void(*)(uint8_t) 回调契约。 */
void BslEntry_IsrOnByte(uint8_t byte);

/* 边界 seam：擦 SRAM + 复位进官方 ROM BSL，永不返回。
 * target 定义在 bsl_entry_invoke.c（含 BSL_ERR_01 勘误绕行）；主机测试由 fake_bsl_invoke.c 替身计数。 */
void BslEntry_InvokeBsl(void);
```

- 无 `_Init`：无私有状态需复位（NVIC 归 board.c、外设归 syscfg、无 FIFO）。
- runtime 接线：`void UART_BSL_ENTRY_INST_IRQHandler(void){ runtime_handle_uart_irq(UART_BSL_ENTRY_INST, BslEntry_IsrOnByte); }`

## 5. preserved_behavior

- 其余 `driver/**`、`app/**`、`middleware/**` 零行为改动；主机既有用例全过；
  既有固件行为不变（新增的是一条此前不存在的中断路径 + 从零新增 Driver）。
- board.syscfg 仅对 UART_BSL_ENTRY 增中断使能，不动其他外设分配。

## 6. 证据行（≤6，恰 1 条固件构建行）

| 行 | 名称 | 命令 | 预期 |
|---|---|---|---|
| E01 | 依赖纯净 | Grep `app/|middleware/`（path=`hc-team/driver/bsl_entry`，`#include` 行） | 0 命中（Driver 允许 DL HAL，故不扫 ti_msp_dl_config；只禁上行依赖） |
| E02 | 范围审计 | `git status` + `git diff --stat` 对照 §3 | 无 allowed_files 之外的改动（本地 subdir_vars.mk 不入库不计） |
| E03 | 主机测试 | PowerShell：`rtk proxy make -C tests/host all` | ≥基线+≥3 PASS / 0 FAIL；必含：`IsrOnByte(0x22)`→InvokeBsl 恰调 1 次；`IsrOnByte` 传 0x00/0x21/0x23/0xFF 等非触发字节→InvokeBsl 调 0 次；触发常量 = 0x22 单一所有者 |
| E04 | 固件构建 | PowerShell：`rtk make -C Debug all` | exit 0、0 diagnostics、`bsl_entry.o`+`bsl_entry_invoke.o` 经 linkInfo.xml 确证进链、`UART_BSL_ENTRY_INST_IRQHandler` 符号已定义（不再落 startup 默认 handler） |

## 7. 收尾

- arch-auditor 复核：ISR 豁免落在契约、V02/V09 边界、单一所有者、依赖矩阵。
- topo-updater：driver.md 新增 `BslEntry_API` 类 + 与 Runtime_API 依赖边、§7 覆盖清单行、关闭 D14、索引 §10 日志。
- 交付说明：什么改了、验证结果、假设与剩余风险（0x22 端到端需 `tools/bsl_flash` 配合，硬件上板由用户验证）。
