# Driver 层总汇报

日期：2026-07-17
范围：`hc-team/driver/**` 全部 14 个模块
依据：P9 巡查（契约 `19214a8`，E05 修订 `7ddc040`），逐条实测，非推断

---

## 0. 结论先说

> **Driver 层已收官。** 14 个模块全部按新编程思想重写或新建，唯一实质缺口 D12（12 路灰度）
> 已于本批次补齐。剩余两项（D13/D14）是小体量债，不阻塞任何功能。
>
> **下一步不在 Driver 层，而在用户手上**：4 项硬件实测（§5）没做完，Driver 再干净也跑不对车。
> 其中 **灰度供电电压（H1）会烧 IO**，是唯一的硬件风险项。

本批次（P9）三件事：
1. 新建 `driver/gray/` —— Driver 层最后一块缺口 D12 补齐
2. 修 `emm42.h` 倒置依赖（V18）、删死符号、补 9 处注释
3. 删两份参考工程（器件事实已先行转录进 `docs/`）

---

## 1. 模块清单（14 个）

| # | 模块 | 目录 | 覆写 | 主机测试 | TI HAL 位置 |
|---|---|---|---|---|---|
| D01 | clock | `driver/clock/` | ✅ P1/P1F | — | `clock.c`（指定边界） |
| D02 | mspm0_runtime | `driver/mspm0_runtime/` | ✅ P1/P5/P8 | — | `mspm0_runtime.c`（指定边界） |
| D03 | board | `driver/board/` | ✅ P5 | — | `board.c`（指定边界） |
| D04 | encoder | `driver/encoder/` | ✅ P2/P2F | 14 项 | **无**（经 BoardGpio） |
| D05 | motor | `driver/motor/` | ✅ P3 | 7 项 | `motor_hw.c`（端口） |
| D06 | key | `driver/key/` | ✅ P4 | 6 项 | **无**（经 BoardGpio） |
| D07 | oled | `driver/oled/` | ✅ P6 | 有 | `oled_hardware_i2c.c` |
| D08 | board_uart ×4 | `driver/board_uart/` | ✅ P5/P8 | 有 | 各角色 `.c`（指定边界） |
| D09 | step_motor/emm42 | `driver/step_motor/` | ✅ P5/P7/**P9** | **无（见 §4）** | **无**（纯组包） |
| D10 | uart_vofa | `driver/uart_vofa/` | ⚠ 见 §3 | 有 | **无** |
| D11 | imu | `driver/imu/` | ✅ P8/P8B | 25 项 | **无**（经 ImuUart） |
| **D12** | **gray** | **`driver/gray/`** | ✅ **P9 新建** | **8 项** | `gray_hw.c`（端口） |
| D13 | board_gpio | `driver/board_gpio/` | ⚠ 过渡模块 | 经 fake | `board_gpio.c` |
| D14 | BSL ENTRY 监听器 | 未建 | **未实现** | — | — |

**主机测试合计：109 项 / 0 FAIL。** 固件 clean 构建 exit 0 / 0 诊断。

---

## 2. 本批次新建：`driver/gray/`（D12）

器件：武汉无名创新 **NCHD1（迹）** 12 路阵列。配置手册：**`docs/12路灰度传感器配置指南.md`**。

公共接口按 `AGENTS.md` §15.3 判据（每个接口都必须能用「器件能做什么」解释）收敛为**一个函数**：

```c
#define GRAY_CHANNEL_COUNT 12u
uint16_t Gray_ReadDarkBitmap(void);   // bit i = PIN_IN(i+1) 当前为深色
```

**器件能做的就这一件事**：12 路各输出一个数字电平，深色=高。没有寄存器、没有命令、没有时序、没有错误码。所以驱动也就只有这一个函数。

### 结构（沿用 motor 范式）

```
gray.h        公共能力接口
gray_port.h   HAL 边界
gray.c        端口值 -> 通道位图 的散射（零 TI 依赖，可主机测试）
gray_hw.c     唯一碰 DL HAL 的文件；引脚全部经 syscfg 生成宏，零手抄
```

### 兑现了一个一直没兑现的性质

`board.syscfg` 特意让 IN4 占 **PB8** 而不是引脚表建议的跨端口 PA7，为的是让 12 路全在 PORTB、
**一次 `DL_GPIO_readPins` 读全**。而旧的 `app/tasks/track_follow/track_follow.c:59-65` 是
**12 次分读** —— 这个性质买了却从没用上，路间一直有时间偏斜。

新驱动一次读全，并由主机用例的**调用计数**钉死。变异验证：把读取挪回循环里，用例立刻失败。

### 刻意不做的事（都不是遗漏）

不做 Init（SysConfig 已配输入，模块无状态）、不做去抖/滤波/反相（器件侧 C2+比较器迟滞已做，
再做就是第二个所有者 §8.2）、不做丢线判断/误差量化/赛道特征识别（属循迹算法，归 Middleware §3.3）、
不做诊断计数（纯电平无错可计）、**不声明左右**（见 §5 H2）。

---

## 3. 本批次修复

### V18 —— `emm42.h` 倒置依赖（已关闭）

`emm42.h` 曾声明 13 个总线动作函数，而它们**实现在 App 层** `stepmotor_bus.c:702-861`。
Driver 头对外宣称 Driver 提供这些能力，实则不提供 —— 单独链接 `emm42.o` 会得到未定义引用。

13 个声明已迁往 `stepmotor_bus.h`（实现所在层）。声明数守恒：`emm42.h` 13→0，`stepmotor_bus.h` 0→13。
调用方零改动（`2DPlatform_LaserStrike.c` 早已同时包含两个头）。

> **这不是 V08 误闭。** V08 的判据是「`emm42.c` 不再 `extern` App 符号」，P5 的 R04 扫的是 `.c` 里的
> `extern`，**看不见头文件里的声明**。这是一个 P5 的量具照不到的新缺陷。历史结论不改写。

### 其余

- 删死符号 6 处：`EMM42_UART_ID`、`EMM42_MICROSTEP`、`EMM42_PULSES_PER_REVOLUTION`、
  `vofa_param_setter_t`、`VOFA_RX_BUF_SIZE`
- `Motor_Brake` 从公共头收回为 `motor.c` 私有 `motor_brake_one`
  —— 两轮同属一个运动体，只刹一轮会把车甩转，那不是任何调用者该有的能力
- 删空目录 `driver/eeprom/`（P6 已删 `at24cxx.*`）
- `uart_vofa.h` 补 `extern "C"` 守卫（全树唯一缺失者）
- 9 个文件补 `@file` 契约块（4 个 board_uart `.c` + `motor_hw.h` + 3 个 uart 头 + `oledfont.h`）

---

## 4. 未修的账（登记在案，不是遗漏）

| ID | 内容 | 为什么现在不修 |
|---|---|---|
| **V19** | `uart_vofa.h:16` `typedef uint8_t u8` 污染全局命名空间 | `u8` 在整条 VOFA 链路广泛使用，替换属跨模块 churn。随 VOFA Service 阶段处理（`extern "C"` 已补） |
| **V20** | `board.h:5-7` 断言「No other project layer may include `ti_msp_dl_config.h`」措辞过宽 | 与 8 个模块的实际设计冲突。clock/board_gpio/oled/board_uart 都是各自外设的**指定边界文件**，包含 TI 头是设计使然。真正的规则是「TI HAL 只能出现在各模块的边界文件里」。**这是文档措辞错，不是代码错** |
| D13 | `board_gpio` → `runtime` 过渡边 | 编码器已改硬件 QEI，这条边的「raw counts」部分是否还需要，得先巡查再定。**不要在没读代码前假设它该删** |
| D14 | BSL ENTRY 监听器未实现 | `UART_BSL_ENTRY`(UART0/9600) 已配好但无消费者。在此之前**软件跳 BSL 不可用**，只能硬件 BSL 引脚+复位 |
| — | `emm42.c` 无主机测试 | 它零 TI 依赖、纯组包，本来可测。V18 修复后已具备条件（`emm42.o` 不再需要 `stepmotor_bus.o`），**下一批次可补** |
| — | `oledfont.h` 在头里定义数据 | 外部链接的数据定义放在头里。实测包含者恰好只有 `oled_hardware_i2c.c`，无冲突。已补约束注释，改结构留待需要时 |
| V03 | `track_follow.c` 直接调 `DL_GPIO_readPins` | §15.2 禁止改。**V03 的关闭时机是上层重置删除 `track_follow.c` 之时，不是 D12 建成之时** |

### 实测过、但**故意不动**的项（避免下次重复巡查）

| 项 | 理由 |
|---|---|
| `Imu_Update/GetSnapshot/GetDiag/ZeroYaw/SetOutputRate`、`Gray_ReadDarkBitmap`、`Key_IsPressed/GetPressEvent` 零生产调用者 | **§15.1：Driver 零调用者是预期状态，不是缺陷。** 实测对照可证这是链接器行为而非缺陷：零调用者的 `Imu_Update` 在 map 中为 0，有调用者的 `Imu_Init` 为 3 —— `--gc-sections` 把没人调的剔了 |
| `OLED_ERR_UNKNOWN`/`OLED_ERR_NULL_PTR` 无消费者 | 它们是 `OLED_*` 公共函数**实际返回**的值。删掉枚举项会让返回值失去名字 —— 那是撒谎，不是精简 |
| `Encoder_Id` typedef 名无引用 | 它命名了 `Encoder_Snapshot` 数组的索引空间，自文档且与 `Motor_Id` 对称。删掉是零收益 churn |

---

## 5. ★ 移交用户的硬件实测项（Driver 无法自证，`AGENTS.md` §15.4）

**这一节是当前真正的瓶颈。Driver 层已经干净了，但下面 4 项不做完，车不会对。**

| # | 项 | 动作 | 不做的后果 |
|---|---|---|---|
| **H1** | **灰度供电电压** | 确认模组是 3.3V 还是 5V 供电。若 5V，须核实 MSPM0G3519 的 PB 脚耐不耐 5V；不耐就**必须**改 3.3V 或每路串 300–1000Ω | 手册 p.31：5V 供电时输出高约 **4.0V**。灌进不耐压的脚 = **烧 IO**。**唯一的硬件风险项** |
| **H2** | **灰度位序** | 黑线移到阵列最左，读 `Gray_ReadDarkBitmap()`，看 bit0 还是 bit11 置位 | 厂商约定 P1=最右，本仓注释写 IN1=最左，**二者矛盾**。未实测前任何左右权重表都是猜的 |
| **H3** | **编码器方向** | 手推左轮前进，`Encoder_GetSnapshot()` 左轮速度应为**正** | `encoder.c:41` `s_direction_sign[]={-1,1}` 是**旧板** AB 接线的标定值。新板若按 A→PB7/PB10 正确接线，应改 `{1,1}`。**未实测前不得改**（§8.1 禁止猜方向） |
| **H4** | **IMU 上位机配置** | 波特率 115200→**230400**；输出速率 10Hz→**500Hz**；**自动零偏校准**（车放平静止 ~20s）；装平（Z 轴垂直）；正方向实测 | 固件 syscfg 已是 230400，**两边不一致就是乱码**。详见 `docs/IMU陀螺仪配置指南.md` |

> ⚠ **H2/H3 的修正点各自只有一个**：灰度位序只能改上层权重表，编码器方向只能改 `s_direction_sign[]`。
> **加第二个反转开关会与第一个互相抵消** —— 两处都对时车是对的，改任何一处车就反着走，且极难查（§8.2）。

其余待硬件组：核心板晶振书面确认（PA3/PA5/PA6）；PA0/PA1（VOFA）引出。

---

## 6. 架构现状

- **依赖方向**：`App -> Middleware -> Driver -> DL HAL`，Driver 不反向调用上层（V01/V02/V08 均已关闭）
- **TI HAL 边界**：每个模块的 TI 依赖都收在指定边界文件里（`*_hw.c` / `board.c` / `clock.c` /
  `mspm0_runtime.c` / `board_gpio.c` / 各 `board_uart` 角色 `.c` / `gray_hw.c` / `oled_hardware_i2c.c`）
  （封包审计补记：本清单原漏列 `board_gpio.c`，它在 `BoardGpio_GetKeyRawLevels` 直调
  `DL_GPIO_readPins`，与本文 §1 D13 行「TI HAL 位置 = board_gpio.c」一致）。
  `encoder`/`key`/`motor.c`/`imu`/`gray.c`/`emm42` 六者零 TI 依赖，因而可主机测试
- **ISR 纪律**：ISR 只做搬运/置位/计数，解析一律在任务态（V09 关闭）
- **单一数据所有者**：编码器方向 `encoder.c:41` 唯一；PWM 频率 syscfg 单源；灰度去抖归器件

**配置唯一来源是仓库根 `board.syscfg`。** 外设实例号与引脚是 Driver 以下的私有事实 ——
P5/P8B/P9 三次改配置，`hc-team` 驱动代码零改动，这一点已被反复实测证实。

---

## 7. 参考资料处置

两份参考工程已按用户要求删除（共 144 个文件）：
`hc-team/12路灰度传感器检测20240331（STM32F103C8T6）/`、`hc-team/IMU_NEW_EXAMPLE/`。

> **它们从未进过 git，删除不可逆。** 因此器件事实在删除**之前**先转录并提交：
> - `docs/12路灰度传感器配置指南.md`（提交 `f443c7f`）—— 灰度手册全部关键事实
> - `docs/IMU陀螺仪配置指南.md`（P8 产物）+ `agent/phase2_driver_rewrite/done/plan_p8_imu_rewrite.md` §1.3
>   —— IMU 协议与数据手册矛盾点
>
> 厂商示例里的**循迹算法**（-11..+11 偏差映射表、停止线检测）没有随驱动迁入 ——
> 它属 Middleware，不属 Driver。其设计前提（10mm 线宽下最多 2 路同时压线）已记入灰度指南 §5，
> 将来写循迹时从那里接着走。

---

## 8. 施工质量记录

P9 全程走 `embedded-closed-loop`：契约含全部证据行先于代码入库，git 充当第二方。

| 事件 | 说明 |
|---|---|
| 契约冻结 | T1 `b421682`、T2/T3 `19214a8` —— 均先于代码 |
| **契约修订 1** | `7ddc040`：E05 用裸模式 `Motor_Brake`，而它是 `Motor_BrakeAll` 的前缀 —— **自冻结起不可满足**。单独提交修订，未与代码混提 |
| 变异验证 | gray 用例经两次变异反证：恒等映射被 4 条用例逮住；把端口读取挪进循环被调用计数用例逮住 |
| **自造 bug 被构建行逮住** | T2 新注释里写了 `Emm42_Send*/Move*` —— 其中的 `*/` 提前终止了块注释，15 条编译诊断。构建证据行的价值即在此 |

### ★ 量具错误第 5 次（记录在案）

E05 是同类错误的第五次。前四次：44/76 误报、`--warn_sections` 误报、P8 的 `IMU_UART_` 裸前缀、
P8 收官的 `--untracked-files=no` 看不见未跟踪文件。

**但本次的根因与 P8 不同，值得单独记：**
P8 的教训是「模式必须在冻结前先对现有代码树跑一遍」。**本次跑了** —— 冻结前实测
`grep -c 'Motor_Brake' motor.h` 得 2，据此写下「2 → 须 0」。
错在**只看了计数、没看命中的是哪几行**：那 2 行里有一行是 `Motor_BrakeAll`，它注定不会消失。

> **修订后的教训：跑一遍不够，必须读它匹配到了什么。计数不是证据，命中的行才是。**
> 「数字变小了」和「目标真的没了」是两回事。
