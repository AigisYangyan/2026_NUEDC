# 计划：编码器迁出晶振脚 + 12 路灰度让位（QEI/GRAY 引脚重映射）

状态：`pending`
日期：2026-07-16
流程：单 agent 自闭环（`.agents/skills/embedded-closed-loop`）。**本契约在写任何代码前提交**，验收比对本文冻结的 E 行。

## 1. 触发与事实源裁定

用户裁定 2026-07-16：**硬件围绕固件走** —— 先定 `board.syscfg`，硬件组照配置画板。
因此 `board.syscfg` 是唯一事实源，`docs/主控板引脚表(2).xlsx` 是它的**派生产物**。

**但本次是例外，且例外方向与上句相反**：核心板是现成模块，不由硬件组绘制。
其上焊死的晶振/负载电容构成固件无法绕过的物理约束。引脚表 r44 备注
「不再用于编码器，避免核心板晶振功能冲突」记录的正是该约束。

核实结果（TI 器件数据 `MSPM0G351X.json`，`peripheralPins` + `reverseMuxes`）：

| 引脚 | SYSCTL 复用 | 固件当前占用 | 判定 |
|---|---|---|---|
| PA3 | `SYSCTL.LFXIN`（低频晶振输入） | `QEI_RIGHT.ccp0`(PHA) | **必须让出** |
| PA6 | `SYSCTL.HFXOUT`（高频晶振输出） | `QEI_LEFT.ccp1`(PHB) | **必须让出** |
| PA2 | `SYSCTL.ROSC`（振荡器外接电阻） | `QEI_RIGHT.ccp1`(PHB) | **必须让出** |
| PA7 | `SYSCTL.CLK_OUT`/`FCC_IN`（非晶振） | `QEI_LEFT.ccp0`(PHA) | 随组迁出，释放 |

固件当前用内部 SYSOSC（`Debug/ti_msp_dl_config.c:376` `DL_SYSCTL_disableHFXT()`，
`:302` `sysPLLRef = DL_SYSCTL_SYSPLL_REF_SYSOSC`），**不使用晶振**。
但「固件不用」不等于「脚是空的」——脚可用与否取决于核心板上有无实焊器件。
故本条约束成立，引脚表在此处比固件更接近物理事实。

## 2. 三个设计问题

- **抽象是什么？** 「左轮/右轮正交编码计数」。外部只需要 `Encoder_GetSnapshot()`，
  不需要知道背后是哪个 TIMG 实例或哪个引脚。
- **什么必须隐藏？** 外设实例号、引脚、PHA/PHB 通道映射 —— 全部是 Driver 以下私有事实。
  先例：调试串口迁移（`plan_debug_uart_remap.md` R05）证实 UART 实例号变更可做到驱动零改动。
- **代码属于哪一层？** 纯 `board.syscfg` 配置层，不触碰 Driver 及以上。

## 3. 裁定的目标配置

**左右轮与 timer 的绑定关系翻转，但由 syscfg `$name` 吸收，驱动零改动。**

| syscfg 实例 | 外设 | ccp0 = PHA | ccp1 = PHB | 变更 |
|---|---|---|---|---|
| `QEI_LEFT` | TIMG8 → **TIMG9** | PA7 → **PB7** | PA6 → **PB9** | 迁出晶振脚 |
| `QEI_RIGHT` | TIMG9 → **TIMG8** | PA3 → **PB10** | PA2 → **PB11** | 迁出晶振脚 |

`$name` 保持与**物理轮子**绑定（QEI_LEFT 永远是左轮），因此 `QEI_LEFT_INST` 宏
自动指向 TIMG9，驱动侧 `encoder.c` 无需感知。**这正是「左右轮静默对调」风险的解法**：
不是靠改驱动去追平 timer，而是让配置层的名字继续说真话。

12 路灰度让出 PB7/PB9/PB10/PB11，**必须全部留在 PORTB**：

| 通道 | 原 | 新 | 理由 |
|---|---|---|---|
| IN4 | PB7 | **PB8** | PB7 的物理邻脚（LQFP-64 59→60，LQFP-100 41→42），布线代价最小；无 ADC 能力，不浪费稀缺脚 |
| IN5 | PB9 | **PB20** | 采纳引脚表 |
| IN10 | PB10 | **PB14** | 采纳引脚表 |
| IN11 | PB11 | **PB0** | 采纳引脚表 |

**IN4 不采纳引脚表的 PA7**，唯一偏离项。理由：`track_follow.c:61` 使用组级单端口宏
`GPIO_LINE_SENSOR_PORT` 读全部 12 路；该宏只在 12 脚同端口时由 SysConfig 生成。
IN4 跨到 PORTA 会使该宏消失（编译失败），并使「一次 `DL_GPIO_readPins(GPIOB, 掩码)`
原子采样 12 路」不再可能，退化为两次读、通道间产生时间偏斜。

封装无关性：PB7/PB9/PB10/PB11/PB20/PB14/PB0/PB8 在 **LQFP-64 与 LQFP-100 下均存在**，
故引脚表 Q1（封装之争）不影响本次裁定，继续挂起。

释放：PA2、PA3、PA6（归还晶振/ROSC）、PA7（空闲备用）。

## 4. 明确不做

- **不改 `s_direction_sign`**（`encoder.c:41` `{-1, 1}`）。它补偿的是**当前实物板**的
  AB 接线极性；新板重画后极性须重新实测。本次是配置迁移，无实测依据即改常量属猜测，
  违反 AGENTS.md §8.1。**风险登记见 §7**。
- **不把 12 路灰度改为连续位段**（如 PB20..PB31）。原子性来自同端口，不来自位连续；
  散位仅多几次位测试，不影响正确性。为此重画 14 根线属无故障模型的优化，违反 §8.3。
- 不动 `track_follow.c`（宏名不变，`GPIO_LINE_SENSOR_PIN_INx_PIN` 全部保留）。

## 5. 范围

- `allowed_files`：`board.syscfg`、`docs/主控板引脚表(2).xlsx`、
  `agent/api_architecture_topology.md`、`agent/README.md`、本文件
- `forbidden_files`：`hc-team/**`、`tests/**`、`Debug/makefile`
- `preserved_behavior`：Encoder 公共 API、`GPIO_LINE_SENSOR_*` 宏名、`QEI_LEFT/RIGHT` 名字语义

## 6. 证据行（冻结）

| ID | 命令 | 期望 | 后置条件（可观察） |
|---|---|---|---|
| E01 | `rtk make -C Debug all`（clean 后） | exit 0 | 0 warning / 0 error |
| E02 | 读 `Debug/ti_msp_dl_config.h` | — | `QEI_LEFT_INST` = `TIMG9` 且 `QEI_RIGHT_INST` = `TIMG8` |
| E03 | 读 `Debug/ti_msp_dl_config.h` | — | `GPIO_LINE_SENSOR_PORT` **仍存在且为 `GPIOB`**（12 路同端口未破） |
| E04 | `git status --porcelain hc-team tests` | — | **输出为空** —— 驱动零改动 |
| E05 | `make.bat -C tests/host all`（PowerShell 工具执行） | exit 0 | 76 项全绿，无回归 |
| E06 | 引脚表与 syscfg 交叉核对脚本 | exit 0 | 表中每个 MCU 引脚的 syscfg 归属与 `board.syscfg` 逐行一致，0 冲突行 |

E 行预算 6/6。无硬件行（板上实测由用户自理）。

## 7. 风险与移交（必须随交付上报）

1. **★ 编码器方向须在新板上重新实测**：`s_direction_sign[] = {-1, 1}` 是对**旧板**
   AB 接线的补偿。新板若把 AB 接正，该常量须改为 `{1, 1}`；若仍接反则保持。
   **禁止新增第二个反转开关**（两处反转互相抵消）。验证方法：手动正推左轮，
   `Encoder_GetSnapshot()` 的左轮速度应为正。
2. **本次假设核心板 PA3/PA6 确有实焊晶振**。依据是引脚表 r44 硬件组备注 + TI 复用表。
   若实物核心板未焊晶振，则本次迁移非必需（但仍无害，且释放了 3 个晶振脚）。
   **请硬件组书面确认核心板晶振配置。**
3. PA0/PA1（VOFA）仍待硬件组引出，与本次无关。
