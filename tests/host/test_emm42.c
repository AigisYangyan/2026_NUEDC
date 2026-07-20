/**
 * @file    test_emm42.c
 * @brief   Host tests for emm42 快速位置/多机/清零 纯组包 (T-GQ1, 契约 plan_gimbal_qpos_codrive).
 *
 * 独立校验：期望字节按 X42S 手册布局硬编码（§5.3.13 快速位置 F1/FC、§5.3.1 多机 0xAA、
 * §5.2.3 当前位置清零 0x0A），不拿被测函数验自己。速度 ×10 限幅（≤100RPM）仍唯一在 emm42。
 */
#include "driver/step_motor/emm42.h"

#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static int expect_frame(const uint8_t *got, uint8_t got_len,
                        const uint8_t *exp, uint8_t exp_len)
{
    if (got_len != exp_len) {
        printf("FAIL: len %u != %u\n", (unsigned)got_len, (unsigned)exp_len);
        return 1;
    }
    if (memcmp(got, exp, exp_len) != 0) {
        printf("FAIL: bytes mismatch\n");
        return 1;
    }
    return 0;
}

/* ---- 0xF1 快速位置预设 -------------------------------------------------------- */

static int test_qpos_preset_bytes(void)
{
    uint8_t out[16];
    uint8_t out_len = 0u;
    /* Y(id=1), speed=30→×10=300=0x012C, accel=0, mode=ABS(1), sync=0. */
    const uint8_t exp[] = {0x01u, 0xF1u, 0x01u, 0x2Cu, 0x00u, 0x01u, 0x00u, 0x6Bu};

    TEST_ASSERT(Emm42_BuildQPosPresetFrame((uint8_t)EMM42_AXIS_Y, 30u, 0u,
                                           EMM42_POSITION_MODE_ABSOLUTE, out, &out_len) == true);
    return expect_frame(out, out_len, exp, (uint8_t)sizeof(exp));
}

static int test_qpos_preset_speed_clamped(void)
{
    uint8_t out[16];
    uint8_t out_len = 0u;
    /* speed=250 → 唯一限幅所有者夹到 100 → ×10 = 1000 = 0x03E8；mode=ABS(1) 在第 6 字节. */
    const uint8_t exp[] = {0x02u, 0xF1u, 0x03u, 0xE8u, 0x00u, 0x01u, 0x00u, 0x6Bu};

    TEST_ASSERT(Emm42_BuildQPosPresetFrame((uint8_t)EMM42_AXIS_X, 250u, 0u,
                                           EMM42_POSITION_MODE_ABSOLUTE, out, &out_len) == true);
    return expect_frame(out, out_len, exp, (uint8_t)sizeof(exp));
}

/* ---- 0xFC 快速位置（有符号 int32 大端） -------------------------------------- */

static int test_qpos_positive(void)
{
    uint8_t out[16];
    uint8_t out_len = 0u;
    /* X(id=2), pulses=2048=0x00000800. */
    const uint8_t exp[] = {0x02u, 0xFCu, 0x00u, 0x00u, 0x08u, 0x00u, 0x6Bu};

    TEST_ASSERT(Emm42_BuildQPosFrame((uint8_t)EMM42_AXIS_X, 2048, out, &out_len) == true);
    return expect_frame(out, out_len, exp, (uint8_t)sizeof(exp));
}

static int test_qpos_negative_twos_complement(void)
{
    uint8_t out[16];
    uint8_t out_len = 0u;
    /* pulses=-2048 → 0xFFFFF800 大端；符号由 int32 承载，无 dir 字节。 */
    const uint8_t exp[] = {0x01u, 0xFCu, 0xFFu, 0xFFu, 0xF8u, 0x00u, 0x6Bu};

    TEST_ASSERT(Emm42_BuildQPosFrame((uint8_t)EMM42_AXIS_Y, -2048, out, &out_len) == true);
    return expect_frame(out, out_len, exp, (uint8_t)sizeof(exp));
}

/* ---- 0xAA 多电机命令封装 ------------------------------------------------------ */

static int test_multicmd_wraps_two_fc(void)
{
    uint8_t out[32];
    uint8_t out_len = 0u;
    /* 子命令串 = FC_Y(y=100=0x64) ∥ FC_X(x=-50=0xFFFFFFCE)，各 7B，共 14B。
     * len = 14+5 = 19 = 0x0013；封装 = [00 AA 00 13 <subs> 6B]。 */
    const uint8_t subs[] = {
        0x01u, 0xFCu, 0x00u, 0x00u, 0x00u, 0x64u, 0x6Bu,
        0x02u, 0xFCu, 0xFFu, 0xFFu, 0xFFu, 0xCEu, 0x6Bu,
    };
    const uint8_t exp[] = {
        0x00u, 0xAAu, 0x00u, 0x13u,
        0x01u, 0xFCu, 0x00u, 0x00u, 0x00u, 0x64u, 0x6Bu,
        0x02u, 0xFCu, 0xFFu, 0xFFu, 0xFFu, 0xCEu, 0x6Bu,
        0x6Bu,
    };

    TEST_ASSERT(Emm42_BuildMultiCmdFrame(subs, (uint8_t)sizeof(subs), out, &out_len) == true);
    return expect_frame(out, out_len, exp, (uint8_t)sizeof(exp));
}

static int test_multicmd_rejects_oversize(void)
{
    uint8_t out[64];
    uint8_t out_len = 0xAAu;
    uint8_t big[64];
    memset(big, 0x00u, sizeof(big));

    /* 子命令串超过封装上限 → 拒绝（保 out 缓冲不溢出），out_len 不被写。 */
    TEST_ASSERT(Emm42_BuildMultiCmdFrame(big, (uint8_t)sizeof(big), out, &out_len) == false);
    return 0;
}

/* ---- 0x0A 当前位置清零（绝对坐标零点） --------------------------------------- */

static int test_clear_position_bytes(void)
{
    uint8_t out[16];
    uint8_t out_len = 0u;
    const uint8_t exp[] = {0x02u, 0x0Au, 0x6Du, 0x6Bu};

    TEST_ASSERT(Emm42_BuildClearPositionFrame((uint8_t)EMM42_AXIS_X, out, &out_len) == true);
    return expect_frame(out, out_len, exp, (uint8_t)sizeof(exp));
}

/* ---- null 防御（同既有 builder 口径） ---------------------------------------- */

static int test_null_out_rejected(void)
{
    uint8_t len = 0u;
    TEST_ASSERT(Emm42_BuildQPosFrame((uint8_t)EMM42_AXIS_X, 1, NULL, &len) == false);
    TEST_ASSERT(Emm42_BuildQPosPresetFrame((uint8_t)EMM42_AXIS_X, 30u, 0u,
                                           EMM42_POSITION_MODE_ABSOLUTE, NULL, &len) == false);
    TEST_ASSERT(Emm42_BuildClearPositionFrame((uint8_t)EMM42_AXIS_X, NULL, &len) == false);
    return 0;
}

#define RUN(fn) do { if ((fn)() != 0) { fails++; } else { printf("PASS: %s\n", #fn); } } while (0)

int main(void)
{
    int fails = 0;

    RUN(test_qpos_preset_bytes);
    RUN(test_qpos_preset_speed_clamped);
    RUN(test_qpos_positive);
    RUN(test_qpos_negative_twos_complement);
    RUN(test_multicmd_wraps_two_fc);
    RUN(test_multicmd_rejects_oversize);
    RUN(test_clear_position_bytes);
    RUN(test_null_out_rejected);

    if (fails == 0) {
        printf("All emm42 tests passed.\n");
    }
    return (fails == 0) ? 0 : 1;
}
