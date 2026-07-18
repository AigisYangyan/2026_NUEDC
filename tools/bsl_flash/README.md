# MSPM0G3519 一键烧录（官方 ROM BSL）

双击 **`一键烧录.bat`** → 先编译（`rtk make -C Debug all`），**编译通过后**才通过 UART0
用官方 ROM BSL 把固件烧进去。烧录方式 = 软件触发（发 `0x22` 让固件跳 BSL）。

## 你通常只需要做一件事

打开 `flash_config.ini`，把 `port` 改成你的虚拟串口号（设备管理器 → 端口(COM 和 LPT)）：

```
[serial]
port = COM7      ; ← 改成你的
```

然后双击 `一键烧录.bat`。就这样。

## 文件

| 文件 | 作用 |
|---|---|
| `一键烧录.bat` | 双击入口（编译门 + 烧录） |
| `flash_config.ini` | 口径配置：串口号 / 波特率 / 口令 / 开关（**给你编辑的文件**） |
| `bsl_flash.py` | 烧录逻辑（Intel HEX 解析 + BSL 协议） |

## 命令行（可选）

```
python bsl_flash.py              # 编译 → 烧录（= 双击 bat）
python bsl_flash.py --dry-run    # 编译+解析+组包，不开串口（无板子也能验证）
python bsl_flash.py --no-build   # 跳过编译直接烧（调试用，绕过前提，慎用）
python bsl_flash.py --self-test  # 只做 CRC/组包自检
```

## 协议 / 口径（已与工程对齐）

- **UART0 / PA10 / PA11 @ 9600 8N1**——ROM BSL 固定值（`board.syscfg` 已预留此口）。
- 包格式：`0x80 | 长度(2B LE) | 命令+payload | CRC32(4B LE)`，CRC32 = `0xEDB88320`/init`0xFFFFFFFF`。
- 序列：`0x22` → Connection(`0x12`) → 解锁(`0x21`,默认 32×`0xFF`) → 全片擦除(`0x15`)
  → 逐包 Program(`0x20`) → 复位运行(`0x40`)。
- 依据 TI SDK `tools/bsl/BSL_GUI_source_code`（MSPM0L/G 家族）。
- 向量表（地址 0）所在块**最后**烧，烧录中断也不会留下"看起来能启动"的半成品。

## ⚠️ 重要前提：固件侧的 0x22 监听器

本脚本发 `0x22` 要生效，**固件必须实现「UART0 收到 0x22 → 软件跳 BSL」**
（参考 SDK `bsl_software_invoke_app_demo_uart`）。

当前工程 `board.syscfg` 已把 UART0 预留为 BSL 口，但 **该监听器尚未落地**
（见 `board.syscfg` 内 `UART_BSL_ENTRY` 注释："中断待 ENTRY 监听器落地时再开"）。
在监听器落地前：

- 方案 A（推荐，后续做）：给固件加 0x22 监听器 → 之后本脚本即可真正一键。
- 方案 B（临时）：用**硬件方式**让设备进 BSL（NRST + Invoke 引脚），
  再把 `flash_config.ini` 里 `send_invoke` 设为 `false`，双击 bat 只跑「解锁→擦除→烧录」。

## 依赖

- Python 3 + `pyserial`（已装：pyserial 3.5）。缺了就 `pip install pyserial`。
- `rtk` 在 PATH（编译门用）。
