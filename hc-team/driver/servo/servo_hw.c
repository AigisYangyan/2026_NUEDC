/**
 * @file    servo_hw.c
 * @brief   Servo Driver 的 TI / DL HAL 硬件写层。
 *
 * 这里是本模块唯一允许包含 SysConfig 和 DriverLib 头文件的位置。
 * 两路均 EDGE_ALIGN_UP、period 49999、计数时钟 2.5MHz（TIMG7 走 80MHz 域 /8/4，
 * TIMG0 走 40MHz 域 /8/2——生成代码核对过 50Hz，勿凭名字猜）。
 */
#include "driver/servo/servo_hw.h"

#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_timerg.h>

static GPTIMER_Regs *servo_hw_inst(Servo_Id id)
{
    return (id == SERVO_1) ? PWM_SERVO_1_INST : PWM_SERVO_2_INST;
}

static uint32_t servo_hw_clk_hz(Servo_Id id)
{
    return (id == SERVO_1) ? PWM_SERVO_1_INST_CLK_FREQ : PWM_SERVO_2_INST_CLK_FREQ;
}

void servo_hw_start(void)
{
    DL_TimerG_setCaptureCompareValue(PWM_SERVO_1_INST, 0u, DL_TIMER_CC_1_INDEX);
    DL_TimerG_setCaptureCompareValue(PWM_SERVO_2_INST, 0u, DL_TIMER_CC_1_INDEX);
    DL_TimerG_startCounter(PWM_SERVO_1_INST);
    DL_TimerG_startCounter(PWM_SERVO_2_INST);
}

void servo_hw_write_pulse_us(Servo_Id id, uint32_t us)
{
    uint32_t ticks = (uint32_t)(((uint64_t)us * servo_hw_clk_hz(id)) / 1000000u);

    DL_TimerG_setCaptureCompareValue(servo_hw_inst(id), ticks, DL_TIMER_CC_1_INDEX);
}

void servo_hw_stop_pulse(Servo_Id id)
{
    DL_TimerG_setCaptureCompareValue(servo_hw_inst(id), 0u, DL_TIMER_CC_1_INDEX);
}
