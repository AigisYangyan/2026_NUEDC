# MSPM0G3507 → MSPM0G3519 移植配方

> 源工程:`NUEDC`(MSPM0G3507, LQFP-64)→ 目标工程:`2026_Diansai`(MSPM0G3519, LQFP-100(PZ))
> 原则:**除编码器外,所有引脚名、外设角色、波特率、DMA 通道、时钟树、编译旗标与旧版完全一致**;编码器由 GROUP1 软件中断计数升级为两路硬件 QEI。
> 移植日期:2026-07-16。

## 1. 版本与工具链

| 项 | 旧 (NUEDC) | 新 (2026_Diansai) |
|---|---|---|
| 器件 | MSPM0G3507, LQFP-64(PM) | MSPM0G3519, LQFP-100(PZ) |
| Flash / SRAM | 128KB / 32KB | 512KB / 128KB(SRAM 分 BANK0/BANK1 两个 64KB) |
| SDK | mspm0_sdk 2.10.00.04 | mspm0_sdk **2.11.00.07** |
| SysConfig | 1.26.2 | 1.26.2(不变) |
| 编译器 | TI Clang 4.0.4.LTS(-O0 -Wall, soft float) | 完全相同(新工程原为 GCC 模板,已改回 ticlang) |
| 调试器 | SEGGER J-Link | J-Link(默认,`MSPM0G3519_JLink.ccxml`);另保留 XDS110 配置 |
| 启动文件/链接脚本 | SysConfig/CCS 按器件自动生成 | 同左,自动切到 `startup_mspm0g351x_ticlang.c` + 512KB 链接脚本 |

## 2. 引脚映射:全部同名保留(编码器除外)

以下引脚在 G3519 LQFP-100 上**逐一经 SysConfig 求解器验证合法**,与旧版完全一致:

| 功能组 | 外设实例 | 引脚(与旧版相同) |
|---|---|---|
| UART_HOST_LINK(VOFA 主机链路, 230400) | UART0 | TX=PA10, RX=PA11 |
| UART_VISION(视觉, 230400) | UART1 | TX=PA8, RX=PA9 |
| UART_STEPPER_BUS(Emm42 步进, 230400) | **UART7(见 §3)** | TX=PB15, RX=PB16 |
| UART_IMU(230400) | UART3 | TX=PA26, RX=PA25 |
| PWM_DRIVE_LEFT(10kHz) | TIMA0 CCP1 | PA22 |
| PWM_DRIVE_RIGHT(10kHz) | TIMA1 CCP0 | PB2 |
| I2C_AUX(OLED, Fast) | I2C1 | SDA=PA30, SCL=PA29 |
| I2C_IMU(Fast) | I2C0 | SDA=PA28, SCL=PA31 |
| 按键 K1/K2/K3/K4 | GPIO 下降沿中断 | PB4 / PB5 / PB25 / PA14 |
| 灰度 8 路 IN1–IN8 | GPIO 输入 | PB13, PB17, PB26, PB19, PB21, PB10, PB11, PB24 |
| 电机方向 BIN1/BIN2/AIN1/AIN2 | GPIO 输出 | PA17 / PA24 / PA13 / PA12 |
| 状态 LED | GPIO | PB22 |
| SWD | DEBUGSS | SWCLK=PA20, SWDIO=PA19 |
| DMA 通道 | CH0–CH6(7 条) | 与旧版通道号一一对应,`runtime_dma_irq_mask` 0–6 覆盖不变 |

时钟树(SYSOSC→SYSPLL→80MHz CPUCLK)、PWM 计数值 7999、所有 FIFO/DMA 触发配置逐项照搬,未做任何改动。

## 3. 唯一被迫的实例改名:UART2 → UART7

**G3519 没有 UART2 这个外设实例**(有效实例:UART0/1/3/4/5/6/7)。旧步进电机总线的引脚 PB15(TX)/PB16(RX) 在 G3519 上由 **UART7** 复用。因此:

- `board.syscfg`:`UART_STEPPER_BUS` 的 `peripheral.$assign` 由 `UART2` 改为 `UART7`,**引脚不变**。
- `board.c`:NVIC 使能改用 SysConfig 生成的器件无关宏(`UART_STEPPER_BUS_INST_INT_IRQN` 等),彻底消除对实例编号的硬编码。外部接线、波特率、协议零变化。

## 4. 编码器:软件中断 → 硬件 QEI(本次唯一功能变更)

### 4.1 为什么必须换引脚

G3519 恰有两个 QEI 定时器:**TIMG8 和 TIMG9**。QEI 只能从各自定时器的 CCP0/CCP1 引脚输入。旧编码器 4 脚中只有 PA7 可复用为 TIMG8.CCP0,其余 3 脚(PB20/PB14/PB0)在 G3519 上无任何 QEI 复用,故必须迁移:

| 信号 | 旧引脚(软件中断) | 新引脚(硬件 QEI) | 复用功能 |
|---|---|---|---|
| 左轮 A 相 (PHA) | PA7 | **PA7(不变)** | TIMG8.CCP0 |
| 左轮 B 相 (PHB) | PB20 | **PA6** | TIMG8.CCP1 |
| 右轮 A 相 (PHA) | PB14 | **PA3** | TIMG9.CCP0 |
| 右轮 B 相 (PHB) | PB0 | **PA2** | TIMG9.CCP1 |

选型理由:PA7/PA6、PA3/PA2 均为相邻引脚便于布线,与本表其他所有功能零冲突;避开了 PA18(BSL 引导脚)和 PA0/PA1(开漏 IO)。
注意:PA5/PA6 兼作 HFXT 晶振脚、PA3/PA4 兼作 LFXT 晶振脚——本工程时钟全部来自内部 SYSOSC/SYSPLL,不用外部晶振,因此安全;**若未来要上外部晶振,需重选 QEI 引脚**。

### 4.2 软件设计(接口零变化)

- 抽象不变:`Encoder_Init/Update/GetSnapshot`、`BoardGpio_GetEncoderRawSnapshot`、`Mspm0Runtime_GetEncoderCounts` 的签名与语义全部保留,`encoder.c`、`board_gpio.c` 及以上各层**一行未改**。
- 隐藏的实现:`mspm0_runtime.c` 删除 GROUP1 内的四脚边沿计数(`runtime_handle_encoder_irqs`),改为在 `Mspm0Runtime_GetEncoderCounts()` 读取 QEI 硬件计数器;16 位计数器(LOAD=65535)通过"无符号模差 + memcpy 位重解释"扩展为 int32 累计值(与 encoder.c 既有的 UB-free 风格一致)。
- 前提条件:两次读数间位移 < 32767 计数(约 21 圈轮转,周期任务毫秒级采样,余量 3 个数量级)。
- 计数器启动:`Board_Init()` 在 `SYSCFG_DL_init()` 后执行 `DL_TimerG_startCounter(QEI_LEFT_INST / QEI_RIGHT_INST)`。
- GROUP1 中断仍保留,仅服务按键;`board.c` 的 GPIOA/GPIOB/DMA NVIC 使能不变。
- 不再产生每个编码器边沿一次的 CPU 中断——这是本次升级的全部收益(高速时 CPU 占用大幅下降,且无丢步风险)。

### 4.3 上板必做的两项校准(软件无法离线验证)

1. **方向校准**:旧版左右轮在 ISR 里用了互为镜像的判向逻辑,硬件 QEI 两路判向约定相同,因此**至少一侧轮子的计数方向可能与旧版相反**。上电后手推小车前进,VOFA 观察 `total_pulses` 左右都应为正;若某侧为负,对调该侧 A/B 两根信号线(或在 `board.syscfg` 对调该 QEI 的 ccp0Pin/ccp1Pin 再重新生成)。`encoder.c` 的 `s_direction_sign = {-1, +1}` 保持不动,作为全链路唯一方向修正点。
2. **PPR 核对**:旧软件是 4 倍频计数(4 脚双沿),MSPM0 QEI 2 输入模式同为 4 倍频,理论上 `s_ppr=1560` 不变;仍建议手转一整圈核对 `total_pulses` ≈ ±1560,若差 2 倍/4 倍按实测改 `s_ppr`。

## 5. 变更文件清单

| 文件 | 变更 |
|---|---|
| `board.syscfg` | 器件/封装/SDK 切换;全部引脚由"建议解"改为硬 `$assign` 锁定;删除 GPIO_ENCODERS 组;新增 QEI_LEFT/QEI_RIGHT;UART2→UART7 |
| `hc-team/driver/mspm0_runtime/mspm0_runtime.c` | 删除软件编码器计数;新增 QEI 16→32 位累计读数;GROUP1 只留按键 |
| `hc-team/driver/mspm0_runtime/mspm0_runtime.h` | 仅注释更新(QEI 语义与调用频率前提) |
| `hc-team/driver/board/board.c` | 启动两个 QEI 计数器;UART NVIC 改器件无关宏 |
| `.cproject` | 以旧工程 ticlang 配置为蓝本:器件 G3519、SDK 2.11、include 路径指向工程内 `hc-team` |
| `.ccsproject` | ticlang 模板、J-Link 连接、激活 `MSPM0G3519_JLink.ccxml` |
| `targetConfigs/MSPM0G3519_JLink.ccxml` | 新增(默认);原 XDS110 版 `MSPM0G3519.ccxml` 保留备用 |
| `hc-team/`(其余全部) | 从 NUEDC 原样复制,零改动 |
| `empty.c` / `empty.syscfg` | 删除(模板残留) |

## 6. 已完成的离线验证(2026-07-16)

1. **SysConfig CLI**(1.26.2 + SDK 2.11.00.07,ticlang):`board.syscfg` 求解 **0 error / 0 warning**——全部引脚复用合法、无 pinmux 冲突、DMA CH0–CH6 保持原编号。
2. **全量编译**:tiarmclang 4.0.4(与 CCS 相同旗标 `-mcpu=cortex-m0plus -mthumb -O0 -Wall @device.opt`)编译全部 33 个应用源文件 + 生成的 `ti_msp_dl_config.c` + `startup_mspm0g351x_ticlang.c`:**0 error / 0 warning**。
3. **完整链接**:使用生成的 `device_linker.cmd`(FLASH 512KB @0x0,SRAM_BANK0/1 各 64KB)+ `mspm0gx51x/driverlib.a` 链接出完整 `.out`,中断向量表、`GROUP1/DMA/UART7_IRQHandler`、QEI 读数路径符号齐全。

CCS 里首次构建即等价重跑以上三步(SysConfig 生成 → 编译 → 链接),预期直接通过。

## 7. 上板烟雾测试清单(按依赖序)

| # | 模块 | 验证点 |
|---|---|---|
| 1 | clock | SysTick 1ms,`Clock_NowMs()` 递增 |
| 2 | board | `SYSCFG_DL_init` 通过,LED(PB22)可控 |
| 3 | key | 4 键下降沿事件(GROUP1 仅按键路径) |
| 4 | **encoder(QEI)** | §4.3 两项校准;正反转计数对称;高速旋转无丢计数 |
| 5 | motor + PWM | PA22/PB2 10kHz 波形,slew 状态机安全行为不变 |
| 6 | uart_vofa | UART0 DMA 收发,VOFA 曲线正常 |
| 7 | step_motor | **UART7** 230400 与 Emm42 收发帧(引脚未变,重点回归) |
| 8 | vision | UART1 帧解析 |
| 9 | IMU | UART3 / I2C0 通信 |
| 10 | OLED | I2C1 刷屏 |
| 11 | speed_loop | 闭环转速跟随(依赖 4/5 通过) |

## 8. 已知差异与遗留注意

- **SRAM_BANK1(0x20210000 起 64KB)在深于 SLEEP 的低功耗模式会掉电**。本工程不进低功耗,无影响;若未来加低功耗需注意链接段分布。
- 旧版 `board.c` 未使能 UART0(HOST_LINK)的 NVIC(其收发全走 DMA_IRQ),此行为**原样保留**。
- `docs/Board_cfg.md.md`(旧仓库)灰度写 12 路且引脚与 syscfg 不符,属旧文档既有问题,本表 §2 以 syscfg 为准。
- 新板若用 LP-MSPM0G3519(板载 XDS110)调试,把激活目标配置切回 `targetConfigs/MSPM0G3519.ccxml` 即可。

## 9. 回滚

旧工程 `NUEDC` 未被触碰,随时可回。新工程为独立 git 仓库,首个提交即完整可编译基线。
