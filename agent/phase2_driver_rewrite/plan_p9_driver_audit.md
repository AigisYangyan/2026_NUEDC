# P9.T2/T3 —— Driver 层全层巡查、整理与总汇报

冻结时间：2026-07-17
协议：`.agents/skills/embedded-closed-loop`
前置：P9.T1（`plan_p9_gray_driver.md`，契约 `b421682`，代码 `b423593`）

---

## 0. 用户裁定

2026-07-17 用户消息原文（节选）：

> 「移植完毕后, 将「12路灰度传感器检测20240331（STM32F103C8T6）」「IMU_NEW_EXAMPLE」删除掉,
> 然后检查 driver 层, 是否全部已经经过了 新版编程思想覆写, 对 driver 层所有模块进行检查+整理,
> 已经没用的直接删掉, 最后添加注释, 生成 driver 层总汇报」

拆成两个任务：**T2 = 代码整理**（删死码、修倒置依赖、补注释）；**T3 = 文档**（删参考工程、器件手册转录、总汇报）。

---

## 1. 巡查实测结论（13 个模块逐个核对）

### 1.1 「新版编程思想」覆写情况

判据取自 `AGENTS.md` §8.2/§8.3/§15.3 与 `rules/embedded-engineering-baseline.md`：
能力型接口、内部状态隐藏、TI HAL 收在边界文件、单一数据所有者、有主机测试。

| 模块 | 覆写 | 依据 |
|---|---|---|
| clock | ✅ | P1/P1F 重写；`Clock_NowMs()` 有回绕契约 |
| mspm0_runtime | ✅ | P1/P5/P8；头部自述「This is not a reusable HAL」，是**指定**的 HAL 边界 |
| board | ✅ | P5；指定 HAL 边界 |
| encoder | ✅ | P2/P2F；零 TI 依赖、可主机测试、单一方向修正点 |
| motor | ✅ | P3；`motor.c` 逻辑 / `motor_hw.c` 端口 拆分，主机 7 项 |
| key | ✅ | P4；零 TI 依赖、经 BoardGpio 拉取 |
| board_uart ×4 | ✅ | P5/P8；四角色私有 FIFO |
| oled | ✅ | P6；公共头收口为 6 个显示能力接口 |
| step_motor/emm42 | ⚠ | P5/P7 拆成纯组包，但**头文件仍倒置**（见 §1.2 D-1） |
| imu | ✅ | P8/P8B 全新重写 |
| **gray** | ✅ | **P9.T1 新建** |
| board_gpio | ⚠ | 过渡模块（D13），功能正确但注释欠契约 |
| uart_vofa | ⚠ | 未经重写批次；头文件卫生欠账（见 §1.2 D-4） |

> **结论：13 个模块中 10 个已按新思想重写；emm42/board_gpio/uart_vofa 三个有头文件级欠账，本任务处理其中可无副作用处理的部分。**

### 1.2 实测缺陷清单

- **D-1（倒置依赖，本任务修）**：`emm42.h:90-119` 声明 13 个函数，
  而它们**实现在 App 层** `app/tasks/platform_2d/stepmotor_bus.c:702-861`。
  Driver 头对外宣称 Driver 提供这些能力，实则不提供；单独链接 `emm42.o` 会得到未定义引用。
  `emm42.h:5-6` 把它记为 P5 的有意妥协（「维持现有上层调用名不变」）。
  **但 Driver 头会活过上层重置，而这个谎言会跟着活下去。**
  → 登记新违规 **V18**，本任务修复：13 个声明迁往 `stepmotor_bus.h`。
  → **不是 V08 误闭**：V08 的判据是「`emm42.c` 不再 `extern` App 符号」，P5 R04 扫的是 `.c` 里的 `extern`，
    看不见头文件里的声明。这是一个**新缺陷**，不改写 V08 的历史结论。

- **D-2（死符号，本任务删）**：全仓仅在自身头文件出现，零消费者：
  `EMM42_UART_ID`（emm42.h:19-20）、`EMM42_MICROSTEP`（:36）、`EMM42_PULSES_PER_REVOLUTION`（:37）、
  `vofa_param_setter_t`（uart_vofa.h:38）、`VOFA_RX_BUF_SIZE`（uart_vofa.h:32）。

- **D-3（公共面过宽，本任务收）**：`Motor_Brake`（motor.h:57）声明为公共 API，
  但唯一调用者是同模块的 `motor.c:202`（`Motor_BrakeAll` 内部）。→ 改 `static`，从公共头移除。

- **D-4（注释欠账，本任务补）**：以下 9 个文件无 `@file` 契约块：
  `board_uart/stepmotor_uart.h`、`vision_uart.h`、`vofa_uart.h`、`motor/motor_hw.h`、`oled/oledfont.h`、
  `board_uart/{imu_uart,stepmotor_uart,vision_uart,vofa_uart}.c`。
  同目录的 `imu_uart.h` 是全树最好的端口契约，三个兄弟却是裸的 —— 同一模块内不一致。

- **D-5（空目录，本任务删）**：`hc-team/driver/eeprom/` 磁盘上存在且**为空**（P6 已删 `at24cxx.*`）。

### 1.3 实测但**不动**的项（连同理由记录，避免下次重复巡查）

| 项 | 为何不动 |
|---|---|
| `Imu_Update/GetSnapshot/GetDiag/ZeroYaw/SetOutputRate` 零生产调用者 | **§15.1：Driver 零调用者是预期状态，不是缺陷。** 上层重置时接线 |
| `Gray_ReadDarkBitmap` 零调用者（被 `--gc-sections` 从镜像剔除） | 同上。已实测对照：零调用者的 `Imu_Update` 在 map 中同样为 0，有调用者的 `Imu_Init` 为 3 |
| `Key_IsPressed`/`Key_GetPressEvent` 仅测试调用 | 同上，且二者可用「器件能做什么」解释（按键能报当前是否按下），符合 §15.3 判据 |
| `Encoder_Id` typedef 名无引用 | 它命名了 `Encoder_Snapshot` 数组的索引空间，自文档且与 `Motor_Id` 对称。删掉是零收益churn，§8.3 反对 |
| `OLED_ERR_UNKNOWN`/`OLED_ERR_NULL_PTR` 无消费者 | 二者是 `OLED_*` 公共函数**实际返回**的值（`oled_hardware_i2c.c:198,234,410`）。删枚举项会让返回值失去名字 —— 那是撒谎，不是精简 |
| `uart_vofa.h:16` 的 `typedef uint8_t u8` 污染全局命名空间 | 真实缺陷，但 `u8` 在 VOFA 链路广泛使用，替换属跨模块 churn。**登记为 V19，不在本任务修** |
| `oledfont.h:7` 在头文件里**定义**数据（`const unsigned char asc2_0806[][6] = {...}`，外部链接） | 真实缺陷，但唯一包含者是 `oled_hardware_i2c.c:32`，无实际冲突。本任务只补注释说明该约束，改结构留待需要时 |
| `board.h:5-7` 自称「No other project layer may include ti_msp_dl_config.h」，而 8 个模块都包含 | **该断言的措辞过宽**：clock/board_gpio/oled/board_uart 都是各自外设的指定边界文件，包含 TI 头是设计使然。真正的规则是「TI HAL 只能出现在各模块的边界文件里」。**登记为 V20（文档措辞缺陷），不在本任务修** |
| `emm42.c` 零 TI 依赖、纯组包，却无主机测试 | 真实缺口。但 `emm42.o` 链接需要 `stepmotor_bus.o`（即 D-1），**D-1 修完才有条件补测**。登记为后续项，不在本任务 |
| `track_follow.c` 的 V03 残留 | §15.2 禁止改。随上层重置删除 |

---

## 2. 契约

## P9.T2 Driver 层整理（删死码 + 修倒置依赖 + 补注释）

Status: pending
Goal: Driver 层无死符号、无倒置声明、无空目录，9 个欠注释文件补齐 `@file` 契约块；固件与主机测试零回归。

Evidence:
- `emm42.h:90-119` 声明 13 个函数，实现在 `app/tasks/platform_2d/stepmotor_bus.c:702-861`（施工前实测：13 条）
- `motor.h:57` `Motor_Brake` 公共声明，外部零调用者（施工前实测：motor.h 命中 2）
- 死符号施工前实测：6 处命中
- 9 个目标文件施工前含 `@file` 的：**0** 个
- `hc-team/driver/eeprom/` 磁盘存在且为空

Architecture:
- Abstraction: 不变。本任务不新增任何能力，只收窄公共面与修正声明归属
- Hidden state: 不变
- Owner layer: Driver（`stepmotor_bus.h` 的改动属 App，仅为接收 D-1 迁出的声明）
- Allowed dependency direction: `StepMotorBus(App) -> Emm42(Driver)`。修复后 Driver 头不再声明 App 实现

Scope:
- allowed_files:
  - `hc-team/driver/step_motor/emm42.h`
  - `hc-team/app/tasks/platform_2d/stepmotor_bus.h`
  - `hc-team/driver/motor/motor.h`
  - `hc-team/driver/motor/motor.c`
  - `hc-team/driver/motor/motor_hw.h`
  - `hc-team/driver/uart_vofa/uart_vofa.h`
  - `hc-team/driver/board_uart/stepmotor_uart.h`
  - `hc-team/driver/board_uart/vision_uart.h`
  - `hc-team/driver/board_uart/vofa_uart.h`
  - `hc-team/driver/board_uart/imu_uart.c`
  - `hc-team/driver/board_uart/stepmotor_uart.c`
  - `hc-team/driver/board_uart/vision_uart.c`
  - `hc-team/driver/board_uart/vofa_uart.c`
  - `hc-team/driver/oled/oledfont.h`
  - `agent/phase2_driver_rewrite/plan_p9_driver_audit.md`
  - `agent/api_architecture_topology.md`
  - `agent/phase2_driver_rewrite/plan_driver_first_order.md`
- forbidden_files:
  - `board.syscfg`
  - `hc-team/app/tasks/track_follow/track_follow.c`
  - `hc-team/app/tasks/platform_2d/stepmotor_bus.c`（**定义不动，只动声明归属**）
  - `hc-team/app/tasks/platform_2d/2DPlatform_LaserStrike.c`（已同时包含两个头，无需改）
  - `hc-team/driver/gray/gray.c`
  - `hc-team/driver/gray/gray_hw.c`
- preserved_behavior:
  - **零行为变化。** 本任务全部是声明搬家、`static` 收窄、死符号删除、注释增补
  - `Motor_Brake` 的运行时行为不变（`Motor_BrakeAll` 仍逐轮调用它），只是不再对外可见
  - 主机 109 项与固件构建零回归
  - **电机安全项（§8.1）不受影响**：本任务不碰 PWM/方向/定时器/任务周期，`motor.c` 只改一个函数的链接可见性

Preconditions:
- P9.T1 已验收（主机基线 109 PASS）
- `2DPlatform_LaserStrike.c:31,32` 已同时包含 `stepmotor_bus.h` 与 `emm42.h` —— 故声明迁移对调用方零改动（施工前实测）

Steps:
1. 先跑基线（109 PASS / 构建绿）确认无漂移。
2. D-1：13 个声明 `emm42.h` → `stepmotor_bus.h`（`stepmotor_bus.h` 补 `#include "driver/step_motor/emm42.h"` 以取 `Emm42_Axis_e`）。
3. D-2/D-3/D-5：删死符号、`Motor_Brake` 改 static、删空目录。
4. D-4：补 9 个文件的 `@file` 契约块。
5. 验证后更新拓扑与严格计划表。

Verification:

- **E01** command: `make.bat -C Debug clean all`（PowerShell 工具启动）
  expected_exit: `0`
  postcondition: `.out` 实测重链接
  negative_check: `: (warning|error|remark)` 命中 **0**；无 "up to date" 空转

- **E02** command: `make.bat -C tests/host clean all`（PowerShell 工具启动）
  expected_exit: `0`
  postcondition: `^\s*PASS:` 计数 **= 109**，`^\s*FAIL:` 计数 **0**
  negative_check: 任何低于 109 即回归

- **E03** command: `grep -rn 'EMM42_UART_ID\|EMM42_MICROSTEP\|EMM42_PULSES_PER_REVOLUTION\|vofa_param_setter_t\|VOFA_RX_BUF_SIZE' --include=*.c --include=*.h hc-team/ tests/`
  expected_exit: `1`
  postcondition: 命中 **0**（施工前实测 **6**）
  negative_check: 任一残留即 REJECT

- **E04** command: `grep -cE '^void Emm42_(SendEnableCommand|SendSpeedCommand|SendPositionCommand|EnableAll|DisableAll|SetAllAxesZero|MoveRelative|MoveAbsolute|SetZeroPosition|StartHoming|ExitHoming|SendPidConfigCommand|SendReadSpeedCommand)' hc-team/driver/step_motor/emm42.h hc-team/app/tasks/platform_2d/stepmotor_bus.h hc-team/driver/motor/motor.h`
  expected_exit: `0`
  postcondition: `emm42.h` = **0**（施工前 13）；`stepmotor_bus.h` = **13**；声明总数守恒
  negative_check: 若 emm42.h ≠0 则倒置未修；若 stepmotor_bus.h ≠13 则搬丢了声明

- **E05** command: `grep -rcE 'Motor_Brake\b' hc-team/driver/motor/motor.h; test -d hc-team/driver/eeprom && echo EEPROM_EXISTS || echo EEPROM_GONE`
  expected_exit: `0`
  postcondition: `motor.h` 中 `Motor_Brake\b` 命中 **0**（施工前 1）；输出 `EEPROM_GONE`
  negative_check: `EEPROM_EXISTS` 即 REJECT
  > 删除类证据须先证在、后证亡：施工前已实测 `hc-team/driver/eeprom/` 存在且为空目录

  > **★ 契约修订 1（2026-07-17，单独提交，量具错误第 5 次）**
  > 原 E05 用裸模式 `Motor_Brake`，postcondition 写「命中 0」。**该行自冻结起就不可满足**：
  > 裸 `Motor_Brake` 是 `Motor_BrakeAll` 的前缀，而 `Motor_BrakeAll` 是必须保留的公共 API。
  > 只要它在，命中就永远 ≥1，与 `Motor_Brake` 是否删除无关。
  > 已收紧为 `Motor_Brake\b`（词边界使其不匹配 `Motor_BrakeAll`），施工前实测 1、施工后 0。
  > **这与 P8 的 `IMU_UART_` 裸前缀错误是同一类。**
  >
  > **根因比重复本身更重要**：P8 的教训是「模式必须在冻结前先对现有代码树跑一遍」，
  > 而本次**跑了** —— 冻结前实测 `grep -c 'Motor_Brake' motor.h` 得 2，我据此写下「2 → 须 0」。
  > 错在**只看了计数、没看命中的是哪几行**。那 2 行里有一行是 `Motor_BrakeAll`，
  > 它注定不会消失。
  > **修订后的教训：跑一遍不够，必须读它匹配到了什么。计数不是证据，命中的行才是。**
  > 零命中类的证据行尤其如此 —— 「数字变小了」和「目标真的没了」是两回事。

- **E06** command: `grep -l '@file' hc-team/driver/board_uart/stepmotor_uart.h hc-team/driver/board_uart/vision_uart.h hc-team/driver/board_uart/vofa_uart.h hc-team/driver/motor/motor_hw.h hc-team/driver/oled/oledfont.h hc-team/driver/board_uart/imu_uart.c hc-team/driver/board_uart/stepmotor_uart.c hc-team/driver/board_uart/vision_uart.c hc-team/driver/board_uart/vofa_uart.c | wc -l`
  expected_exit: `0`
  postcondition: 输出 **9**（施工前 **0**）
  negative_check: <9 即有文件漏补

Stop conditions:
- 若声明迁移导致 `2DPlatform_LaserStrike.c` 需改 include → 超范围（forbidden_files），停工重新决策
- 若 `Motor_Brake` 改 static 导致任何外部链接失败 → 说明存在未发现的调用者，停工
- 若补注释过程中发现任何行为疑点 → 记录，不得顺手改行为

---

## P9.T3 参考工程删除 + 器件手册转录 + Driver 层总汇报

Status: pending
Goal: 两份参考工程从工作区消失；其中不可再生的器件事实已先行转录进 `docs/`；产出 Driver 层总汇报。

Architecture:
- Owner layer: 文档，零代码

Scope:
- allowed_files:
  - `docs/12路灰度传感器配置指南.md`（新建）
  - `docs/driver层总汇报.md`（新建）
  - `agent/phase2_driver_rewrite/plan_p9_driver_audit.md`
  - `agent/README.md`
- forbidden_files:
  - `hc-team/driver/gray/gray.c`
  - `hc-team/driver/gray/gray.h`
  - `board.syscfg`
- preserved_behavior: 零代码改动

Preconditions:
- **P9.T2 已验收**
- **转录先于删除**：两份参考工程均为未跟踪文件，删除**不可逆**（git 里没有它们）。
  故 `docs/12路灰度传感器配置指南.md` 必须在删除前写完并提交

Verification:

- **E07** command: `test -e "hc-team/12路灰度传感器检测20240331（STM32F103C8T6）" && echo GRAY_REF_EXISTS || echo GRAY_REF_GONE; test -e hc-team/IMU_NEW_EXAMPLE && echo IMU_REF_EXISTS || echo IMU_REF_GONE`
  expected_exit: `0`
  postcondition: 输出 `GRAY_REF_GONE` 与 `IMU_REF_GONE`
  negative_check: 任一 `_EXISTS` 即未删净
  > 先证在：施工前 `git status --untracked-files=all` 实测两目录下共 143 个未跟踪条目

- **E08** command: `git status --porcelain --untracked-files=all hc-team/`
  expected_exit: `0`
  postcondition: 输出为**空** —— 参考工程已消失且未留残渣；Driver 代码零改动
  negative_check: 任何输出即 REJECT

Stop conditions:
- 若器件事实尚未转录完毕 → **禁止删除**。删掉就再也拿不回来了

---

## 3. 新登记的违规（本任务登记，未必本任务修）

| ID | 内容 | 处置 |
|---|---|---|
| **V18** | `emm42.h` 声明 13 个由 App 层 `stepmotor_bus.c` 实现的函数（Driver 头倒置依赖） | **T2 修复并关闭** |
| **V19** | `uart_vofa.h:16` `typedef uint8_t u8` 污染全局命名空间；该头缺 `extern "C"` 守卫 | `extern "C"` T2 补；`u8` 属跨模块 churn，随 VOFA Service 阶段处理 |
| **V20** | `board.h:5-7` 断言「No other project layer may include ti_msp_dl_config.h」措辞过宽，与 8 个模块的实际设计冲突 | 文档措辞缺陷，随后续文档批次修正 |

## 4. 施工报告（BUILD 阶段填写）

待填。

## 5. 验收（ACCEPT 阶段填写）

待填。
