/**
 * @file    test_tuning.c
 * @brief   Host tests for the VOFA tuning-link service (S03).
 *
 * 契约回顾（phase4 计划表 §9）：
 * - 变量组隔离三原则：只做调参 / 与运行变量分离不相互赋予 / Enter 重进一律安全初值
 * - Q2 定案：解析归 Driver vofa_run()，分发/应用归本服务（cmd → Chassis 公共 API 单向）
 * - Update 自门控 10ms：vofa_run → Apply → RefreshTx（遥测晚一帧）；末尾恒推进内环
 * - Exit：Chassis_Stop + 清 VOFA profile，此后静默且刹车保持
 */
#include "app/service/tuning/tuning.h"

#include "app/service/chassis/chassis.h"
#include "driver/clock/clock.h"
#include "driver/encoder/encoder.h"
#include "driver/motor/motor.h"
#include "driver/uart_vofa/uart_vofa.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* fake 观测/注入接口（fake_motor_hw.c / fake_board_gpio.c / fake_clock.c / fake_uart_port.c） */
extern void FakeMotorHw_ResetLog(void);
extern int FakeMotorHw_GetWriteCount(void);
extern uint16_t FakeMotorHw_GetDutyPermille(Motor_Id id);
extern bool FakeMotorHw_IsBrakeActive(Motor_Id id);
extern void FakeBoardGpio_SetRaw(int32_t left, int32_t right);
extern void FakeClock_Set(uint32_t now_ms);
extern void FakeClock_Advance(uint32_t delta_ms);
extern void FakeUartPort_ResetAll(void);
extern void FakeUartPort_PushVofaBytes(const uint8_t *data, uint32_t length);
extern void FakeUartPort_CompleteVofaTx(void);
extern uint32_t FakeUartPort_CopyVofaTx(uint8_t *out, uint32_t capacity);

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

#define TEST_ASSERT_FLOAT_NEAR(actual, expected, epsilon) do { \
    if (fabsf((actual) - (expected)) > (epsilon)) { \
        printf("FAIL: |%f - %f| > %f at %s:%d\n", \
               (double)(actual), (double)(expected), (double)(epsilon), \
               __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

/* JustFloat 帧：6 通道 ×4 字节 + 4 字节帧尾 */
#define TUNING_FRAME_CHANNELS 6u
#define TUNING_FRAME_BYTES (TUNING_FRAME_CHANNELS * 4u + 4u)

/* 标准装配序：与 SysInit 职责一致（Clock/Motor/Encoder/vofa 先于 Service） */
static void setup(void)
{
    Clock_Init();
    FakeClock_Set(1000u);
    FakeBoardGpio_SetRaw(0, 0);
    Motor_Init();
    Encoder_Init();
    Chassis_Init();
    (void)vofa_init();          /* 内部执行 VofaUart_Init + 清空 profile */
    FakeMotorHw_ResetLog();
    Tuning_Init();
}

static void push_cmd(const char *text)
{
    FakeUartPort_PushVofaBytes((const uint8_t *)text, (uint32_t)strlen(text));
}

/* 抓取最近一帧并按 little-endian 解出 count 个 float；返回帧长（0 = 无帧） */
static uint32_t copy_frame_floats(float *out, uint32_t count)
{
    uint8_t raw[TUNING_FRAME_BYTES + 16u];
    uint32_t len = FakeUartPort_CopyVofaTx(raw, sizeof(raw));
    uint32_t i;

    if (len >= TUNING_FRAME_BYTES) {
        for (i = 0u; i < count; i++) {
            memcpy(&out[i], &raw[i * 4u], 4u);
        }
    }
    return len;
}

/* 安全项：Init 后与 NONE 态 Update 完全静默——无帧、无电机命令、不推底盘 */
static int test_init_and_none_update_silent(void)
{
    uint8_t raw[64];

    setup();
    FakeClock_Advance(50u);
    Tuning_Update();
    Tuning_Update();
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) == 0u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == 0);
    TEST_ASSERT_TRUE(Tuning_GetActiveProfile() == TUNING_PROFILE_NONE);
    printf("PASS: test_init_and_none_update_silent\n");
    return 0;
}

/* 未知 profile：拒绝进入且保持静默 */
static int test_enter_invalid_rejected_silent(void)
{
    uint8_t raw[64];

    setup();
    TEST_ASSERT_TRUE(!Tuning_EnterProfile((Tuning_Profile)777));
    TEST_ASSERT_TRUE(!Tuning_EnterProfile(TUNING_PROFILE_NONE));
    TEST_ASSERT_TRUE(Tuning_GetActiveProfile() == TUNING_PROFILE_NONE);
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) == 0u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetWriteCount() == 0);
    printf("PASS: test_enter_invalid_rejected_silent\n");
    return 0;
}

/* 安全初值可观测：Enter 后首帧 = 6 通道全 0 */
static int test_enter_first_frame_all_safe_zero(void)
{
    float ch[TUNING_FRAME_CHANNELS];
    uint32_t i;

    setup();
    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    FakeClock_Advance(10u);
    Tuning_Update();
    TEST_ASSERT_TRUE(copy_frame_floats(ch, TUNING_FRAME_CHANNELS) == TUNING_FRAME_BYTES);
    for (i = 0u; i < TUNING_FRAME_CHANNELS; i++) {
        TEST_ASSERT_FLOAT_NEAR(ch[i], 0.0f, 1e-6f);
    }
    printf("PASS: test_enter_first_frame_all_safe_zero\n");
    return 0;
}

/* 安全项：Enter 即安全——底盘残留增益/目标被安全 cmd 覆写，刹车起点，随后不出力 */
static int test_enter_overrides_residual_chassis(void)
{
    Chassis_Telemetry_T t;
    int i;

    setup();
    /* 模拟上一任务残留：底盘带增益带目标在跑 */
    Chassis_SetSpeedGains(CHASSIS_SIDE_LEFT, 100.0f, 0.0f, 0.0f);
    Chassis_SetSpeedGains(CHASSIS_SIDE_RIGHT, 100.0f, 0.0f, 0.0f);
    Chassis_SetTargetMps(1.0f, 1.0f);
    FakeClock_Advance(10u);
    Chassis_Update();
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) > 0u);

    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_RIGHT], 0.0f, 1e-6f);

    /* 增益/目标均为安全 0：继续推进不再出力 */
    for (i = 0; i < 5; i++) {
        FakeClock_Advance(10u);
        Tuning_Update();
        FakeUartPort_CompleteVofaTx();
    }
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == 0u);
    printf("PASS: test_enter_overrides_residual_chassis\n");
    return 0;
}

/* RX 目标经服务应用：LM/RM → Chassis 目标（Q2 分发收口主链路） */
static int test_rx_target_applies_via_service(void)
{
    Chassis_Telemetry_T t;

    setup();
    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    push_cmd("LM=0.5\nRM=0.25\n");
    FakeClock_Advance(10u);
    Tuning_Update();
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_LEFT], 0.5f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_RIGHT], 0.25f, 1e-6f);
    printf("PASS: test_rx_target_applies_via_service\n");
    return 0;
}

/* 悬挂调参主链路：LP+LM → 左轮 PID 出力驱动电机 */
static int test_rx_gains_drive_suspended_motor(void)
{
    Chassis_Telemetry_T t;

    setup();
    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    push_cmd("LP=100\nLM=1\n");
    FakeClock_Advance(10u);
    Tuning_Update();    /* 本拍：解析 + 应用；内环同拍到期出力 */
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_LEFT], 100.0f, 1e-3f);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) > 0u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == 0u);
    printf("PASS: test_rx_gains_drive_suspended_motor\n");
    return 0;
}

/* 分离性：外部改运行目标不回写 cmd 组——下一拍被 cmd 覆写回 */
static int test_separation_running_value_never_writes_back(void)
{
    Chassis_Telemetry_T t;

    setup();
    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    push_cmd("LM=0.5\n");
    FakeClock_Advance(10u);
    Tuning_Update();
    FakeUartPort_CompleteVofaTx();

    /* 旁路修改运行变量（模拟其他代码触碰底盘目标） */
    Chassis_SetTargetMps(0.9f, 0.9f);
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_LEFT], 0.9f, 1e-6f);

    /* 下一拍：cmd 组未被运行值污染，重新单向应用 0.5 */
    FakeClock_Advance(10u);
    Tuning_Update();
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_LEFT], 0.5f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_RIGHT], 0.0f, 1e-6f);
    printf("PASS: test_separation_running_value_never_writes_back\n");
    return 0;
}

/* tx = 快照单向副本：帧内容等于上一拍 Chassis 遥测（晚一帧语义） */
static int test_tx_frame_is_snapshot_copy(void)
{
    Chassis_Telemetry_T t_at_refresh;
    float ch[TUNING_FRAME_CHANNELS];

    setup();
    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    push_cmd("LM=0.5\nRM=0.25\n");
    FakeClock_Advance(10u);
    Tuning_Update();                    /* 拍 1：应用 cmd，拍末刷新 tx */
    FakeUartPort_CompleteVofaTx();
    Chassis_GetTelemetry(&t_at_refresh);

    FakeClock_Advance(10u);
    Tuning_Update();                    /* 拍 2：发出拍 1 刷新的 tx */
    TEST_ASSERT_TRUE(copy_frame_floats(ch, TUNING_FRAME_CHANNELS) == TUNING_FRAME_BYTES);
    TEST_ASSERT_FLOAT_NEAR(ch[0], t_at_refresh.target_mps[CHASSIS_SIDE_LEFT], 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(ch[1], t_at_refresh.target_mps[CHASSIS_SIDE_RIGHT], 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(ch[2], t_at_refresh.feedback_mps[CHASSIS_SIDE_LEFT], 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(ch[3], t_at_refresh.feedback_mps[CHASSIS_SIDE_RIGHT], 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(ch[4], t_at_refresh.pid_out[CHASSIS_SIDE_LEFT], 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(ch[5], t_at_refresh.pid_out[CHASSIS_SIDE_RIGHT], 1e-6f);
    printf("PASS: test_tx_frame_is_snapshot_copy\n");
    return 0;
}

/* 10ms 门控：不足周期的 Update 不跑 VOFA 链路（RX 不解析、不发新帧） */
static int test_stream_period_gating(void)
{
    Chassis_Telemetry_T t;
    float ch[TUNING_FRAME_CHANNELS];

    setup();
    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    FakeClock_Advance(10u);
    Tuning_Update();                    /* 首帧（全 0） */
    FakeUartPort_CompleteVofaTx();

    push_cmd("LM=0.5\n");
    FakeClock_Advance(9u);
    Tuning_Update();                    /* 未到期：RX 不解析 */
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    TEST_ASSERT_TRUE(copy_frame_floats(ch, TUNING_FRAME_CHANNELS) == TUNING_FRAME_BYTES);
    TEST_ASSERT_FLOAT_NEAR(ch[0], 0.0f, 1e-6f); /* 仍是首帧内容，无新帧 */

    FakeClock_Advance(1u);
    Tuning_Update();                    /* 累计 10ms 到期：解析 + 应用 */
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_LEFT], 0.5f, 1e-6f);
    printf("PASS: test_stream_period_gating\n");
    return 0;
}

/* 级联：Tuning_Update 恒推进内环——编码器反馈经 chassis 出现在遥测里 */
static int test_cascade_pumps_inner_loop(void)
{
    Chassis_Telemetry_T t;

    setup();
    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    FakeBoardGpio_SetRaw(100, 100);
    FakeClock_Advance(10u);
    Tuning_Update();
    Chassis_GetTelemetry(&t);
    /* Driver 唯一方向修正点 s_direction_sign={-1,+1}：右正左负（口径同 test_chassis） */
    TEST_ASSERT_TRUE(t.feedback_mps[CHASSIS_SIDE_RIGHT] > 0.0f);
    TEST_ASSERT_TRUE(t.feedback_mps[CHASSIS_SIDE_LEFT] < 0.0f);
    printf("PASS: test_cascade_pumps_inner_loop\n");
    return 0;
}

/* 安全项：Exit → 刹车保持 + 无新帧 + 不再推进内环 */
static int test_exit_brakes_and_silences(void)
{
    uint8_t before[TUNING_FRAME_BYTES + 16u];
    uint8_t after[TUNING_FRAME_BYTES + 16u];
    uint32_t len_before;
    int i;

    setup();
    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    push_cmd("LP=100\nLM=1\n");
    FakeClock_Advance(10u);
    Tuning_Update();
    FakeUartPort_CompleteVofaTx();
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) > 0u);

    Tuning_ExitProfile();
    TEST_ASSERT_TRUE(Tuning_GetActiveProfile() == TUNING_PROFILE_NONE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));

    len_before = FakeUartPort_CopyVofaTx(before, sizeof(before));
    for (i = 0; i < 5; i++) {
        FakeClock_Advance(10u);
        Tuning_Update();
    }
    /* 刹车保持（无人推进内环覆盖）；TX 无新帧（缓冲内容与长度不变） */
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(after, sizeof(after)) == len_before);
    TEST_ASSERT_TRUE(memcmp(before, after, len_before) == 0);
    printf("PASS: test_exit_brakes_and_silences\n");
    return 0;
}

/* 安全项：重进不保参——上一会话的调参被重置回安全 0 */
static int test_reenter_resets_cmd_to_safe(void)
{
    Chassis_Telemetry_T t;
    int i;

    setup();
    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    push_cmd("LP=100\nRP=100\nLM=1\nRM=1\n");
    FakeClock_Advance(10u);
    Tuning_Update();
    FakeUartPort_CompleteVofaTx();
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) > 0u);
    Tuning_ExitProfile();

    /* 重进：cmd 组必须回安全值，悬挂车辆不出力 */
    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    for (i = 0; i < 5; i++) {
        FakeClock_Advance(10u);
        Tuning_Update();
        FakeUartPort_CompleteVofaTx();
    }
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_RIGHT], 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(t.pid_out[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == 0u);
    printf("PASS: test_reenter_resets_cmd_to_safe\n");
    return 0;
}

/* 安全项（契约修订 1）：NONE 期间积压的历史命令在 Enter 时被排空，不带入新会话 */
static int test_enter_drains_stale_rx_commands(void)
{
    Chassis_Telemetry_T t;
    int i;

    setup();
    /* 会话间上位机残留：NONE 态 FIFO 里积压危险命令 */
    push_cmd("LM=1\nLP=100\nRM=1\nRP=100\n");
    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    for (i = 0; i < 5; i++) {
        FakeClock_Advance(10u);
        Tuning_Update();
        FakeUartPort_CompleteVofaTx();
    }
    Chassis_GetTelemetry(&t);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_LEFT], 0.0f, 1e-6f);
    TEST_ASSERT_FLOAT_NEAR(t.target_mps[CHASSIS_SIDE_RIGHT], 0.0f, 1e-6f);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) == 0u);
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_RIGHT) == 0u);
    printf("PASS: test_enter_drains_stale_rx_commands\n");
    return 0;
}

/* 契约行为：激活态传入无效 profile 等效 Exit——刹车 + 回 NONE + 此后静默 */
static int test_enter_invalid_while_active_acts_as_exit(void)
{
    uint8_t raw[TUNING_FRAME_BYTES + 16u];
    uint32_t len_before;
    int i;

    setup();
    TEST_ASSERT_TRUE(Tuning_EnterProfile(TUNING_PROFILE_CHASSIS_SPEED));
    push_cmd("LP=100\nLM=1\n");
    FakeClock_Advance(10u);
    Tuning_Update();
    FakeUartPort_CompleteVofaTx();
    TEST_ASSERT_TRUE(FakeMotorHw_GetDutyPermille(MOTOR_LEFT) > 0u);

    TEST_ASSERT_TRUE(!Tuning_EnterProfile((Tuning_Profile)777));
    TEST_ASSERT_TRUE(Tuning_GetActiveProfile() == TUNING_PROFILE_NONE);
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_RIGHT));

    len_before = FakeUartPort_CopyVofaTx(raw, sizeof(raw));
    for (i = 0; i < 5; i++) {
        FakeClock_Advance(10u);
        Tuning_Update();
    }
    TEST_ASSERT_TRUE(FakeMotorHw_IsBrakeActive(MOTOR_LEFT));
    TEST_ASSERT_TRUE(FakeUartPort_CopyVofaTx(raw, sizeof(raw)) == len_before);
    printf("PASS: test_enter_invalid_while_active_acts_as_exit\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_init_and_none_update_silent();
    failures += test_enter_invalid_rejected_silent();
    failures += test_enter_first_frame_all_safe_zero();
    failures += test_enter_overrides_residual_chassis();
    failures += test_rx_target_applies_via_service();
    failures += test_rx_gains_drive_suspended_motor();
    failures += test_separation_running_value_never_writes_back();
    failures += test_tx_frame_is_snapshot_copy();
    failures += test_stream_period_gating();
    failures += test_cascade_pumps_inner_loop();
    failures += test_exit_brakes_and_silences();
    failures += test_reenter_resets_cmd_to_safe();
    failures += test_enter_drains_stale_rx_commands();
    failures += test_enter_invalid_while_active_acts_as_exit();

    if (failures != 0) {
        printf("%d tuning test(s) failed.\n", failures);
        return 1;
    }
    printf("All tuning tests passed.\n");
    return 0;
}
