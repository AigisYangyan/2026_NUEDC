#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/key/key.h"

#define KEY_BIT(key) ((uint8_t)(1u << (uint8_t)(key)))

void FakeBoardGpio_ResetKeys(void);
void FakeBoardGpio_SetKeyLevels(uint8_t pressed_bits);
void FakeBoardGpio_SetKeyEdges(uint8_t edge_bits);
void FakeBoardGpio_OrKeyEdges(uint8_t edge_bits);
void FakeBoardGpio_ResetKeyObservability(void);
int FakeBoardGpio_GetKeyLevelReadCount(void);

static int s_failures = 0;

#define ASSERT_TRUE(condition)                                                     \
    do {                                                                           \
        if (!(condition)) {                                                        \
            printf("ASSERT_TRUE failed in %s:%d: %s\n",                            \
                   __func__,                                                       \
                   __LINE__,                                                       \
                   #condition);                                                    \
            return false;                                                          \
        }                                                                          \
    } while (0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

#define ASSERT_EQ_INT(expected, actual)                                            \
    do {                                                                           \
        int expected_value = (expected);                                           \
        int actual_value = (actual);                                               \
        if (expected_value != actual_value) {                                      \
            printf("ASSERT_EQ_INT failed in %s:%d: expected %d, got %d\n",         \
                   __func__,                                                       \
                   __LINE__,                                                       \
                   expected_value,                                                 \
                   actual_value);                                                  \
            return false;                                                          \
        }                                                                          \
    } while (0)

static void reset_key_driver(uint8_t initial_levels)
{
    FakeBoardGpio_ResetKeys();
    FakeBoardGpio_SetKeyLevels(initial_levels);
    Key_Init();
    FakeBoardGpio_ResetKeyObservability();
}

static void scan_n(uint8_t levels, int count)
{
    FakeBoardGpio_SetKeyLevels(levels);
    for (int i = 0; i < count; ++i) {
        Key_Scan();
    }
}

static bool test_idle_scan_skips_raw_level_read(void)
{
    Key_Id_e key = KEY_ID_K1;

    reset_key_driver(0u);

    Key_Scan();

    ASSERT_EQ_INT(0, FakeBoardGpio_GetKeyLevelReadCount());
    ASSERT_FALSE(Key_PollPressEvent(&key));
    return true;
}

static bool test_press_requires_four_low_samples_after_edge(void)
{
    reset_key_driver(0u);

    FakeBoardGpio_SetKeyEdges(KEY_BIT(KEY_ID_K1));
    FakeBoardGpio_SetKeyLevels(KEY_BIT(KEY_ID_K1));

    for (int i = 0; i < 3; ++i) {
        Key_Scan();
        ASSERT_FALSE(Key_IsPressed(KEY_ID_K1));
        ASSERT_FALSE(Key_GetPressEvent(KEY_ID_K1));
    }

    Key_Scan();

    ASSERT_TRUE(Key_IsPressed(KEY_ID_K1));
    ASSERT_TRUE(Key_GetPressEvent(KEY_ID_K1));
    ASSERT_FALSE(Key_GetPressEvent(KEY_ID_K1));
    return true;
}

static bool test_third_high_sample_cancels_candidate(void)
{
    reset_key_driver(0u);

    FakeBoardGpio_SetKeyEdges(KEY_BIT(KEY_ID_K1));
    scan_n(KEY_BIT(KEY_ID_K1), 1);
    scan_n(KEY_BIT(KEY_ID_K1), 1);
    scan_n(0u, 1);

    ASSERT_FALSE(Key_IsPressed(KEY_ID_K1));
    ASSERT_FALSE(Key_GetPressEvent(KEY_ID_K1));

    scan_n(KEY_BIT(KEY_ID_K1), 2);

    ASSERT_FALSE(Key_IsPressed(KEY_ID_K1));
    ASSERT_FALSE(Key_GetPressEvent(KEY_ID_K1));
    return true;
}

static bool test_hold_repeated_edges_and_release_rearm(void)
{
    reset_key_driver(0u);

    FakeBoardGpio_SetKeyEdges(KEY_BIT(KEY_ID_K1));
    scan_n(KEY_BIT(KEY_ID_K1), 4);

    ASSERT_TRUE(Key_IsPressed(KEY_ID_K1));
    ASSERT_TRUE(Key_GetPressEvent(KEY_ID_K1));

    FakeBoardGpio_OrKeyEdges(KEY_BIT(KEY_ID_K1));
    scan_n(KEY_BIT(KEY_ID_K1), 3);

    ASSERT_TRUE(Key_IsPressed(KEY_ID_K1));
    ASSERT_FALSE(Key_GetPressEvent(KEY_ID_K1));

    scan_n(0u, 3);
    ASSERT_TRUE(Key_IsPressed(KEY_ID_K1));

    scan_n(0u, 1);
    ASSERT_FALSE(Key_IsPressed(KEY_ID_K1));

    FakeBoardGpio_SetKeyEdges(KEY_BIT(KEY_ID_K1));
    scan_n(KEY_BIT(KEY_ID_K1), 4);

    ASSERT_TRUE(Key_GetPressEvent(KEY_ID_K1));
    return true;
}

static bool test_poll_returns_k1_to_k4_order_and_clears(void)
{
    Key_Id_e key = KEY_ID_K1;
    uint8_t pressed_bits = KEY_BIT(KEY_ID_K1) |
                           KEY_BIT(KEY_ID_K2) |
                           KEY_BIT(KEY_ID_K4);

    reset_key_driver(0u);

    FakeBoardGpio_SetKeyEdges(pressed_bits);
    scan_n(pressed_bits, 4);

    ASSERT_TRUE(Key_PollPressEvent(&key));
    ASSERT_EQ_INT(KEY_ID_K1, key);
    ASSERT_FALSE(Key_GetPressEvent(KEY_ID_K1));

    ASSERT_TRUE(Key_PollPressEvent(&key));
    ASSERT_EQ_INT(KEY_ID_K2, key);

    ASSERT_TRUE(Key_PollPressEvent(&key));
    ASSERT_EQ_INT(KEY_ID_K4, key);

    ASSERT_FALSE(Key_PollPressEvent(&key));
    return true;
}

static bool test_invalid_key_queries_return_false(void)
{
    reset_key_driver(0u);

    ASSERT_FALSE(Key_IsPressed((Key_Id_e)-1));
    ASSERT_FALSE(Key_IsPressed(KEY_ID_COUNT));
    ASSERT_FALSE(Key_GetPressEvent((Key_Id_e)-1));
    ASSERT_FALSE(Key_GetPressEvent(KEY_ID_COUNT));
    ASSERT_FALSE(Key_PollPressEvent(NULL));
    return true;
}

static void run_test(const char *name, bool (*test_fn)(void))
{
    if (test_fn()) {
        printf("PASS: %s\n", name);
        return;
    }

    s_failures++;
}

int main(void)
{
    run_test("test_idle_scan_skips_raw_level_read",
             test_idle_scan_skips_raw_level_read);
    run_test("test_press_requires_four_low_samples_after_edge",
             test_press_requires_four_low_samples_after_edge);
    run_test("test_third_high_sample_cancels_candidate",
             test_third_high_sample_cancels_candidate);
    run_test("test_hold_repeated_edges_and_release_rearm",
             test_hold_repeated_edges_and_release_rearm);
    run_test("test_poll_returns_k1_to_k4_order_and_clears",
             test_poll_returns_k1_to_k4_order_and_clears);
    run_test("test_invalid_key_queries_return_false",
             test_invalid_key_queries_return_false);

    if (s_failures != 0) {
        printf("Key tests failed: %d\n", s_failures);
        return 1;
    }

    printf("\nAll key tests passed.\n");
    return 0;
}
