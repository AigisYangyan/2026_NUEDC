/**
 * @file    param_store_hw.c
 * @brief   参数存储 Driver 的真实硬件实现（本模块唯一允许包含 TI 头的文件）。
 *
 * 片内 flash：保留**最后一个 1KB 扇区** 0x0007FC00（FLASH 共 512KB=0x80000，见
 * Debug/device_linker.cmd）。本工程代码远不及 508KB，末扇区不与 .text/.const 冲突。
 * 若日后代码逼近末扇区，须在链接脚本显式 carve 出本扇区（belt-and-suspenders）。
 *
 * 擦/写序列照搬 SDK eeprom_emulation_type_a（executeClearStatus → unprotectSector →
 * eraseMemoryFromRAM / programMemoryFromRAM64WithECCGenerated）：FromRAM 变体为 .TI.ramfunc，
 * 在同 bank 擦写自身 flash 安全；64-bit + ECC 为 MSPM0 编程粒度。
 *
 * ★ 真实 flash 擦/写在硬件上验证（用户上板自理，§Phase1.6 无硬件证据行）；
 *   本文件不进主机链接（主机用 fake_param_store_port.c 顶替）。
 */
#include "driver/param_store/param_store_port.h"

#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_flashctl.h>
#include <string.h>

#define PARAM_STORE_FLASH_ADDR  0x0007FC00u  /* 末 1KB 扇区起址 */
#define PARAM_STORE_SECTOR_SIZE 1024u

uint16_t param_store_port_capacity(void)
{
    return PARAM_STORE_SECTOR_SIZE;
}

void param_store_port_read(uint16_t offset, uint8_t *dst, uint16_t len)
{
    memcpy(dst, (const void *)(PARAM_STORE_FLASH_ADDR + offset), len);
}

bool param_store_port_erase(void)
{
    DL_FlashCTL_executeClearStatus(FLASHCTL);
    DL_FlashCTL_unprotectSector(
        FLASHCTL, PARAM_STORE_FLASH_ADDR, DL_FLASHCTL_REGION_SELECT_MAIN);
    return DL_FlashCTL_eraseMemoryFromRAM(
               FLASHCTL, PARAM_STORE_FLASH_ADDR,
               DL_FLASHCTL_COMMAND_SIZE_SECTOR) != DL_FLASHCTL_COMMAND_STATUS_FAILED;
}

bool param_store_port_program(uint16_t offset, const uint8_t *src, uint16_t len)
{
    uint16_t i;

    /* param_store.c 保证 offset/len 均 8 字节对齐；逐 64-bit 字编程（ECC 生成）。 */
    for (i = 0u; i < len; i = (uint16_t)(i + 8u)) {
        uint32_t word[2];
        uint32_t addr = PARAM_STORE_FLASH_ADDR + offset + i;

        memcpy(word, &src[i], 8u);
        DL_FlashCTL_executeClearStatus(FLASHCTL);
        DL_FlashCTL_unprotectSector(FLASHCTL, addr, DL_FLASHCTL_REGION_SELECT_MAIN);
        if (DL_FlashCTL_programMemoryFromRAM64WithECCGenerated(FLASHCTL, addr, word) ==
            DL_FLASHCTL_COMMAND_STATUS_FAILED) {
            return false;
        }
    }
    return true;
}
