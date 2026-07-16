#include "driver/motor/motor_hw.h"

#include <stdbool.h>

typedef struct {
    int16_t direction;
    uint16_t duty_permille;
    bool brake_active;
    int write_dir_seq;
    int write_duty_seq;
    int brake_seq;
} FakeMotorState;

static FakeMotorState s_fake_motors[MOTOR_COUNT];
static int s_sequence;
static int s_write_count;
static int s_write_dir_count;
static int s_write_duty_count;
static int s_start_pwm_count;
static int s_brake_count;

void FakeMotorHw_ResetLog(void)
{
    Motor_Id id;

    s_sequence = 0;
    s_write_count = 0;
    s_write_dir_count = 0;
    s_write_duty_count = 0;
    s_start_pwm_count = 0;
    s_brake_count = 0;

    for (id = MOTOR_LEFT; id < MOTOR_COUNT; ++id) {
        s_fake_motors[id].direction = 0;
        s_fake_motors[id].duty_permille = 0u;
        s_fake_motors[id].brake_active = false;
        s_fake_motors[id].write_dir_seq = 0;
        s_fake_motors[id].write_duty_seq = 0;
        s_fake_motors[id].brake_seq = 0;
    }
}

void motor_hw_write_dir(Motor_Id id, int8_t direction)
{
    s_sequence++;
    s_write_count++;
    s_write_dir_count++;
    s_fake_motors[id].direction = direction;
    s_fake_motors[id].brake_active = false;
    s_fake_motors[id].write_dir_seq = s_sequence;
}

void motor_hw_write_duty_permille(Motor_Id id, uint16_t duty_permille)
{
    s_sequence++;
    s_write_count++;
    s_write_duty_count++;
    s_fake_motors[id].duty_permille = duty_permille;
    s_fake_motors[id].write_duty_seq = s_sequence;
}

void motor_hw_brake_pins(Motor_Id id)
{
    s_sequence++;
    s_brake_count++;
    s_fake_motors[id].brake_active = true;
    s_fake_motors[id].brake_seq = s_sequence;
}

void motor_hw_start_pwm(void)
{
    s_start_pwm_count++;
}

int FakeMotorHw_GetWriteCount(void)
{
    return s_write_count;
}

int FakeMotorHw_GetWriteDirCount(void)
{
    return s_write_dir_count;
}

int FakeMotorHw_GetWriteDutyCount(void)
{
    return s_write_duty_count;
}

int FakeMotorHw_GetStartPwmCount(void)
{
    return s_start_pwm_count;
}

int FakeMotorHw_GetBrakeCount(void)
{
    return s_brake_count;
}

int16_t FakeMotorHw_GetDir(Motor_Id id)
{
    return s_fake_motors[id].direction;
}

uint16_t FakeMotorHw_GetDutyPermille(Motor_Id id)
{
    return s_fake_motors[id].duty_permille;
}

bool FakeMotorHw_IsBrakeActive(Motor_Id id)
{
    return s_fake_motors[id].brake_active;
}

int FakeMotorHw_GetBrakeSeq(Motor_Id id)
{
    return s_fake_motors[id].brake_seq;
}

int FakeMotorHw_GetDutySeq(Motor_Id id)
{
    return s_fake_motors[id].write_duty_seq;
}
