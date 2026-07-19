/**
 * @file    test_param_store.c
 * @brief   参数 blob 存储 Driver 主机测试（PT1，契约 §25.2）。
 *
 * 链接组成：真实 param_store.c + fake_param_store_port.c（RAM 顶替片内 flash）+ 测试。
 * 验框定/CRC 逻辑：空扇区拒读、往返一致、擦前写、超容拒写、program/erase 失败注入、CRC 篡改拒读。
 * 真实 flash 擦/写不在此验（硬件边界，用户上板自理）。
 */
#include "driver/param_store/param_store.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void FakeParamStorePort_Reset(void);
extern void FakeParamStorePort_SetEraseFail(bool fail);
extern void FakeParamStorePort_SetProgramFail(bool fail);
extern void FakeParamStorePort_Poke(uint16_t offset, uint8_t value);

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* 记录框定：payload 落在扇区偏移 8 起（header=8B）。用于 Poke 篡改定位。 */
#define PS_HEADER_SIZE 8u

static const uint8_t k_payload_a[13] = {
    0x01u, 0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u,
    0x70u, 0x80u, 0x90u, 0xA0u, 0xB0u, 0xC0u,
};
static const uint8_t k_payload_b[13] = {
    0x01u, 0xDEu, 0xADu, 0xBEu, 0xEFu, 0x11u, 0x22u,
    0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u,
};

/* 空扇区（全 0xFF）→ magic 不符 → Read false，且 buf 不被触碰。 */
static int test_blank_reads_false(void)
{
    uint8_t buf[13];

    FakeParamStorePort_Reset();
    memset(buf, 0x5Au, sizeof buf);
    TEST_ASSERT_TRUE(ParamStore_Read(buf, 13u) == false);
    TEST_ASSERT_TRUE(buf[0] == 0x5Au);           /* buf 不动 */
    printf("PASS: test_blank_reads_false\n");
    return 0;
}

/* Save → Read 往返一致。 */
static int test_save_read_roundtrip(void)
{
    uint8_t buf[13];

    FakeParamStorePort_Reset();
    TEST_ASSERT_TRUE(ParamStore_Save(k_payload_a, 13u) == true);
    memset(buf, 0, sizeof buf);
    TEST_ASSERT_TRUE(ParamStore_Read(buf, 13u) == true);
    TEST_ASSERT_TRUE(memcmp(buf, k_payload_a, 13u) == 0);
    printf("PASS: test_save_read_roundtrip\n");
    return 0;
}

/* 二次 Save 覆盖：擦前写生效，Read 得新值（非旧 A 残留）。 */
static int test_overwrite_erases_old(void)
{
    uint8_t buf[13];

    FakeParamStorePort_Reset();
    TEST_ASSERT_TRUE(ParamStore_Save(k_payload_a, 13u) == true);
    TEST_ASSERT_TRUE(ParamStore_Save(k_payload_b, 13u) == true);
    TEST_ASSERT_TRUE(ParamStore_Read(buf, 13u) == true);
    TEST_ASSERT_TRUE(memcmp(buf, k_payload_b, 13u) == 0);
    printf("PASS: test_overwrite_erases_old\n");
    return 0;
}

/* program 失败注入 → Save false，且随后无有效记录（擦后写空 → magic 0xFFFF → Read false）。 */
static int test_program_fail_rejects(void)
{
    uint8_t buf[13];

    FakeParamStorePort_Reset();
    FakeParamStorePort_SetProgramFail(true);
    TEST_ASSERT_TRUE(ParamStore_Save(k_payload_a, 13u) == false);
    FakeParamStorePort_SetProgramFail(false);
    TEST_ASSERT_TRUE(ParamStore_Read(buf, 13u) == false);
    printf("PASS: test_program_fail_rejects\n");
    return 0;
}

/* erase 失败注入 → Save false。 */
static int test_erase_fail_rejects(void)
{
    FakeParamStorePort_Reset();
    FakeParamStorePort_SetEraseFail(true);
    TEST_ASSERT_TRUE(ParamStore_Save(k_payload_a, 13u) == false);
    printf("PASS: test_erase_fail_rejects\n");
    return 0;
}

/* CRC 篡改：Save 后翻转一个 payload 字节 → Read false（完整性守卫）。 */
static int test_crc_tamper_rejects(void)
{
    uint8_t buf[13];

    FakeParamStorePort_Reset();
    TEST_ASSERT_TRUE(ParamStore_Save(k_payload_a, 13u) == true);
    /* payload 首字节在扇区偏移 8；翻位使 CRC 失配。0x01 -> 0x00 是 1→0，NOR 合法直写。 */
    FakeParamStorePort_Poke(PS_HEADER_SIZE, 0x00u);
    TEST_ASSERT_TRUE(ParamStore_Read(buf, 13u) == false);
    printf("PASS: test_crc_tamper_rejects\n");
    return 0;
}

/* 超容量：len > PARAM_STORE_MAX_PAYLOAD → Save false（不擦不写）。 */
static int test_oversize_rejects(void)
{
    uint8_t big[PARAM_STORE_MAX_PAYLOAD + 1u];

    FakeParamStorePort_Reset();
    memset(big, 0xA5, sizeof big);
    TEST_ASSERT_TRUE(ParamStore_Save(big, (uint16_t)(PARAM_STORE_MAX_PAYLOAD + 1u)) == false);
    printf("PASS: test_oversize_rejects\n");
    return 0;
}

/* 长度不符：以 13B 存、以 12B 读 → Read false（长度校验）。 */
static int test_length_mismatch_rejects(void)
{
    uint8_t buf[13];

    FakeParamStorePort_Reset();
    TEST_ASSERT_TRUE(ParamStore_Save(k_payload_a, 13u) == true);
    TEST_ASSERT_TRUE(ParamStore_Read(buf, 12u) == false);
    printf("PASS: test_length_mismatch_rejects\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_blank_reads_false();
    failures += test_save_read_roundtrip();
    failures += test_overwrite_erases_old();
    failures += test_program_fail_rejects();
    failures += test_erase_fail_rejects();
    failures += test_crc_tamper_rejects();
    failures += test_oversize_rejects();
    failures += test_length_mismatch_rejects();

    if (failures != 0) {
        printf("param_store driver tests failed: %d\n", failures);
        return 1;
    }
    printf("\nAll param_store driver tests passed.\n");
    return 0;
}
