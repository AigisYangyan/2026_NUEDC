#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MSPM0G3519 官方 ROM BSL 一键烧录脚本
====================================

流程（软件触发 / Software Invoke）：
  1. 先编译（rtk make -C Debug all，经 PowerShell）——编译不过就不烧录（用户硬性前提）。
  2. 解析 Debug/2026_Diansai.hex（Intel HEX）→ 程序段。
  3. 打开虚拟串口（口径见 flash_config.ini）。
  4. 发 0x22 → 运行中的固件软件跳转到 ROM BSL。
  5. Connection → 软解锁(默认口令 32×0xFF) → 全片擦除 → 逐包烧录 → 复位运行。

协议依据：TI SDK tools/bsl/BSL_GUI_source_code（MSPM0L/G 家族用 CRC32）。
包格式： 0x80 | 长度(2B 小端) | 命令+payload | CRC32(4B 小端, 校验命令+payload)。

⚠️ 前置条件（固件侧）：本脚本第 4 步发 0x22 要生效，固件必须实现
   「UART0 收到 0x22 → 软件跳 BSL」的监听器（参考 SDK bsl_software_invoke_app_demo_uart）。
   当前工程 board.syscfg 已把 UART0/PA10/PA11@9600 预留为 BSL 口，但该监听器尚未落地
   （见 board.syscfg UART_BSL_ENTRY 注释）。监听器落地前，请用硬件方式进入 BSL，
   或把 config 里 send_invoke 关掉后手动让设备进 BSL。

用法：
  python bsl_flash.py               # 编译→烧录（正常一键流程）
  python bsl_flash.py --dry-run     # 编译+解析+组包，但不开串口（无硬件可验证）
  python bsl_flash.py --no-build    # 跳过编译直接烧录（调试用，绕过硬性前提，慎用）
  python bsl_flash.py --self-test   # 只做 CRC/组包自检，不编译不开串口
"""

import os
import sys
import time
import struct
import subprocess
import configparser

# ---------------------------------------------------------------- 路径
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))   # tools/bsl_flash → 仓库根
CONFIG_PATH = os.path.join(SCRIPT_DIR, "flash_config.ini")

# ---------------------------------------------------------------- BSL 协议常量（MSPM0L/G）
BSL_HEADER = 0x80
CMD_CONNECTION = 0x12
CMD_PASSWORD = 0x21
CMD_MASS_ERASE = 0x15
CMD_PROGRAM = 0x20
CMD_START_APP = 0x40

CHUNK_BYTES = 128          # 每个 Program 包最多 128 字节数据（与 TI GUI 一致）

# ACK 码（第一字节）
ACK_OK = 0x00
ACK_MSG = {
    0x51: "帧头错误(Header incorrect)",
    0x52: "校验错误(Checksum incorrect)",
    0x53: "包长度为零(Packet size zero)",
    0x54: "包过大(Packet size too big)",
    0x55: "未知错误(Unknown error)",
    0x56: "未知波特率(Unknown baud rate)",
}
# 9 字节响应包里 status 字节的含义
STATUS_MSG = {
    0x00: "操作成功",
    0x01: "Flash 编程失败",
    0x02: "全片擦除失败",
    0x04: "BSL 已锁定(BSL locked)",
    0x05: "BSL 口令错误(password error)",
    0x06: "BSL 口令多次错误",
    0x07: "未知命令",
    0x08: "非法内存范围",
    0x0B: "工厂复位被禁用",
    0x0C: "工厂复位口令错误",
}


class BslError(Exception):
    pass


# ---------------------------------------------------------------- CRC32（与 TI 一致，无末尾取反）
def crc32_bsl(data: bytes) -> int:
    crc = 0xFFFFFFFF
    poly = 0xEDB88320
    for b in data:
        crc ^= b
        for _ in range(8):
            mask = -(crc & 1)
            crc = (crc >> 1) ^ (poly & mask)
            crc &= 0xFFFFFFFF
    return crc & 0xFFFFFFFF


def bsl_pack(body: bytes) -> bytes:
    """组一个 BSL 包：0x80 | 长度(2B LE) | body | CRC32(4B LE)。body = 命令+payload。"""
    return (bytes([BSL_HEADER]) + struct.pack("<H", len(body)) + body
            + struct.pack("<I", crc32_bsl(body)))


# ---------------------------------------------------------------- Intel HEX 解析
def parse_intel_hex(path: str):
    """解析 Intel HEX，返回按地址升序、连续合并的 [(addr, bytes), ...] 段列表。"""
    segments = []
    cur_addr = None
    cur = bytearray()
    ext_base = 0
    with open(path, "r", encoding="ascii") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line or line[0] != ":":
                continue
            raw = bytes.fromhex(line[1:])
            if (sum(raw) & 0xFF) != 0:
                raise BslError(f"HEX 行 {lineno} 校验和错误: {line}")
            count = raw[0]
            addr16 = (raw[1] << 8) | raw[2]
            rectype = raw[3]
            data = raw[4:4 + count]
            if rectype == 0x00:            # 数据
                full = ext_base + addr16
                if cur_addr is not None and full == cur_addr + len(cur):
                    cur += data
                else:
                    if cur_addr is not None:
                        segments.append((cur_addr, bytes(cur)))
                    cur_addr = full
                    cur = bytearray(data)
            elif rectype == 0x04:          # 扩展线性地址
                ext_base = ((data[0] << 8) | data[1]) << 16
            elif rectype == 0x02:          # 扩展段地址
                ext_base = ((data[0] << 8) | data[1]) << 4
            elif rectype == 0x01:          # EOF
                break
            # 0x03/0x05（起始地址记录）忽略
    if cur_addr is not None:
        segments.append((cur_addr, bytes(cur)))
    return segments


def build_program_packets(segments):
    """段列表 → Program 包列表。地址 0（向量表）所在的 128B 块最后烧，避免半成品可启动。"""
    chunks = []
    for addr, data in segments:
        for i in range(0, len(data), CHUNK_BYTES):
            chunks.append((addr + i, data[i:i + CHUNK_BYTES]))
    zero = [c for c in chunks if c[0] == 0]
    other = [c for c in chunks if c[0] != 0]
    ordered = other + zero      # 地址 0 块置于末尾
    packets = []
    for addr, data in ordered:
        body = bytes([CMD_PROGRAM]) + struct.pack("<I", addr) + data
        packets.append((addr, len(data), bsl_pack(body)))
    return packets


# ---------------------------------------------------------------- 串口事务
def _read_exact(ser, n, what):
    buf = ser.read(n)
    if len(buf) != n:
        raise BslError(f"{what}：串口读取超时（期望 {n} 字节，实得 {len(buf)}）")
    return buf


def bsl_ack(ser, what):
    ack = _read_exact(ser, 1, what + " 的 ACK")
    if ack[0] != ACK_OK:
        raise BslError(f"{what} 被 BSL 拒绝：ACK=0x{ack[0]:02X} {ACK_MSG.get(ack[0], '未知')}")


def bsl_response(ser, what):
    resp = _read_exact(ser, 9, what + " 的响应包")
    status = resp[4]
    if status != ACK_OK:
        raise BslError(f"{what} 失败：status=0x{status:02X} {STATUS_MSG.get(status, '未知')}")


def bsl_txn(ser, packet, what, expect_response):
    ser.write(packet)
    bsl_ack(ser, what)
    if expect_response:
        bsl_response(ser, what)


# ---------------------------------------------------------------- 配置
def load_config():
    cfg = configparser.ConfigParser(inline_comment_prefixes=(";", "#"))
    if not os.path.exists(CONFIG_PATH):
        raise BslError(f"找不到配置文件：{CONFIG_PATH}")
    cfg.read(CONFIG_PATH, encoding="utf-8")

    def get(sec, key, default):
        try:
            return cfg.get(sec, key)
        except (configparser.NoSectionError, configparser.NoOptionError):
            return default

    def getbool(sec, key, default):
        try:
            return cfg.getboolean(sec, key)
        except (configparser.NoSectionError, configparser.NoOptionError, ValueError):
            return default

    pw_raw = get("bsl", "password", "").replace("0x", "").replace(",", " ")
    pw_hex = "".join(pw_raw.split())
    if pw_hex:
        password = bytes.fromhex(pw_hex)
        if len(password) != 32:
            raise BslError(f"password 必须是 32 字节（当前 {len(password)}）——软解锁默认全 FF")
    else:
        password = b"\xFF" * 32          # 默认口令：软解锁

    return {
        "port": get("serial", "port", "COM7").strip(),
        "baud": int(get("serial", "baudrate", "9600")),
        "timeout": float(get("serial", "read_timeout_s", "5")),
        "password": password,
        "send_invoke": getbool("bsl", "send_invoke", True),
        "invoke_byte": int(get("bsl", "invoke_byte", "0x22"), 0),
        "invoke_delay": float(get("bsl", "invoke_delay_s", "0.3")),
        "mass_erase": getbool("bsl", "mass_erase", True),
        "start_app": getbool("bsl", "start_app", True),
        "run_build": getbool("build", "run_build", True),
        "build_cmd": get("build", "build_cmd", "rtk make -C Debug all").strip(),
        "hex_file": get("build", "hex_file", "Debug/2026_Diansai.hex").strip(),
    }


# ---------------------------------------------------------------- 编译前提门
def run_build(build_cmd: str) -> bool:
    print(f"[编译] {build_cmd}（经 PowerShell，工作目录 {REPO_ROOT}）")
    print("-" * 60)
    # CLAUDE.md：make 必须经 PowerShell；Bash 里调 make.bat 会静默假绿。
    proc = subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", build_cmd],
        cwd=REPO_ROOT,
    )
    print("-" * 60)
    if proc.returncode != 0:
        print(f"[编译] 失败（exit {proc.returncode}）——按前提，终止烧录。")
        return False
    print("[编译] 通过。")
    return True


# ---------------------------------------------------------------- 自检
def self_test():
    import zlib
    ok = True
    for sample in [b"\x12", b"\x21" + b"\xFF" * 32, bytes(range(20))]:
        mine = crc32_bsl(sample)
        ref = zlib.crc32(sample) ^ 0xFFFFFFFF     # TI 的 crc32 == zlib.crc32 少最后一次取反
        if mine != (ref & 0xFFFFFFFF):
            print(f"[自检] CRC32 不匹配：{sample.hex()} mine={mine:08X} ref={ref & 0xFFFFFFFF:08X}")
            ok = False
    conn = bsl_pack(bytes([CMD_CONNECTION]))
    if not (conn[0] == 0x80 and conn[1:3] == b"\x01\x00" and conn[3] == 0x12 and len(conn) == 8):
        print(f"[自检] Connection 包结构异常：{conn.hex()}")
        ok = False
    print("[自检] " + ("全部通过 ✓" if ok else "存在失败 ✗"))
    return ok


# ---------------------------------------------------------------- 主流程
def main():
    args = set(sys.argv[1:])
    dry_run = "--dry-run" in args
    no_build = "--no-build" in args

    print("=" * 60)
    print(" MSPM0G3519 官方 ROM BSL 一键烧录")
    print("=" * 60)

    if "--self-test" in args:
        return 0 if self_test() else 1

    if not self_test():
        return 1

    cfg = load_config()

    # 1) 编译前提门
    if cfg["run_build"] and not no_build:
        if not run_build(cfg["build_cmd"]):
            return 1
    else:
        print("[编译] 已跳过（--no-build 或 run_build=false）。")

    # 2) 解析固件
    hex_path = os.path.join(REPO_ROOT, cfg["hex_file"])
    if not os.path.exists(hex_path):
        print(f"[错误] 找不到固件：{hex_path}")
        return 1
    segments = parse_intel_hex(hex_path)
    packets = build_program_packets(segments)
    total = sum(n for _, n, _ in packets)
    lo = min(a for a, _ in segments)
    hi = max(a + len(d) for a, d in segments)
    print(f"[固件] {cfg['hex_file']}：{len(segments)} 段，{total} 字节，"
          f"地址 0x{lo:08X}–0x{hi:08X}，共 {len(packets)} 个 Program 包。")

    if dry_run:
        print("[dry-run] 未开串口。组包正常，可交付。")
        print(f"[dry-run] 首包 {packets[0][2][:8].hex()}… 末包(含地址0) 地址=0x{packets[-1][0]:08X}")
        return 0

    # 3) 打开串口
    try:
        import serial
    except ImportError:
        print("[错误] 缺少 pyserial：请 `pip install pyserial`")
        return 1

    print(f"[串口] 打开 {cfg['port']} @ {cfg['baud']} 8N1 ...")
    try:
        ser = serial.Serial(cfg["port"], cfg["baud"], bytesize=8,
                            parity=serial.PARITY_NONE, stopbits=1,
                            timeout=cfg["timeout"])
    except serial.SerialException as e:
        print(f"[错误] 打开串口失败：{e}")
        print("       → 请把 flash_config.ini 里的 port 改成你的虚拟串口号（设备管理器可查）。")
        return 1

    try:
        # 4) 软件触发进 BSL
        if cfg["send_invoke"]:
            print(f"[BSL] 发送触发字节 0x{cfg['invoke_byte']:02X}，等待设备跳入 BSL ...")
            ser.reset_input_buffer()
            ser.write(bytes([cfg["invoke_byte"]]))
            ser.read(1)                       # 丢弃固件可能的回应
            time.sleep(cfg["invoke_delay"])

        # 5) Connection（只回 ACK，无响应包）
        print("[BSL] Connection ...")
        try:
            bsl_txn(ser, bsl_pack(bytes([CMD_CONNECTION])), "Connection", expect_response=False)
        except BslError as e:
            print(f"[错误] {e}")
            print("       → 最可能原因：设备没进 BSL。若固件尚未实现 0x22 监听器，")
            print("         请先用硬件方式进 BSL（NRST+Invoke 引脚）再重试。")
            return 1
        print("[BSL] 已进入 BSL。")

        # 6) 软解锁
        print("[BSL] 解锁（发送口令）...")
        bsl_txn(ser, bsl_pack(bytes([CMD_PASSWORD]) + cfg["password"]), "解锁", expect_response=True)

        # 7) 全片擦除
        if cfg["mass_erase"]:
            print("[BSL] 全片擦除 ...")
            bsl_txn(ser, bsl_pack(bytes([CMD_MASS_ERASE])), "全片擦除", expect_response=True)

        # 8) 逐包烧录
        print(f"[BSL] 烧录 {len(packets)} 包 ...")
        for i, (addr, n, pkt) in enumerate(packets, 1):
            bsl_txn(ser, pkt, f"Program#{i}@0x{addr:08X}", expect_response=True)
            if i % 20 == 0 or i == len(packets):
                print(f"      {i}/{len(packets)} 包 ...")
        print("[BSL] 固件烧录完成。")

        # 9) 复位运行
        if cfg["start_app"]:
            print("[BSL] 复位运行新固件 ...")
            ser.write(bsl_pack(bytes([CMD_START_APP])))
            ser.read(1)

        print("=" * 60)
        print(" ✓ 烧录成功！")
        print("=" * 60)
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BslError as e:
        print(f"[失败] {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n[中断] 用户取消。")
        sys.exit(130)
