/**
 * @file    test_scheduler.c
 * @brief   scheduler 运行条目调度器主机测试（SCH01 契约 §11.4 E03）。
 *
 * 链接组成：真实 scheduler.c，仅此——纯逻辑模块，零 fake（含 fake_clock：
 * 时间按 Q1 定案以 Scheduler_Run(now_ms) 参数注入，测试直接喂值）。
 * 条目钩子为本文件内的记录桩，按调用序与 now_ms 透传值断言。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app/scheduler/scheduler.h"

static int s_failures = 0;

#define ASSERT_TRUE(condition)                                                 \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("ASSERT_TRUE failed in %s:%d: %s\n",                        \
                   __func__, __LINE__, #condition);                            \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

#define ASSERT_EQ_INT(expected, actual)                                        \
    do {                                                                       \
        int expected_value = (expected);                                       \
        int actual_value = (actual);                                           \
        if (expected_value != actual_value) {                                  \
            printf("ASSERT_EQ_INT failed in %s:%d: expected %d, got %d\n",     \
                   __func__, __LINE__, expected_value, actual_value);          \
            return false;                                                      \
        }                                                                      \
    } while (0)

/* ---- 调用记录桩 ------------------------------------------------------- */

#define LOG_CAPACITY 16

typedef struct {
    const char *tag;
    uint32_t now_ms; /* enter/exit 无时间参数，记 0 */
} CallRec_T;

static CallRec_T s_call_log[LOG_CAPACITY];
static int s_call_count = 0;

static void log_call(const char *tag, uint32_t now_ms)
{
    if (s_call_count < LOG_CAPACITY) {
        s_call_log[s_call_count].tag = tag;
        s_call_log[s_call_count].now_ms = now_ms;
    }
    s_call_count++;
}

static void log_reset(void)
{
    s_call_count = 0;
    memset(s_call_log, 0, sizeof(s_call_log));
}

#define ASSERT_LOG(index, expected_tag, expected_now)                          \
    do {                                                                       \
        ASSERT_TRUE((index) < s_call_count);                                   \
        ASSERT_TRUE(strcmp(s_call_log[(index)].tag, (expected_tag)) == 0);     \
        ASSERT_EQ_INT((int)(expected_now), (int)s_call_log[(index)].now_ms);   \
    } while (0)

static void a_enter(void) { log_call("A_enter", 0u); }
static void a_step(uint32_t now_ms) { log_call("A_step", now_ms); }
static void a_exit(void) { log_call("A_exit", 0u); }
static void b_enter(void) { log_call("B_enter", 0u); }
static void b_step(uint32_t now_ms) { log_call("B_step", now_ms); }
static void b_exit(void) { log_call("B_exit", 0u); }
static void bg_step(uint32_t now_ms) { log_call("BG", now_ms); }

/* 自终止条目：step 内 Leave（赛题小题跑完自行收场的形态）。 */
static void self_term_step(uint32_t now_ms)
{
    log_call("S_step", now_ms);
    Scheduler_LeaveEntry();
}

static void self_term_exit(void) { log_call("S_exit", 0u); }

/* 背景钩子内 Enter（菜单确认键进入条目的形态），单次触发开关。 */
static bool s_bg_do_enter = false;

static void bg_entering_step(uint32_t now_ms)
{
    log_call("BGE", now_ms);
    if (s_bg_do_enter) {
        s_bg_do_enter = false;
        (void)Scheduler_EnterEntry(0u);
    }
}

static const Scheduler_Entry_T s_entries_ab_null[] = {
    { "EntryA", a_enter, a_step, a_exit },
    { "EntryB", b_enter, b_step, b_exit },
    { "EntryC", NULL, NULL, NULL },
};

static const Scheduler_Entry_T s_entries_self_term[] = {
    { "EntryS", NULL, self_term_step, self_term_exit },
};

/* on_exit 内嵌套 Enter（Task 收场后链式进入下一条目的形态，契约修订 1）。 */
static void x_exit_chains_enter(void)
{
    log_call("X_exit", 0u);
    (void)Scheduler_EnterEntry(1u); /* 嵌套进入 EntryB */
}

static const Scheduler_Entry_T s_entries_chain[] = {
    { "EntryX", NULL, NULL, x_exit_chains_enter },
    { "EntryB", b_enter, b_step, b_exit },
};

#define ENTRY_AB_COUNT \
    ((uint8_t)(sizeof(s_entries_ab_null) / sizeof(s_entries_ab_null[0])))

static void setup_ab(void)
{
    Scheduler_Init(s_entries_ab_null, ENTRY_AB_COUNT, bg_step);
    log_reset();
}

/* ---- 用例 ------------------------------------------------------------- */

/* 必须首个执行：覆盖 Init 前静态初值状态（count 0 / active NONE / 全安全）。 */
static bool test_before_init_all_safe(void)
{
    ASSERT_EQ_INT(0, (int)Scheduler_GetEntryCount());
    ASSERT_TRUE(Scheduler_GetEntryName(0u) == NULL);
    ASSERT_EQ_INT(SCHEDULER_ENTRY_NONE, (int)Scheduler_GetActiveEntry());
    ASSERT_FALSE(Scheduler_EnterEntry(0u));
    Scheduler_LeaveEntry();
    Scheduler_Run(123u);
    ASSERT_EQ_INT(0, s_call_count);
    return true;
}

static bool test_empty_table_init_safe(void)
{
    Scheduler_Init(NULL, 0u, bg_step);
    log_reset();
    ASSERT_EQ_INT(0, (int)Scheduler_GetEntryCount());
    ASSERT_FALSE(Scheduler_EnterEntry(0u));
    Scheduler_Run(7u);
    ASSERT_EQ_INT(1, s_call_count); /* 仅背景钩子 */
    ASSERT_LOG(0, "BG", 7u);
    return true;
}

static bool test_idle_run_only_background_passthrough(void)
{
    setup_ab();
    Scheduler_Run(123u);
    Scheduler_Run(456u);
    ASSERT_EQ_INT(2, s_call_count);
    ASSERT_LOG(0, "BG", 123u);
    ASSERT_LOG(1, "BG", 456u);
    ASSERT_EQ_INT(SCHEDULER_ENTRY_NONE, (int)Scheduler_GetActiveEntry());
    return true;
}

static bool test_enter_then_run_background_before_step(void)
{
    setup_ab();
    ASSERT_TRUE(Scheduler_EnterEntry(0u));
    ASSERT_EQ_INT(0, (int)Scheduler_GetActiveEntry());
    ASSERT_EQ_INT(1, s_call_count); /* 无旧条目：仅 A_enter */
    ASSERT_LOG(0, "A_enter", 0u);
    log_reset();
    Scheduler_Run(5u);
    ASSERT_EQ_INT(2, s_call_count);
    ASSERT_LOG(0, "BG", 5u);      /* 背景先行 */
    ASSERT_LOG(1, "A_step", 5u);  /* now_ms 原值透传 */
    return true;
}

static bool test_switch_exit_before_enter_exactly_once(void)
{
    setup_ab();
    (void)Scheduler_EnterEntry(0u);
    log_reset();
    ASSERT_TRUE(Scheduler_EnterEntry(1u));
    ASSERT_EQ_INT(2, s_call_count);
    ASSERT_LOG(0, "A_exit", 0u);
    ASSERT_LOG(1, "B_enter", 0u);
    ASSERT_EQ_INT(1, (int)Scheduler_GetActiveEntry());
    return true;
}

static bool test_reenter_same_index_restarts(void)
{
    setup_ab();
    (void)Scheduler_EnterEntry(1u);
    log_reset();
    ASSERT_TRUE(Scheduler_EnterEntry(1u));
    ASSERT_EQ_INT(2, s_call_count);
    ASSERT_LOG(0, "B_exit", 0u);
    ASSERT_LOG(1, "B_enter", 0u);
    ASSERT_EQ_INT(1, (int)Scheduler_GetActiveEntry());
    return true;
}

static bool test_enter_out_of_bounds_false_no_side_effect(void)
{
    setup_ab();
    (void)Scheduler_EnterEntry(1u);
    log_reset();
    ASSERT_FALSE(Scheduler_EnterEntry(ENTRY_AB_COUNT));
    ASSERT_EQ_INT(0, s_call_count); /* 零钩子调用 */
    ASSERT_EQ_INT(1, (int)Scheduler_GetActiveEntry());
    return true;
}

static bool test_leave_then_idle_then_repeat_leave_noop(void)
{
    setup_ab();
    (void)Scheduler_EnterEntry(1u);
    log_reset();
    Scheduler_LeaveEntry();
    ASSERT_EQ_INT(1, s_call_count);
    ASSERT_LOG(0, "B_exit", 0u);
    ASSERT_EQ_INT(SCHEDULER_ENTRY_NONE, (int)Scheduler_GetActiveEntry());
    log_reset();
    Scheduler_Run(9u);
    ASSERT_EQ_INT(1, s_call_count); /* 仅背景 */
    ASSERT_LOG(0, "BG", 9u);
    log_reset();
    Scheduler_LeaveEntry(); /* 重复 Leave：no-op */
    ASSERT_EQ_INT(0, s_call_count);
    return true;
}

static bool test_step_self_terminate_immediate_exit(void)
{
    Scheduler_Init(s_entries_self_term, 1u, bg_step);
    log_reset();
    (void)Scheduler_EnterEntry(0u);
    log_reset();
    Scheduler_Run(7u);
    ASSERT_EQ_INT(3, s_call_count);
    ASSERT_LOG(0, "BG", 7u);
    ASSERT_LOG(1, "S_step", 7u);
    ASSERT_LOG(2, "S_exit", 0u); /* on_exit 即刻执行，本拍无后续 step */
    ASSERT_EQ_INT(SCHEDULER_ENTRY_NONE, (int)Scheduler_GetActiveEntry());
    log_reset();
    Scheduler_Run(8u);
    ASSERT_EQ_INT(1, s_call_count); /* 下拍无条目 step */
    ASSERT_LOG(0, "BG", 8u);
    return true;
}

static bool test_background_enter_takes_effect_same_tick(void)
{
    Scheduler_Init(s_entries_ab_null, ENTRY_AB_COUNT, bg_entering_step);
    log_reset();
    s_bg_do_enter = true;
    Scheduler_Run(9u);
    ASSERT_EQ_INT(3, s_call_count);
    ASSERT_LOG(0, "BGE", 9u);
    ASSERT_LOG(1, "A_enter", 0u);
    ASSERT_LOG(2, "A_step", 9u); /* 首拍 step 同拍生效 */
    ASSERT_EQ_INT(0, (int)Scheduler_GetActiveEntry());
    return true;
}

static bool test_null_hooks_tolerated(void)
{
    setup_ab();
    ASSERT_TRUE(Scheduler_EnterEntry(2u)); /* EntryC 全 NULL 钩子 */
    ASSERT_EQ_INT(2, (int)Scheduler_GetActiveEntry());
    Scheduler_Run(4u);
    Scheduler_LeaveEntry();
    ASSERT_EQ_INT(1, s_call_count); /* 仅背景；NULL 钩子全部安全跳过 */
    ASSERT_LOG(0, "BG", 4u);
    ASSERT_EQ_INT(SCHEDULER_ENTRY_NONE, (int)Scheduler_GetActiveEntry());
    return true;
}

static bool test_init_without_background(void)
{
    Scheduler_Init(s_entries_ab_null, ENTRY_AB_COUNT, NULL);
    log_reset();
    Scheduler_Run(3u);
    ASSERT_EQ_INT(0, s_call_count); /* 无背景无条目：完全无操作 */
    (void)Scheduler_EnterEntry(0u);
    log_reset();
    Scheduler_Run(4u);
    ASSERT_EQ_INT(1, s_call_count);
    ASSERT_LOG(0, "A_step", 4u);
    return true;
}

static bool test_entry_name_and_count_getters(void)
{
    setup_ab();
    ASSERT_EQ_INT((int)ENTRY_AB_COUNT, (int)Scheduler_GetEntryCount());
    ASSERT_TRUE(strcmp(Scheduler_GetEntryName(0u), "EntryA") == 0);
    ASSERT_TRUE(strcmp(Scheduler_GetEntryName(2u), "EntryC") == 0);
    ASSERT_TRUE(Scheduler_GetEntryName(ENTRY_AB_COUNT) == NULL);
    return true;
}

/* 契约修订 1：on_exit 内嵌套 Enter → 嵌套转移胜出，外层 false，
 * 无孤儿 on_enter（每个 on_enter 都保有配对 on_exit 的机会）。 */
static bool test_exit_hook_nested_enter_wins_outer_aborts(void)
{
    Scheduler_Init(s_entries_chain, 2u, NULL);
    log_reset();
    (void)Scheduler_EnterEntry(0u); /* 活动 = EntryX（全 NULL 除 on_exit） */
    log_reset();
    ASSERT_FALSE(Scheduler_EnterEntry(0u)); /* 重进触发 X_exit → 嵌套进 B */
    ASSERT_EQ_INT(2, s_call_count);
    ASSERT_LOG(0, "X_exit", 0u);
    ASSERT_LOG(1, "B_enter", 0u); /* 无外层条目的孤儿 on_enter */
    ASSERT_EQ_INT(1, (int)Scheduler_GetActiveEntry()); /* 嵌套转移保持 */
    log_reset();
    Scheduler_Run(6u);
    ASSERT_EQ_INT(1, s_call_count);
    ASSERT_LOG(0, "B_step", 6u);
    return true;
}

/* Init 重置活动条目：带活动条目时重新 Init，活动清空且不触发旧 on_exit
 * （Init 是装配级复位，不是运行级转移——转移语义唯一实现点在 Enter/Leave）。 */
static bool test_reinit_clears_active_without_exit_hook(void)
{
    setup_ab();
    (void)Scheduler_EnterEntry(0u);
    log_reset();
    Scheduler_Init(s_entries_ab_null, ENTRY_AB_COUNT, bg_step);
    ASSERT_EQ_INT(0, s_call_count);
    ASSERT_EQ_INT(SCHEDULER_ENTRY_NONE, (int)Scheduler_GetActiveEntry());
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
    run_test("test_before_init_all_safe", test_before_init_all_safe);
    run_test("test_empty_table_init_safe", test_empty_table_init_safe);
    run_test("test_idle_run_only_background_passthrough",
             test_idle_run_only_background_passthrough);
    run_test("test_enter_then_run_background_before_step",
             test_enter_then_run_background_before_step);
    run_test("test_switch_exit_before_enter_exactly_once",
             test_switch_exit_before_enter_exactly_once);
    run_test("test_reenter_same_index_restarts",
             test_reenter_same_index_restarts);
    run_test("test_enter_out_of_bounds_false_no_side_effect",
             test_enter_out_of_bounds_false_no_side_effect);
    run_test("test_leave_then_idle_then_repeat_leave_noop",
             test_leave_then_idle_then_repeat_leave_noop);
    run_test("test_step_self_terminate_immediate_exit",
             test_step_self_terminate_immediate_exit);
    run_test("test_background_enter_takes_effect_same_tick",
             test_background_enter_takes_effect_same_tick);
    run_test("test_null_hooks_tolerated", test_null_hooks_tolerated);
    run_test("test_init_without_background", test_init_without_background);
    run_test("test_entry_name_and_count_getters",
             test_entry_name_and_count_getters);
    run_test("test_exit_hook_nested_enter_wins_outer_aborts",
             test_exit_hook_nested_enter_wins_outer_aborts);
    run_test("test_reinit_clears_active_without_exit_hook",
             test_reinit_clears_active_without_exit_hook);

    if (s_failures != 0) {
        printf("Scheduler tests failed: %d\n", s_failures);
        return 1;
    }

    printf("\nAll scheduler tests passed.\n");
    return 0;
}
