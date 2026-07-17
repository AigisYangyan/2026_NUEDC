/**
 * @file    test_encoder.c
 * @brief   Host unit tests for the new Encoder Driver interface.
 *
 * These tests define the acceptance criteria for the new Encoder Driver
 * interface. They are designed to be compiled and run on a host without
 * MSPM0 HAL dependencies. A minimal implementation is present in encoder.c;
 * therefore the tests are expected to pass once a host C compiler is
 * available.
 */
#include "driver/encoder/encoder.h"
#include "driver/board_gpio/board_gpio.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

/* Provided by fake_board_gpio.c */
extern void FakeBoardGpio_SetRaw(int32_t left, int32_t right);

/* Production sampling owner from task_groups.c. */
extern void Task_EncoderSpeedSample(void);

static uint32_t s_fake_clock_ms;

uint32_t Clock_NowMs(void)
{
    return s_fake_clock_ms;
}

/*
 * task_groups.c is linked to exercise the production sampling owner. Its
 * unrelated exported task table keeps peer task sections reachable on MinGW,
 * so provide inert link-only symbols for paths this test never invokes.
 */
unsigned char g_tMotors[512];
void OLED_IsReady(void) {}
void OLED_Process(void) {}
void Key_Scan(void) {}
void Menu_HandleKey(void) {}
void Key_PollPressEvent(void) {}
void Menu_IsDirty(void) {}
void Menu_RenderIfDirty(void) {}
void Motor_SetPwm(void) {}
void StepmotorBus_Service5ms(void) {}
void VisionBus_Service5ms(void) {}
void VisionHdl_Run10ms(void) {}
void VisionHdl_Control5ms(void) {}
void VisionHdl_Telemetry10ms(void) {}
void VisionCoord_GetLatest(void) {}
void vofa_run(void) {}
void SpeedLoop_Sample10ms(void) {}
void SpeedLoop_Control10ms(void) {}
void SpeedLoop_Telemetry20ms(void) {}
void UartTest_Telemetry10ms(void) {}
void GrayTest_SampleAndTelemetry10ms(void) {}
void UartStress_Tick5ms(void) {}
void DebugSmooth_Control5ms(void) {}
void DebugSmooth_Telemetry10ms(void) {}
void DebugVisionData_Telemetry10ms(void) {}
void Task1_Sample10ms(void) {}
void Task1_Control10ms(void) {}
void Task1_Telemetry20ms(void) {}

/* Test helpers */
#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_FLOAT_NEAR(a, b, eps) do { \
    if (fabsf((a) - (b)) > (eps)) { \
        printf("FAIL: |%f - %f| > %f at %s:%d\n", (a), (b), (eps), __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static int test_init_baseline_zero_delta(void)
{
    Encoder_Snapshot snap;

    FakeBoardGpio_SetRaw(1000, 2000);
    Encoder_Init();
    Encoder_GetSnapshot(&snap);

    TEST_ASSERT(snap.total_pulses[ENCODER_LEFT] == -1000);  /* direction_sign = -1 */
    TEST_ASSERT(snap.total_pulses[ENCODER_RIGHT] == 2000);  /* direction_sign = +1 */
    TEST_ASSERT(snap.delta_pulses[ENCODER_LEFT] == 0);
    TEST_ASSERT(snap.delta_pulses[ENCODER_RIGHT] == 0);
    TEST_ASSERT_FLOAT_NEAR(snap.speed_mps[ENCODER_LEFT], 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(snap.speed_mps[ENCODER_RIGHT], 0.0f, 1e-6f);

    printf("PASS: test_init_baseline_zero_delta\n");
    return 0;
}

static int test_forward_motion(void)
{
    Encoder_Snapshot snap;

    FakeBoardGpio_SetRaw(1000, 2000);
    Encoder_Init();

    FakeBoardGpio_SetRaw(1100, 2100);
    TEST_ASSERT(Encoder_Update(10u) == true);
    Encoder_GetSnapshot(&snap);

    TEST_ASSERT(snap.delta_pulses[ENCODER_LEFT] == -100);  /* raw +100, sign -1 */
    TEST_ASSERT(snap.delta_pulses[ENCODER_RIGHT] == 100);
    TEST_ASSERT(snap.total_pulses[ENCODER_LEFT] == -1100);
    TEST_ASSERT(snap.total_pulses[ENCODER_RIGHT] == 2100);

    printf("PASS: test_forward_motion\n");
    return 0;
}

static int test_reverse_motion(void)
{
    Encoder_Snapshot snap;

    FakeBoardGpio_SetRaw(1000, 2000);
    Encoder_Init();

    FakeBoardGpio_SetRaw(900, 1900);
    TEST_ASSERT(Encoder_Update(10u) == true);
    Encoder_GetSnapshot(&snap);

    TEST_ASSERT(snap.delta_pulses[ENCODER_LEFT] == 100);   /* raw -100, sign -1 */
    TEST_ASSERT(snap.delta_pulses[ENCODER_RIGHT] == -100);

    printf("PASS: test_reverse_motion\n");
    return 0;
}

static int test_zero_elapsed_rejected(void)
{
    TEST_ASSERT(Encoder_Update(0u) == false);
    printf("PASS: test_zero_elapsed_rejected\n");
    return 0;
}

static int test_speed_scales_with_elapsed(void)
{
    Encoder_Snapshot snap_10ms;
    Encoder_Snapshot snap_20ms;
    const int32_t delta = 100;

    /* 100 pulses over 10 ms */
    FakeBoardGpio_SetRaw(1000, 1000);
    Encoder_Init();
    FakeBoardGpio_SetRaw(1000 + delta, 1000 + delta);
    Encoder_Update(10u);
    Encoder_GetSnapshot(&snap_10ms);

    /* Same 100 pulses over 20 ms -> half the speed */
    FakeBoardGpio_SetRaw(1000, 1000);
    Encoder_Init();
    FakeBoardGpio_SetRaw(1000 + delta, 1000 + delta);
    Encoder_Update(20u);
    Encoder_GetSnapshot(&snap_20ms);

    TEST_ASSERT_FLOAT_NEAR(snap_20ms.speed_mps[ENCODER_RIGHT],
                           snap_10ms.speed_mps[ENCODER_RIGHT] / 2.0f,
                           1e-4f);

    printf("PASS: test_speed_scales_with_elapsed\n");
    return 0;
}

static int test_int32_wraparound_forward(void)
{
    Encoder_Snapshot snap;

    /* Forward wrap: INT32_MAX -> INT32_MIN is +1 in unsigned arithmetic. */
    FakeBoardGpio_SetRaw(INT32_MAX, INT32_MAX);
    Encoder_Init();

    FakeBoardGpio_SetRaw(INT32_MIN, INT32_MIN);
    TEST_ASSERT(Encoder_Update(10u) == true);
    Encoder_GetSnapshot(&snap);

    /* Left wheel (sign=-1): raw_delta=+1, delta=-1 */
    TEST_ASSERT(snap.delta_pulses[ENCODER_LEFT] == -1);
    /* Right wheel (sign=+1): raw_delta=+1, delta=+1 */
    TEST_ASSERT(snap.delta_pulses[ENCODER_RIGHT] == 1);

    /* Total pulses: verified not UB via int64_t intermediate cast. */
    TEST_ASSERT(snap.total_pulses[ENCODER_RIGHT] == INT32_MIN);
    /* Left total: raw INT32_MIN * -1 = INT32_MIN (2's complement modulo). */
    TEST_ASSERT(snap.total_pulses[ENCODER_LEFT] == INT32_MIN);

    printf("PASS: test_int32_wraparound_forward\n");
    return 0;
}

static int test_int32_wraparound_reverse(void)
{
    Encoder_Snapshot snap;

    /* Reverse wrap: INT32_MIN -> INT32_MAX is -1 in unsigned arithmetic. */
    FakeBoardGpio_SetRaw(INT32_MIN, INT32_MIN);
    Encoder_Init();

    FakeBoardGpio_SetRaw(INT32_MAX, INT32_MAX);
    TEST_ASSERT(Encoder_Update(10u) == true);
    Encoder_GetSnapshot(&snap);

    /* Left wheel (sign=-1): raw_delta=-1, delta=+1 */
    TEST_ASSERT(snap.delta_pulses[ENCODER_LEFT] == 1);
    /* Right wheel (sign=+1): raw_delta=-1, delta=-1 */
    TEST_ASSERT(snap.delta_pulses[ENCODER_RIGHT] == -1);

    printf("PASS: test_int32_wraparound_reverse\n");
    return 0;
}

static int test_known_speed_absolute(void)
{
    Encoder_Snapshot snap;

    /* 100 pulses over 10 ms: speed ≈ 1.3815 m/s (baseline doc §6). */
    FakeBoardGpio_SetRaw(1000, 1000);
    Encoder_Init();
    FakeBoardGpio_SetRaw(1100, 1100);
    Encoder_Update(10u);
    Encoder_GetSnapshot(&snap);

    TEST_ASSERT_FLOAT_NEAR(snap.speed_mps[ENCODER_RIGHT], 1.3815f, 0.01f);

    /* 100 pulses over 20 ms: speed ≈ 0.6907 m/s (half the rate). */
    FakeBoardGpio_SetRaw(1000, 1000);
    Encoder_Init();
    FakeBoardGpio_SetRaw(1100, 1100);
    Encoder_Update(20u);
    Encoder_GetSnapshot(&snap);

    TEST_ASSERT_FLOAT_NEAR(snap.speed_mps[ENCODER_RIGHT], 0.6907f, 0.005f);

    printf("PASS: test_known_speed_absolute\n");
    return 0;
}

static int test_multi_ms_elapsed(void)
{
    Encoder_Snapshot snap;

    /* Large elapsed value (1 second) must not overflow or misbehave. */
    FakeBoardGpio_SetRaw(0, 0);
    Encoder_Init();
    FakeBoardGpio_SetRaw(100, 100);
    TEST_ASSERT(Encoder_Update(1000u) == true);
    Encoder_GetSnapshot(&snap);

    /* 100 pulses / 1s: idle time must stay in the divisor, avoiding a re-entry spike. */
    TEST_ASSERT(snap.delta_pulses[ENCODER_RIGHT] == 100);
    TEST_ASSERT_FLOAT_NEAR(snap.speed_mps[ENCODER_RIGHT], 0.013815f, 0.0001f);

    printf("PASS: test_multi_ms_elapsed\n");
    return 0;
}

static int test_sampling_owner_preserves_idle_elapsed(void)
{
    Encoder_Snapshot snap;

    /* Encoder baseline and Clock epoch both start at zero during SysInit. */
    s_fake_clock_ms = 0u;
    FakeBoardGpio_SetRaw(0, 0);
    Encoder_Init();

    s_fake_clock_ms = 1000u;
    FakeBoardGpio_SetRaw(100, 100);
    Task_EncoderSpeedSample();
    Encoder_GetSnapshot(&snap);
    TEST_ASSERT_FLOAT_NEAR(snap.speed_mps[ENCODER_RIGHT], 0.013815f, 0.0001f);

    /* One active 10 ms sample. */
    s_fake_clock_ms = 1010u;
    FakeBoardGpio_SetRaw(110, 110);
    Task_EncoderSpeedSample();
    Encoder_GetSnapshot(&snap);
    TEST_ASSERT_FLOAT_NEAR(snap.speed_mps[ENCODER_RIGHT], 0.13815f, 0.001f);

    /* After a 1 s inactive interval, the divisor must also span the full 1 s. */
    s_fake_clock_ms = 2010u;
    FakeBoardGpio_SetRaw(210, 210);
    Task_EncoderSpeedSample();
    Encoder_GetSnapshot(&snap);
    TEST_ASSERT_FLOAT_NEAR(snap.speed_mps[ENCODER_RIGHT], 0.013815f, 0.0001f);

    printf("PASS: test_sampling_owner_preserves_idle_elapsed\n");
    return 0;
}

static int test_dual_wheel_snapshot_consistency(void)
{
    Encoder_Snapshot snap;

    /* Different left/right raw values — verify both are independently
     * direction-corrected within one snapshot call. */
    FakeBoardGpio_SetRaw(-500, 3000);
    Encoder_Init();

    FakeBoardGpio_SetRaw(-400, 3100);
    Encoder_Update(10u);
    Encoder_GetSnapshot(&snap);

    /* Left (sign=-1): raw -400, corrected +400; delta from raw -500 → -400 = +100 raw, * -1 = -100. */
    TEST_ASSERT(snap.delta_pulses[ENCODER_LEFT] == -100);
    TEST_ASSERT(snap.total_pulses[ENCODER_LEFT] == 400);

    /* Right (sign=+1): raw 3100, delta from 3000 → 3100 = +100. */
    TEST_ASSERT(snap.delta_pulses[ENCODER_RIGHT] == 100);
    TEST_ASSERT(snap.total_pulses[ENCODER_RIGHT] == 3100);

    printf("PASS: test_dual_wheel_snapshot_consistency\n");
    return 0;
}

static int test_consecutive_updates(void)
{
    Encoder_Snapshot snap;
    int i;

    FakeBoardGpio_SetRaw(0, 0);
    Encoder_Init();

    /* 5 consecutive updates, each +10 pulses, each 10 ms. */
    for (i = 1; i <= 5; i++) {
        FakeBoardGpio_SetRaw(i * 10, i * 10);
        TEST_ASSERT(Encoder_Update(10u) == true);
    }
    Encoder_GetSnapshot(&snap);

    /* Each update delta = +10, total after 5 updates = +50. */
    TEST_ASSERT(snap.delta_pulses[ENCODER_RIGHT] == 10);
    TEST_ASSERT(snap.total_pulses[ENCODER_RIGHT] == 50);

    printf("PASS: test_consecutive_updates\n");
    return 0;
}

static int test_getsnapshot_null_safe(void)
{
    /* Should not crash. */
    Encoder_GetSnapshot(NULL);
    printf("PASS: test_getsnapshot_null_safe\n");
    return 0;
}

static int test_snapshot_is_value_copy(void)
{
    Encoder_Snapshot snap;

    FakeBoardGpio_SetRaw(0, 0);
    Encoder_Init();
    Encoder_GetSnapshot(&snap);

    /* Modifying the returned snapshot must not affect internal state. */
    snap.total_pulses[ENCODER_LEFT] = 9999;
    Encoder_GetSnapshot(&snap);
    TEST_ASSERT(snap.total_pulses[ENCODER_LEFT] == 0);

    printf("PASS: test_snapshot_is_value_copy\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_init_baseline_zero_delta();
    failures += test_forward_motion();
    failures += test_reverse_motion();
    failures += test_zero_elapsed_rejected();
    failures += test_speed_scales_with_elapsed();
    failures += test_int32_wraparound_forward();
    failures += test_int32_wraparound_reverse();
    failures += test_known_speed_absolute();
    failures += test_multi_ms_elapsed();
    failures += test_sampling_owner_preserves_idle_elapsed();
    failures += test_dual_wheel_snapshot_consistency();
    failures += test_consecutive_updates();
    failures += test_getsnapshot_null_safe();
    failures += test_snapshot_is_value_copy();

    if (failures == 0) {
        printf("\nAll encoder tests passed.\n");
        return 0;
    }

    printf("\n%d encoder test(s) failed.\n", failures);
    return 1;
}
