# P6：I2C 屏幕收口与 EEPROM 移除

计划所有者：Codex（本文件即验收契约，REASONIX 不得修改验收条目）
制定日期：2026-07-16
状态：**`CODEX_ACCEPTED`（2026-07-16 验收）**

> ## 验收记录（Codex，2026-07-16）
>
> R01–R06 全部由 Codex 独立复现（未采信施工报告的输出）：
>
> - **R01** `make.bat -C tests/host clean` 后 `all` → exit 0。**76 项全绿** = 既有 61（Encoder 14 / PID 5 / Motor 7 / Key 6 / UART FIFO 13 / VOFA RX 6 / StepMotor UART 10）+ 新增 OLED 15。先 clean 再构建，排除陈旧 `.o` 冒充通过。
> - **R02** `at24cxx|AT24_|EEPROM` 扫 `hc-team` 与 `Debug/*.mk` → 零命中。
> - **R03** 隐藏符号扫 `oled_hardware_i2c.h` + `hc-team/app` → 零命中；`oledfont.h` 在 app 层亦零命中。
> - **R04** `I2C_AUX` 扫 `hc-team` → **恰好 1 个文件**（`driver/oled/oled_hardware_i2c.c`），独占成立，V17 关闭依据充分。
> - **R05** `50000u` 扫 `driver/oled/` → 零命中。算式经 Codex 独立复算：`2 byte × 9 bit / 400 kHz = 45 µs`；`× 安全系数 2 = 90 µs`；`90 µs × 80 MHz / 4 cycles = 1800 loops`——与实现注释一致，且为编译期具名常量推导，非伪装魔数。
> - **R06** `make.bat -C Debug clean` 后 `all` → exit 0，0 warning。map 含 `OLED_ShowString`/`OLED_IsReady`/`OLED_Clear`，**不含** `at24cxx_write`/`at24cxx_read`/`oled_pow`；链接行无 eeprom 目标；`Debug/hc-team/driver/eeprom` 不存在；产物时间戳新鲜。
>
> 契约外另行核实（R 行未覆盖，Codex 主动查证）：
>
> - **超时算式的前提是代码事实而非假设**：`oled_i2c_master_write` 全文件仅 1 个调用者 `oled_write_packet`，恒传 `sizeof(packet)`=2。故「单次事务固定 2 字节」成立，`wait_not_busy` 上限无推小风险（§0.1 要求的"现值偏小需报数"不适用）。
> - **测试接缝为编译期 `#if defined(HOST_TEST)`**，无函数指针、无运行期间接层，符合 P5 先例；§6 禁止的 I2C 总线抽象层确未出现。
> - **主机测试未绕过公共 API**：`test_oled.c` 只经公共头 + `fake_i2c_port.c` 注入，无直写内部状态。R01 postcondition 四项（超时不挂死、错误不吞、`IsReady` 三态、越界不写）逐项对应到具体用例。
> - **签名未被借机改动**：`git show HEAD:...h` 证实原始返回类型本就是 `Oled_Status_e`——**本计划 §4 代码块里写的 `void` 是 Codex 的笔误**，施工按 §4「以现有实现为准，逐字抄录」照抄真实签名，处置正确。UI 侧零改动编译通过。
>
> **本计划的两处缺口（Codex 自认，非施工过错）**：
>
> 1. §4 的保留/隐藏清单**漏列** `OLED_WR_Byte`、`OLED_ShowNum`、`OLED_ShowChinese`（原公共头有，R03 亦未扫）。施工按 §4 判据（UI 零调用且非显示能力抽象 → 隐藏）整体删除，全仓零命中，处置与判据一致，**接受**。
> 2. §4 代码块的返回类型笔误（见上）。
>
> **验收补丁（Codex 在合入前修复，均属 R 行未覆盖的构建元数据缺口，未达 `CODEX_REJECTED` 阈值）**：
>
> - `.gitignore` 补 `tests/host/test_oled` / `.exe`——与 P5 同类缺口，否则测试二进制会被提交。
> - 摘除 `Debug/makefile` clean 中新增的 `rmdir hc-team\driver\eeprom` 规则：它为一个已不存在且不会再出现的模块留下永久清理规则，与本计划刚写入拓扑的"EEPROM 已删除"自相矛盾，属误导性死重量。摘除后 clean+build 复跑仍 exit 0、0 warning。
>
> **未归因于 P6 的工作树变更（Codex 不代为处置，见 §10）**：`docs/主控板引脚表_G3519.xlsx` 被删除、`docs/主控板引脚表(2).xlsx` 未入库——施工报告已如实披露，Codex 未纳入本次提交。

前置条件：P5 已验收合入（`CODEX_ACCEPTED`，提交 `b24a456`）；`tests/host` 基线全绿（61 项）。

流程：Codex 计划 → REASONIX 施工并提交行级完成报告 → Codex 精简验收（自检阶段已撤销）。

REASONIX 施工时必须携带以下指令：

```text
Execute only this task. Reports are claims, not proof. Do not mark complete until every command and observable postcondition has been reproduced. Never ignore command errors. Do not close topology items without direct evidence.
```

施工报告状态只允许：`CONSTRUCTION_DONE`、`CONSTRUCTION_BLOCKED`。
Codex 验收结论只允许：`CODEX_ACCEPTED`、`CODEX_REJECTED`（硬件验收已取消，板上实测由用户自理）。
证据行 ID（R01–R06）由本计划固定分配，施工与验收双方引用同一 ID。

## 0. 基线（Codex 于 2026-07-16 逐条核实）

- **I2C_AUX = I2C1，PA30(SDA)/PA29(SCL)，Fast 400k**（`board.syscfg:149-155`）。新引脚表第 13/14 行确认此二引脚"不变"，故本计划**与引脚表无冲突**（见 `plan_pin_table_v2_migration.md`）。
- **当前 I2C_AUX 上挂两个器件**：`driver/eeprom/at24cxx.c`（12 处 `I2C_AUX_INST`）与 `driver/oled/oled_hardware_i2c.c`（11 处），两者各自持有一份近乎重复的 I2C 事务实现，**无任何总线归属或仲裁**（登记为 V17）。
- **`at24cxx` 零外部调用者**：`rg 'at24cxx|AT24' hc-team --glob '!hc-team/driver/eeprom/*'` 零命中——仅参与编译的死代码。用户裁定 2026-07-16：EEPROM 器件删除，改用 MSPM0 内置 Flash（新引脚表亦无 EEPROM 器件行，硬件侧已无此器件）。
- **删除 EEPROM 后 I2C_AUX 仅剩 OLED 一个器件**（`rg -l 'I2C_AUX' hc-team` 命中仅 eeprom/oled 两模块；MPU6050/I2C_IMU 已于 G3519 迁移时移除）。**故 V17 由"删除竞争者"关闭，不建 I2C 总线抽象层**——单器件独占外设不需要仲裁层，建之即违反 AGENTS §2「不预建灵活性」。
- OLED 公共头当前暴露 10+ 个符号，**UI 实际只用 6 个**：`OLED_Init`、`OLED_Clear`、`OLED_ShowString`、`OLED_ShowChar`、`OLED_IsReady`、`OLED_Process`（`rg 'OLED_\w+' hc-team/app/ui/oled/ app/tasks/task_groups.c app/system/sys_init.c` 统计）。
- 主机测试入口 `make.bat -C tests/host all`；固件构建入口 `make.bat -C Debug all`。

## 0.1 超时口径裁定（沿用 P5 §0.1 的"算式而非魔数"口径）

`at24cxx.c:16` 与 OLED 侧各自定义 `*_I2C_TIMEOUT_LOOPS = 50000u`，**是无推导的魔数循环计数**——与 P5 在 FIFO 容量上禁掉的"拍脑袋取值"属同一类问题，且循环计数隐含 CPU 主频与编译优化假设，`-O0`/`-O2` 下实际时长可差数倍。

裁定：OLED 的等待上限必须**按 I2C 时序推导并把算式写进实现注释**，输入取自 `board.syscfg`（Fast 400 kHz）与系统主频（80 MHz）。参考推导：一字节含 ACK 共 9 bit，400 kHz 下 22.5 µs/byte；最长单次事务字节数由实现决定（命令 2 字节 / 数据页 N 字节）；上限 = 最长事务时间 × 安全系数 2。**禁止沿用 50000u**。若推导结果显示现值偏小（存在正常事务被误判超时的风险），必须在报告中给出数字并说明。

优先使用时间基准而非循环计数；若受限于 ms 级 `Clock_NowMs()` 粒度不足以表达 µs 级上限，允许保留循环计数，但必须写明"循环计数 = 目标时长 × 主频 / 每次迭代周期数"的完整算式与所依赖的主频假设。

## 1. 三个设计问题

1. **抽象是什么？** 一块字符/位图显示屏：初始化、清屏、在坐标写字符/字符串、就绪查询、周期性推进。调用者不需要知道它是 I2C 还是 SPI、地址是 0x3C、也不需要知道页寻址。
2. **必须隐藏什么？** I2C 实例与设备地址、事务与 FIFO 细节、超时与总线恢复、页/列寻址（`OLED_Set_Pos`）、字模表、`oled_pow` 这类实现辅助、就绪标志的内部表示。
3. **代码属于哪里？** I2C 字节搬运与显示命令组包属于 `driver/oled`；菜单内容与页面编排属于 `app/ui/oled`（存量位置不变）；EEPROM 器件驱动整体删除，持久化改内置 Flash 时另立计划（不在本计划范围）。

## 2. 当前问题证据（Codex 已逐条核实）

- **V17（新登记）**：`at24cxx.c:25,49,50,54,61,67,75,119,126,132,171,173` 与 `oled_hardware_i2c.c:19,23,81,93,94,116,117,121,128,134,143` 同时直接驱动 `I2C_AUX_INST`，两份独立事务实现，无总线所有者。拓扑 `:198` `OLED_API --> DL_HAL : I2C` 与 `:207` `EEPROM_API --> DL_HAL : I2C` 两条边并存即为图上体现。
- **死代码**：`at24cxx` 零外部调用者（见 §0），但仍在 `Debug/makefile:40,78,104` 与 `Debug/sources.mk:137` 参与构建。
- **魔数超时**：`at24cxx.c:16` `AT24_I2C_TIMEOUT_LOOPS 50000u`；OLED 侧 `OLED_I2C_TIMEOUT_LOOPS` 同值同性质（`oled_hardware_i2c.c:79,127`）。无推导注释。
- **公共头泄漏内部件**：`oled_hardware_i2c.h:88` `OLED_Set_Pos`（页/列寻址）、`:103` `oled_pow`（纯数学辅助，实现见 `.c:327-334`）、`:130` `oled_i2c_sda_unlock`（`.c:199-202` 仅转调私有 `oled_bus_recover()`）——三者均非显示能力抽象，且 UI 零调用。
- **错误上报不对称**：`at24cxx` 内部 `at24_i2c_write/read` 返回 `-1/0`，公共 `at24cxx_write/read` 却返回 `void`（`.h:63,72`），错误被吞；OLED 侧已有 `OLED_ERR_TIMEOUT`（`.c:84,130`），口径不一致。（EEPROM 删除后此条自动消失，仅作为 V17 证据留档。）

## 3. 唯一数据处理链（P6 之后）

```text
app/ui/oled（菜单内容与页面编排）
 -> OLED 公共 API（Init/Clear/ShowString/ShowChar/IsReady/Process）
 -> driver/oled 私有：命令组包 + 页寻址 + 字模查表
 -> 私有 I2C 事务（I2C_AUX 独占；有界等待，上限按 §0.1 算式）
 -> DL HAL
```

- I2C_AUX **单器件独占**：`driver/oled` 是 `I2C_AUX_INST` 的唯一使用者，无需仲裁层。若将来在同一总线新增器件，**必须先修订本契约**再施工（届时才引入总线所有者）。
- 显示能力以外的一切（页寻址、字模、pow、总线恢复）一律 `static`。

## 4. 最小目标接口

`hc-team/driver/oled/oled_hardware_i2c.h` 公共面收敛为：

```c
void OLED_Init(void);
bool OLED_IsReady(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size);
void OLED_ShowString(uint8_t x, uint8_t y, const char *chr, uint8_t size);
void OLED_Process(void);
```

（参数表以现有实现为准，施工时逐字抄录，不得借机改签名。）

转为 `static` 或删除：`OLED_Set_Pos`、`oled_pow`、`oled_i2c_sda_unlock`、`OLED_ColorTurn`、`OLED_DisplayTurn`、`OLED_Display_On`、`OLED_Display_Off`、`OLED_DrawBMP`。

- 判据：UI 零调用且非显示能力抽象 → 隐藏。
- **若某符号被证明确有调用者**（施工时 `rg` 发现），保留在公共头并在报告中列出该调用点——**以代码事实为准，不得为了让头看起来干净而删掉在用符号**。
- `oledfont.h` 仅供 `oled_hardware_i2c.c` 内部使用，不得被 app 层包含。

整体删除：`hc-team/driver/eeprom/`（`at24cxx.c`、`at24cxx.h`）及其构建登记。

## 5. 施工任务

### P6.T1 EEPROM 器件删除

Status: pending
Goal: `driver/eeprom/` 整体移除，构建与拓扑同步；I2C_AUX 变为 OLED 独占。

Evidence: §0（零调用者）、§2（构建登记 `Debug/makefile:40,78,104`、`Debug/sources.mk:137`）。

Architecture:
- Abstraction: 无（删除）。持久化能力改由 MSPM0 内置 Flash 承接，属另案。
- Owner layer: —
- Allowed dependency direction: 删除后 `I2C_AUX_INST` 的唯一使用者为 `driver/oled`。

Scope:
- allowed_files: `hc-team/driver/eeprom/at24cxx.c`（删除）、`hc-team/driver/eeprom/at24cxx.h`（删除）、构建元数据登记的最小修改（`Debug/makefile`、`Debug/sources.mk`、`Debug/hc-team/driver/eeprom/*`）、`agent/api_architecture_topology.md`
- forbidden_files: `hc-team/driver/oled/*`（归 T2）、`board.syscfg`（I2C_AUX 配置保持不变——OLED 仍在用）、`hc-team/app/**`
- preserved_behavior: 无行为变更（死代码）。**固件不得因此少任何功能**。

Preconditions:
- 施工第一步复跑 `rg 'at24cxx|AT24' hc-team --glob '!hc-team/driver/eeprom/*'` 确认仍为零命中并写入日志。**若出现命中，立即 `CONSTRUCTION_BLOCKED`**，不得自行给调用者找替代。

Steps:
1. 删除两个源文件与 `driver/eeprom/` 目录。
2. 从构建元数据摘除登记（`.o`/`.d`/`subdir_*.mk`/`sources.mk` 目录行）。
3. 拓扑删除 `EEPROM_API` 类与 `EEPROM_API --> DL_HAL : I2C` 边、覆盖清单对应行。
4. 不得顺手动 OLED（归 T2）。

Stop conditions:
- 发现 `at24cxx` 存在真实调用者；`board.syscfg` 中 I2C_AUX 存在 EEPROM 专属配置项而无法在不影响 OLED 的前提下摘除。

### P6.T2 OLED 收口（封装 + 超时算式）

Status: pending
Goal: OLED 公共头只留显示能力；内部件全部 `static`；等待上限按 §0.1 算式推导；I2C_AUX 独占事实写入注释。

Evidence: §2（头泄漏三处 `:88,:103,:130`；魔数 `.c:79,127`）。

Architecture:
- Abstraction: 字符/位图显示屏。
- Hidden state: I2C 实例与 0x3C 地址、事务与超时、页/列寻址、字模、就绪标志。
- Owner layer: Driver（`driver/oled`）。
- Allowed dependency direction: `app/ui/oled -> OLED 公共 API -> (私有) DL HAL`。禁止 app 层包含 `oledfont.h` 或直接触碰 `I2C_AUX_INST`。

Scope:
- allowed_files: `hc-team/driver/oled/oled_hardware_i2c.c`、`hc-team/driver/oled/oled_hardware_i2c.h`、`tests/host/test_oled.c`（新建）、`tests/host/fake_i2c_port.c`（新建）、`tests/host/Makefile`、构建元数据最小修改、`agent/api_architecture_topology.md`
- forbidden_files: `hc-team/app/ui/oled/menu_core.*`、`hc-team/app/ui/oled/menu_pages.*`（V14 属 Service 层另案，**本计划不得改 UI**）、`hc-team/driver/oled/oledfont.h`（字模内容不动）、`board.syscfg`
- preserved_behavior: 屏幕显示效果、菜单渲染结果、`OLED_Process` 调用周期、6 个保留接口的签名与语义**逐字不变**；UI 侧零改动即可编译通过。

Preconditions:
- 从 `board.syscfg` 抄录 I2C_AUX 实际总线速率（Fast 400k，`:151`）与系统主频，写入执行日志与超时算式注释。

Steps:
1. 先写主机测试（RED），经 `fake_i2c_port.c` 注入：忙等超时返回错误而非挂死；错误被公共 API 上报/记录而非静默吞掉；`OLED_IsReady` 在 `Init` 前为 false、成功 `Init` 后为 true、事务错误后语义明确；`OLED_ShowChar` 越界坐标不越界写。
2. 收口头文件（§4 判据），内部件转 `static`；超时常量按 §0.1 推导并写算式注释，删除 `50000u`。
3. 只重构本任务引入的代码；**不得借机改字模、改渲染算法、改菜单**。
4. 验证通过后更新拓扑。

Stop conditions:
- §4 待隐藏符号中存在真实调用者且调用者位于 `forbidden_files`（改不动 → 报告并停）；超时算式推导结果与现有 `50000u` 差异导致正常事务失败（先报数据再定）；OLED 渲染依赖 `oled_pow` 的公共可见性无法在不改 UI 的前提下消除。

## 6. 全任务禁止事项

- **禁止新建 I2C 总线管理器/HAL 抽象层**。EEPROM 删除后 I2C_AUX 单器件独占，建之即过度设计（AGENTS §2、§12）。将来新增器件须先修订契约。
- 禁止无界等待：所有等待必须有按 I2C 时序推导的上限并写明算式；禁止沿用无推导的 `50000u`。
- 禁止改动 `app/ui/oled/**`（V14 归 Service 层另案）与 `board.syscfg`。
- 禁止借"收口"之名改字模、渲染算法或菜单内容。
- 禁止为 EEPROM 删除顺手实现内置 Flash 持久化（另案；当前零调用者，无功能缺口）。
- 禁止把 `oledfont.h` 或 `I2C_AUX_INST` 泄漏到 app 层。

## 7. 拓扑更新契约（验证通过后才允许改）

- 类图：删除 `EEPROM_API` 类与 `EEPROM_API --> DL_HAL : I2C` 边（`:207`）；`OLED_API` 公共方法收敛为 §4 六项，标注 `I2C_AUX 单器件独占`。
- 登记表：**新增 V17**「EEPROM 与 OLED 共用 I2C_AUX 且无总线所有者」，同批次写 `closed 2026-07-16（P6 R02/R04）：EEPROM 器件删除，I2C_AUX 由 driver/oled 独占`——登记与关闭同批次，因问题与解法在同一计划内（符合 AGENTS §14「以代码事实为准，不得隐藏现有违规」）。
- V14 保持 open（UI 直调 OLED Driver 未动，归 Service 层）。
- 源文件覆盖清单：删除 `Driver | EEPROM` 行（`:632` 邻近）；`Driver | OLED` 行更新为收口后接口。
- 日志新增一行。

## 8. 统一派工（沿用 P5 修订 6 先例：一次派完，单报告单验收）

T1+T2 合并为**一次施工**，内部顺序 T1 → T2（先删 EEPROM 使 I2C_AUX 独占成立，T2 的"单器件独占"注释才是事实）。§5 各任务 Steps、preserved_behavior、per-task stop conditions 全部保留为施工内部阶段说明与止损条件。

**若在 T1 遇到 stop condition：提交 `CONSTRUCTION_BLOCKED`，T2 不启动**（T2 的独占前提不成立）。若 T1 完成而 T2 遇阻：报告 T1 行状态，T1 不回滚。

### 合并证据行（R01–R06）

- R01 command: `make -C tests/host all`
- R01 expected_exit: 0
- R01 postcondition: 既有 61 项 + 新增 OLED 用例（§5.T2 Steps 第 1 步定义：超时返回错误不挂死、错误上报不吞、`IsReady` 三态、越界坐标不越界写）全部通过；通过总数与分套件计数写入报告。
- R01 negative_check: 测试从源码新构建（先 `clean`）；不得调用已删除符号；不得绕过公共 API 直写内部状态。
- R02 command: `rg 'at24cxx|AT24_|EEPROM' hc-team Debug/makefile Debug/sources.mk`
- R02 expected_exit: 1
- R02 postcondition: 全仓库零命中——EEPROM 源文件与构建登记整体消失。
- R02 negative_check: 不得只删源文件而把构建登记留成悬挂引用。
- R03 command: `rg 'oled_pow|oled_i2c_sda_unlock|OLED_Set_Pos|OLED_DrawBMP|OLED_ColorTurn|OLED_DisplayTurn|OLED_Display_On|OLED_Display_Off' hc-team/driver/oled/oled_hardware_i2c.h hc-team/app`
- R03 expected_exit: 1
- R03 postcondition: 零命中——公共头只剩 §4 六项，且 app 层零依赖被隐藏符号。
- R03 negative_check: 不得把内部件挪到另一个公共头（如新建 `oled_internal.h` 供 app 包含）规避扫描；`rg 'oledfont.h' hc-team/app` 亦须零命中。
- R04 command: `rg -l 'I2C_AUX' hc-team`
- R04 expected_exit: 0
- R04 postcondition: **命中文件仅 `hc-team/driver/oled/oled_hardware_i2c.c` 一个**（独占成立，V17 关闭依据）。命中清单写入报告。
- R04 negative_check: 头文件不得再出现 `I2C_AUX`。
- R05 command: `rg '50000u' hc-team/driver/oled/`
- R05 expected_exit: 1
- R05 postcondition: 零命中；超时常量的推导算式（I2C 400 kHz 时序 + 主频假设）在实现注释中可见，算式与所取值写入报告。
- R05 negative_check: 不得把 50000 改写成等价魔数（如 `0xC350`、`5 * 10000`）冒充推导。
- R06 command: `make -C Debug clean` 后 `make -C Debug all`
- R06 expected_exit: 0
- R06 postcondition: 新鲜产物；map 含 `OLED_ShowString`、`OLED_IsReady`；map **不含** `at24cxx_write`、`at24cxx_read`、`oled_pow`（已 static 或删除）；无新增 warning；`git diff --stat` 只落在合并 allowed_files。
- R06 negative_check: 无旧目标缓存。

### 报告与验收

施工报告一份：R01–R06 每行一行 `Rxx: <command> -> exit <n>, <observed>` + 改动文件清单 + 状态（`CONSTRUCTION_DONE`/`CONSTRUCTION_BLOCKED <任务+原因>`）。拓扑按 §7 契约在验证通过后一次性更新（V17 登记并 closed）。Codex 一次精简验收，结论 `CODEX_ACCEPTED`/`CODEX_REJECTED`。

## 9. 与新引脚表的关系

引脚表（`docs/主控板引脚表(2).xlsx`，2026-07-16 硬件组）第 13/14 行：OLED I2C1 = PA30(SDA)/PA29(SCL)，变更状态"不变"，与当前 `board.syscfg:154-155` 一致。表中**无 EEPROM 器件行**，佐证 EEPROM 删除。

**结论：P6 与引脚表零冲突，可独立推进，不必等待引脚表争议裁定。**

引脚表的冲突全部集中在编码器/灰度/IMU/VOFA，且含阻断级歧义，另立 `plan_pin_table_v2_migration.md`（状态 `blocked`）处理。两个计划无文件重叠：P6 不碰 `board.syscfg`，引脚迁移计划不碰 `driver/oled`。

## 10. 待用户裁定：引脚表文档未入库（P6 验收时发现，不属 P6 范围）

工作树中 `docs/主控板引脚表_G3519.xlsx` 处于已删除状态，新表 `docs/主控板引脚表(2).xlsx` 处于未跟踪状态，两者均**不在 P6 的 allowed_files 内**，Codex 未纳入 P6 提交。

**但这构成一个真实缺口**：`plan_pin_table_v2_migration.md` 已入库，且把 `docs/主控板引脚表(2).xlsx` 声明为其**事实来源**——而该文件不在版本库中，任何人（含未来的 Codex/REASONIX）都无法复核该计划的 Q1–Q4 依据。

**处置（2026-07-16，P6 之后单独提交）**：新表已入库，并新增可读导出 `docs/pin_table_v2/`（Markdown + 两份 CSV，602 格逐格回读比对零失配，不含 Codex 批注）。Q1–Q4 的依据现已可被他人复核，缺口关闭。

**仍待用户裁定**：旧表 `docs/主控板引脚表_G3519.xlsx` 的删除是否为用户本人操作。Codex 未代为提交该删除——删除一份自己没创建、且与当前删除意图存在解释空间的文档，须由用户确认。
