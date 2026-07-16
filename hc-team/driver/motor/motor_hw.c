/**
 * @file    motor_hw.c
 * @brief   Motor Driver 的 TI / DL HAL 硬件写层。
 *
 * 这里是本模块唯一允许包含 SysConfig 和 DriverLib 头文件的位置。
 */

#include "driver/motor/motor_hw.h"

#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_gpio.h>
#include <ti/driverlib/dl_timera.h>

/*
 * Compare 口径必须与 board.syscfg 生成配置保持一致。
 * P3.T3 将左右驱动 PWM 统一为 10 kHz：80 MHz / 8000 = 10 kHz。
 */
#define MOTOR_PWM_PERIOD_TICKS 7999u

static void motor_hw_set_dir_pin(uint32_t pin)
{
    DL_GPIO_setPins(GPIO_DRIVE_DIR_PORT, pin);
}

static void motor_hw_clear_dir_pin(uint32_t pin)
{
    DL_GPIO_clearPins(GPIO_DRIVE_DIR_PORT, pin);
}

static uint32_t motor_hw_period_ticks(Motor_Id id)
{
    (void)id;
    return MOTOR_PWM_PERIOD_TICKS;
}

static uint32_t motor_hw_compare_index(Motor_Id id)
{
    if (id == MOTOR_LEFT) {
        return GPIO_PWM_DRIVE_LEFT_C1_IDX;
    }
    return GPIO_PWM_DRIVE_RIGHT_C0_IDX;
}

static GPTIMER_Regs *motor_hw_timer_inst(Motor_Id id)
{
    if (id == MOTOR_LEFT) {
        return PWM_DRIVE_LEFT_INST;
    }
    return PWM_DRIVE_RIGHT_INST;
}

void motor_hw_write_dir(Motor_Id id, int8_t direction)
{
    uint32_t pin_fwd;
    uint32_t pin_rev;

    if (id == MOTOR_LEFT) {
        pin_fwd = GPIO_DRIVE_DIR_BIN2_PIN;
        pin_rev = GPIO_DRIVE_DIR_BIN1_PIN;
    } else {
        pin_fwd = GPIO_DRIVE_DIR_AIN1_PIN;
        pin_rev = GPIO_DRIVE_DIR_AIN2_PIN;
    }

    if (direction > 0) {
        motor_hw_set_dir_pin(pin_fwd);
        motor_hw_clear_dir_pin(pin_rev);
    } else if (direction < 0) {
        motor_hw_clear_dir_pin(pin_fwd);
        motor_hw_set_dir_pin(pin_rev);
    } else {
        motor_hw_clear_dir_pin(pin_fwd);
        motor_hw_clear_dir_pin(pin_rev);
    }
}

void motor_hw_write_duty_permille(Motor_Id id, uint16_t duty_permille)
{
    uint32_t compare = (motor_hw_period_ticks(id) * duty_permille) / MOTOR_OUTPUT_MAX;

    DL_TimerA_setCaptureCompareValue(motor_hw_timer_inst(id),
                                     compare,
                                     motor_hw_compare_index(id));
}

void motor_hw_brake_pins(Motor_Id id)
{
    if (id == MOTOR_LEFT) {
        motor_hw_set_dir_pin(GPIO_DRIVE_DIR_BIN2_PIN);
        motor_hw_set_dir_pin(GPIO_DRIVE_DIR_BIN1_PIN);
    } else {
        motor_hw_set_dir_pin(GPIO_DRIVE_DIR_AIN1_PIN);
        motor_hw_set_dir_pin(GPIO_DRIVE_DIR_AIN2_PIN);
    }
}

void motor_hw_start_pwm(void)
{
    DL_TimerA_setCaptureCompareValue(PWM_DRIVE_LEFT_INST, 0u, GPIO_PWM_DRIVE_LEFT_C1_IDX);
    DL_TimerA_setCaptureCompareValue(PWM_DRIVE_RIGHT_INST, 0u, GPIO_PWM_DRIVE_RIGHT_C0_IDX);
    DL_TimerA_startCounter(PWM_DRIVE_LEFT_INST);
    DL_TimerA_startCounter(PWM_DRIVE_RIGHT_INST);
}
