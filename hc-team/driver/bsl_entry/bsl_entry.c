/**
 * @file    bsl_entry.c
 * @brief   BSL 入口触发判定（可移植逻辑；不含 DL HAL/asm，可主机链接）
 *
 * 单一所有者：触发字节常量 BSL_ENTRY_TRIGGER_BYTE 唯一定义在此。
 * 实际的 SRAM 擦除 + 复位进 BSL 序列在边界文件 bsl_entry_invoke.c（BslEntry_InvokeBsl）。
 */
#include "driver/bsl_entry/bsl_entry.h"

/* 触发字节：主机烧录脚本 tools/bsl_flash 运行期下发的软触发字节（唯一所有者）。 */
#define BSL_ENTRY_TRIGGER_BYTE  0x22u

void BslEntry_IsrOnByte(uint8_t byte)
{
    if (byte == BSL_ENTRY_TRIGGER_BYTE) {
        /* 命中软触发字节：跳官方 ROM BSL，永不返回（ISR 内直接触发，契约 D14 豁免）。 */
        BslEntry_InvokeBsl();
    }
}
