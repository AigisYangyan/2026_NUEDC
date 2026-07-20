# VOFA 串口：上位机改参数命令 & FireWater 帧格式

> 权威实现：`hc-team/driver/uart_vofa/uart_vofa.c`（本文所有规则均以该文件为准，改代码后请同步本文）。
> 物理链路：`UART_HOST_LINK` = UART5 / PA1(TX) / PA0(RX) / **230400 8N1** / DMA（`board.syscfg` 单源）。

本文回答一个问题：**上位机（VOFA+ 或任意串口助手）往下位机发什么，才能在线改一个变量？**

---

## 1. 一句话

给下位机发一行文本：

```
名字=数值\n
```

例如发 `LP=1.5`（回车），下位机里绑定到 `LP` 的那个 `float` 变量立刻变成 `1.5`。

> ⚠️ 接收命令格式与 `VOFA_PROTOCOL_SELECT` **无关**。那个宏只决定「下位机→上位机」的发送格式
> （当前 = `JUSTFLOAT` 二进制）。无论上行是 JustFloat 还是 FireWater，**改参数的下行命令永远是这套
> `名字=数值` 文本**。在 VOFA+ 里用 FireWater 通道的「命令」输入框发即可。

---

## 2. 命令帧格式（host → MCU）

一帧 = 一行文本，以 **行结束符** 收尾；一行里可以塞多个赋值。

```
帧      = 赋值 ( 分隔符 赋值 )* 结束符
赋值    = 名字 ( '=' | ':' ) 数值
分隔符  = ','                      // 一行内多个赋值之间
结束符  = '\n' | '\r' | ';'        // 触发本帧解析
```

| 元素 | 规则（源：`uart_vofa.c`） |
|---|---|
| **结束符** | `\n`、`\r`、`;` 任一都会立即解析当前行（`vofa_process_rx_byte`）。用串口助手时勾选「发送新行」即可。 |
| **行内分隔** | 逗号 `,`。一行可发多个：`LP=1.2,LI=0.02,LD=3`（`vofa_parse_rx_frame`）。 |
| **键值分隔** | `=` 或 `:` 都行：`LP=1.5` 等价 `LP:1.5`（`vofa_parse_rx_token`）。 |
| **名字** | **大小写不敏感**：`lp`、`LP`、`Lp` 等价（`vofa_cmd_equals_ignore_case`）。 |
| **数值** | 按浮点解析（`atof`）：支持整数 `LP=2`、小数 `LP=1.5`、负号 `SG=-1`、科学计数 `KI=1e-3`。 |
| **空格** | 名字/数值两侧空格自动去除：`LP = 1.5` 也认（`vofa_trim_inplace`）。 |

### 例子

```
LM=0.5                 // 左轮目标 0.5 m/s
LM=0.5,RM=0.5          // 一行设左右目标
LP=1.2,LI=0.02,LD=3    // 一行调左轮 PID 三个增益
SG=-1                  // 负值合法
RUN=1                  // 触发型命令：写 1 让 Service 执行一次
```

---

## 3. 约束与坑（全部来自实测，不是通用 VOFA 文档）

- **单行 ≤ 63 字符。** 超长 → 该行连同本次 `vofa_run()` 周期剩余的接收字节一起被丢弃
  （`g_rx_drop_for_run`）。别把几十个赋值挤成一行，超了会静默吃掉。
- **未知名字、或没有 `=`/`:` 的 token → 静默忽略。** 打错名字不会报错，只是没反应。
- **数值必须是数字。** `RUN=on` 会被 `atof` 解析成 `0.0`（把变量清零！）。触发请发 `RUN=1`。
- **解析只在 `vofa_run()` 任务态发生**，ISR 只把字节搬进 FIFO（V09）。所以命令的生效延迟 =
  一个 `vofa_run` 调用周期，不是逐字节即时。
- **绑定上限 32 条**（`VOFA_RX_PARAM_MAX`）。
- **改的是「绑定变量」，不等于「立刻动作」。** 命令把值写进一个 `volatile float`；这个值何时被
  消费（喂进速度环 / 触发一次运动）由对应 Service 决定。触发型变量（如 `RUN`/`SAVE`/`PID_APPLY`）
  是 Service 读到非 0 后执行并自行清零。

---

## 4. 当前有哪些名字可用？

命令名不是固定的——由**当前激活的 Service** 用 `vofa_bind_cmd("名字", &变量)` 注册。
权威清单永远是那个 Service 源码里的 `vofa_bind_cmd` 调用。

**现役示例：底盘调参 Service `app/service/tuning/tuning_chassis.c`**

| 名字 | 改的变量 | 含义 |
|---|---|---|
| `LM` / `RM` | 左/右轮目标速度 | m/s |
| `LP` / `LI` / `LD` | 左轮 PID | kp / ki / kd |
| `RP` / `RI` / `RD` | 右轮 PID | kp / ki / kd |

其它 profile（见 `app/scheduler/vofa_register.c`）会注册不同名字，例如通用 PID 组
`KP/KI/KD/IL/OL/SP/AC/SG`、步进平滑调试组 `AX/MODE/RUN/DIR/V/A/KP/KI/KD/SAVE/PID_APPLY` 等——
**具体以当轮激活 Service 的绑定为准**。

> 注：现场动态调参的推荐路径已转向 `param_tune` + `param_store`（掉电保存）参数组；
> `vofa_bind_cmd` 仍是"临时/少量在线改值"的最短路，两者可并存。

---

## 5. FireWater 下行数据帧（MCU → host，选看）

「FireWater 协议」本身指 VOFA+ 的**文本 CSV 数据通道**。若你把 `uart_vofa.h` 的
`VOFA_PROTOCOL_SELECT` 改成 `VOFA_PROTOCOL_FIREWATER`，下位机上行帧格式为：

```
v0,v1,v2,...,vN\n
```

- 每个值 `"%.2f"`，逗号分隔，**行尾换行 `\n`**（`vofa_pack_firewater`）。
- 通道来源与顺序 = `vofa_register_float()` / `vofa_register_int()` 的注册顺序。
- 与命令帧独立：上行发 CSV，下行仍用第 2 节的 `名字=数值` 收命令。

（当前工程 `VOFA_PROTOCOL_SELECT = JUSTFLOAT`，上行是二进制浮点 + 帧尾 `00 00 80 7F`；
若上位机要画波形又要文本可读，才切 FireWater。）

---

## 6. 实操（VOFA+）

1. 连 UART5，波特率 **230400**，8N1。
2. 通道协议选 **FireWater**。
3. 在命令/发送框输入 `LP=1.5` 回车（或串口助手发 `LP=1.5\r\n`，勾「发送新行」）。
4. 想一次改多个：`LP=1.2,LI=0.02,LD=3` 回车。
