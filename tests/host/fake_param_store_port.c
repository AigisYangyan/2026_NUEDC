/**
 * @file    fake_param_store_port.c
 * @brief   param_store_port.h 的主机假件，顶替 param_store_hw.c。
 *
 * RAM 数组模拟片内扇区：擦=全 0xFF；program 按 NOR 语义只清位（&= src），故「未擦先写」
 * 会被忠实暴露（脏位留存）。提供擦/写失败注入以验 Save 的失败路径，Poke 直写以验 CRC 校验。
 */
#include "driver/param_store/param_store_port.h"

#include <string.h>

#define FAKE_PARAM_STORE_CAP 1024u

static uint8_t s_flash[FAKE_PARAM_STORE_CAP];
static bool    s_erase_fail;
static bool    s_program_fail;

void FakeParamStorePort_Reset(void)
{
    memset(s_flash, 0xFF, sizeof s_flash);   /* 擦除态 */
    s_erase_fail = false;
    s_program_fail = false;
}

void FakeParamStorePort_SetEraseFail(bool fail)
{
    s_erase_fail = fail;
}

void FakeParamStorePort_SetProgramFail(bool fail)
{
    s_program_fail = fail;
}

/** 直写一个字节（绕过 NOR &= 语义），用于注入 payload 位翻转以验 CRC 拒绝。 */
void FakeParamStorePort_Poke(uint16_t offset, uint8_t value)
{
    if (offset < FAKE_PARAM_STORE_CAP) {
        s_flash[offset] = value;
    }
}

uint16_t param_store_port_capacity(void)
{
    return FAKE_PARAM_STORE_CAP;
}

bool param_store_port_erase(void)
{
    if (s_erase_fail) {
        return false;
    }
    memset(s_flash, 0xFF, sizeof s_flash);
    return true;
}

bool param_store_port_program(uint16_t offset, const uint8_t *src, uint16_t len)
{
    uint16_t i;

    if (s_program_fail) {
        return false;
    }
    if ((uint32_t)offset + (uint32_t)len > FAKE_PARAM_STORE_CAP) {
        return false;
    }
    for (i = 0u; i < len; i++) {
        s_flash[offset + i] &= src[i];       /* NOR：只能 1→0 */
    }
    return true;
}

void param_store_port_read(uint16_t offset, uint8_t *dst, uint16_t len)
{
    if ((uint32_t)offset + (uint32_t)len <= FAKE_PARAM_STORE_CAP) {
        memcpy(dst, &s_flash[offset], len);
    }
}
