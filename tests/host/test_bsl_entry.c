#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/bsl_entry/bsl_entry.h"

/* fake_bsl_invoke.c 提供的替身观测面。 */
unsigned int FakeBslInvoke_Count(void);
void FakeBslInvoke_Reset(void);

static int s_test_count = 0;

static void expect_true(bool condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
    s_test_count++;
    printf("PASS: %s\n", name);
}

/* 命中触发字节 0x22 → 恰调一次 InvokeBsl。 */
static void test_trigger_byte_invokes_once(void)
{
    FakeBslInvoke_Reset();
    BslEntry_IsrOnByte(0x22u);
    expect_true(FakeBslInvoke_Count() == 1u,
                "trigger: 0x22 invokes BSL exactly once");
}

/* 非触发字节（含相邻值/边界值）→ 零调用。触发字节是单一的 0x22，不误触发。 */
static void test_non_trigger_bytes_never_invoke(void)
{
    const uint8_t others[] = {0x00u, 0x01u, 0x21u, 0x23u, 0x32u, 0x55u, 0xAAu, 0xFFu};
    unsigned int i;

    FakeBslInvoke_Reset();
    for (i = 0u; i < sizeof(others); i++) {
        BslEntry_IsrOnByte(others[i]);
    }
    expect_true(FakeBslInvoke_Count() == 0u,
                "non-trigger: 0x00/0x21/0x23/0x55/0xAA/0xFF... never invoke");
}

/* 混合流里只有 0x22 触发：喂若干非触发字节 + 一个 0x22 → 恰一次。 */
static void test_only_trigger_in_stream_counts(void)
{
    FakeBslInvoke_Reset();
    BslEntry_IsrOnByte(0x00u);
    BslEntry_IsrOnByte(0x21u);
    BslEntry_IsrOnByte(0x22u); /* 唯一触发 */
    BslEntry_IsrOnByte(0x23u);
    expect_true(FakeBslInvoke_Count() == 1u,
                "stream: exactly one 0x22 among noise invokes once");
}

int main(void)
{
    test_trigger_byte_invokes_once();
    test_non_trigger_bytes_never_invoke();
    test_only_trigger_in_stream_counts();

    printf("All bsl_entry tests passed (%d tests).\n", s_test_count);
    return 0;
}
