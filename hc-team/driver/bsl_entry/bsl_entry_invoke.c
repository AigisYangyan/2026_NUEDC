/**
 * @file    bsl_entry_invoke.c
 * @brief   BslEntry_InvokeBsl —— 擦 SRAM + 复位进官方 ROM BSL（target-only，永不返回）
 *
 * 单一所有者：进 BSL 的复位序列唯一在此。**主机测试不链本文件**（含内联汇编 + SYSCTL 寄存器，
 * x86 无法编译/运行）；主机侧由 tests/host/fake_bsl_invoke.c 替身计数。上板真实行为由用户验证。
 *
 * 机制照 SDK 权威参考 LP_MSPM0G3507/bsl/bsl_software_invoke_app_demo_uart/main.c 的 invokeBSLAsm()：
 *   ① 按 SRAMFLASH 报告的容量清空整片 SRAM（ECC 区 0x20300000 + 非 ECC 区 0x20200000）——
 *      这是 BSL_ERR_01 勘误的必需绕行（进 BSL 前必须清 SRAM）；
 *   ② 写 DL_SYSCTL_RESET_BOOTLOADER_ENTRY 到 RESETLEVEL、KEY|GO 到 RESETCMD，复位进 BSL。
 */
#include "ti_msp_dl_config.h"

void BslEntry_InvokeBsl(void)
{
    /* 进 BSL 前清空整片 SRAM（BSL_ERR_01 勘误绕行），随后强制复位调 BSL。 */
    __asm(
#if defined(__GNUC__)
        ".syntax unified\n"
#endif
        "ldr     r4, = 0x41C40018\n" /* Load SRAMFLASH register */
        "ldr     r4, [r4]\n"
        "ldr     r1, = 0x03FF0000\n" /* SRAMFLASH.SRAM_SZ mask */
        "ands    r4, r1\n"           /* Get SRAMFLASH.SRAM_SZ */
        "lsrs    r4, r4, #6\n"       /* SRAMFLASH.SRAM_SZ to kB */
        "ldr     r1, = 0x20300000\n" /* Start of ECC-code */
        "adds    r2, r4, r1\n"       /* End of ECC-code */
        "movs    r3, #0\n"
        "init_ecc_loop: \n"          /* Loop to clear ECC-code */
        "str     r3, [r1]\n"
        "adds    r1, r1, #4\n"
        "cmp     r1, r2\n"
        "blo     init_ecc_loop\n"
        "ldr     r1, = 0x20200000\n" /* Start of NON-ECC-data */
        "adds    r2, r4, r1\n"       /* End of NON-ECC-data */
        "movs    r3, #0\n"
        "init_data_loop:\n"          /* Loop to clear ECC-data */
        "str     r3, [r1]\n"
        "adds    r1, r1, #4\n"
        "cmp     r1, r2\n"
        "blo     init_data_loop\n"
        /* Force a reset calling BSL after clearing SRAM */
        "str     %[resetLvlVal], [%[resetLvlAddr], #0x00]\n"
        "str     %[resetCmdVal], [%[resetCmdAddr], #0x00]"
        : /* No outputs */
        : [ resetLvlAddr ] "r"(&SYSCTL->SOCLOCK.RESETLEVEL),
        [ resetLvlVal ] "r"(DL_SYSCTL_RESET_BOOTLOADER_ENTRY),
        [ resetCmdAddr ] "r"(&SYSCTL->SOCLOCK.RESETCMD),
        [ resetCmdVal ] "r"(SYSCTL_RESETCMD_KEY_VALUE | SYSCTL_RESETCMD_GO_TRUE)
        : "r1", "r2", "r3", "r4");
}
