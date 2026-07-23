/*
 * Copyright (c) 2023, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ============ ti_msp_dl_config.c =============
 *  Configured MSPM0 DriverLib module definitions
 *
 *  DO NOT EDIT - This file is generated for the MSPM0G351X
 *  by the SysConfig tool.
 */

#include "ti_msp_dl_config.h"

DL_TimerA_backupConfig gPWM_DRIVE_LEFTBackup;
DL_TimerA_backupConfig gPWM_DRIVE_RIGHTBackup;
DL_TimerG_backupConfig gPWM_SERVO_1Backup;
DL_TimerG_backupConfig gQEI_LEFTBackup;
DL_UART_Main_backupConfig gUART_IMUBackup;

/*
 *  ======== SYSCFG_DL_init ========
 *  Perform any initialization needed before using any board APIs
 */
SYSCONFIG_WEAK void SYSCFG_DL_init(void)
{
    SYSCFG_DL_initPower();
    SYSCFG_DL_GPIO_init();
    /* Module-Specific Initializations*/
    SYSCFG_DL_SYSCTL_init();
    SYSCFG_DL_PWM_DRIVE_LEFT_init();
    SYSCFG_DL_PWM_DRIVE_RIGHT_init();
    SYSCFG_DL_PWM_SERVO_1_init();
    SYSCFG_DL_PWM_SERVO_2_init();
    SYSCFG_DL_QEI_RIGHT_init();
    SYSCFG_DL_QEI_LEFT_init();
    SYSCFG_DL_I2C_AUX_init();
    SYSCFG_DL_UART_STEPPER_BUS_init();
    SYSCFG_DL_UART_HOST_LINK_init();
    SYSCFG_DL_UART_VISION_init();
    SYSCFG_DL_UART_IMU_init();
    SYSCFG_DL_UART_BSL_ENTRY_init();
    SYSCFG_DL_DMA_init();
    /* Ensure backup structures have no valid state */
	gPWM_DRIVE_LEFTBackup.backupRdy 	= false;
	gPWM_DRIVE_RIGHTBackup.backupRdy 	= false;
	gPWM_SERVO_1Backup.backupRdy 	= false;
	gQEI_LEFTBackup.backupRdy 	= false;
	gUART_IMUBackup.backupRdy 	= false;

}
/*
 * User should take care to save and restore register configuration in application.
 * See Retention Configuration section for more details.
 */
SYSCONFIG_WEAK bool SYSCFG_DL_saveConfiguration(void)
{
    bool retStatus = true;

	retStatus &= DL_TimerA_saveConfiguration(PWM_DRIVE_LEFT_INST, &gPWM_DRIVE_LEFTBackup);
	retStatus &= DL_TimerA_saveConfiguration(PWM_DRIVE_RIGHT_INST, &gPWM_DRIVE_RIGHTBackup);
	retStatus &= DL_TimerG_saveConfiguration(PWM_SERVO_1_INST, &gPWM_SERVO_1Backup);
	retStatus &= DL_TimerG_saveConfiguration(QEI_LEFT_INST, &gQEI_LEFTBackup);
	retStatus &= DL_UART_Main_saveConfiguration(UART_IMU_INST, &gUART_IMUBackup);

    return retStatus;
}


SYSCONFIG_WEAK bool SYSCFG_DL_restoreConfiguration(void)
{
    bool retStatus = true;

	retStatus &= DL_TimerA_restoreConfiguration(PWM_DRIVE_LEFT_INST, &gPWM_DRIVE_LEFTBackup, false);
	retStatus &= DL_TimerA_restoreConfiguration(PWM_DRIVE_RIGHT_INST, &gPWM_DRIVE_RIGHTBackup, false);
	retStatus &= DL_TimerG_restoreConfiguration(PWM_SERVO_1_INST, &gPWM_SERVO_1Backup, false);
	retStatus &= DL_TimerG_restoreConfiguration(QEI_LEFT_INST, &gQEI_LEFTBackup, false);
	retStatus &= DL_UART_Main_restoreConfiguration(UART_IMU_INST, &gUART_IMUBackup);

    return retStatus;
}

SYSCONFIG_WEAK void SYSCFG_DL_initPower(void)
{
    DL_GPIO_reset(GPIOA);
    DL_GPIO_reset(GPIOB);
    DL_GPIO_reset(GPIOC);
    DL_TimerA_reset(PWM_DRIVE_LEFT_INST);
    DL_TimerA_reset(PWM_DRIVE_RIGHT_INST);
    DL_TimerG_reset(PWM_SERVO_1_INST);
    DL_TimerG_reset(PWM_SERVO_2_INST);
    DL_TimerG_reset(QEI_RIGHT_INST);
    DL_TimerG_reset(QEI_LEFT_INST);
    DL_I2C_reset(I2C_AUX_INST);
    DL_UART_Main_reset(UART_STEPPER_BUS_INST);
    DL_UART_Main_reset(UART_HOST_LINK_INST);
    DL_UART_Main_reset(UART_VISION_INST);
    DL_UART_Main_reset(UART_IMU_INST);
    DL_UART_Main_reset(UART_BSL_ENTRY_INST);


    DL_GPIO_enablePower(GPIOA);
    DL_GPIO_enablePower(GPIOB);
    DL_GPIO_enablePower(GPIOC);
    DL_TimerA_enablePower(PWM_DRIVE_LEFT_INST);
    DL_TimerA_enablePower(PWM_DRIVE_RIGHT_INST);
    DL_TimerG_enablePower(PWM_SERVO_1_INST);
    DL_TimerG_enablePower(PWM_SERVO_2_INST);
    DL_TimerG_enablePower(QEI_RIGHT_INST);
    DL_TimerG_enablePower(QEI_LEFT_INST);
    DL_I2C_enablePower(I2C_AUX_INST);
    DL_UART_Main_enablePower(UART_STEPPER_BUS_INST);
    DL_UART_Main_enablePower(UART_HOST_LINK_INST);
    DL_UART_Main_enablePower(UART_VISION_INST);
    DL_UART_Main_enablePower(UART_IMU_INST);
    DL_UART_Main_enablePower(UART_BSL_ENTRY_INST);

    delay_cycles(POWER_STARTUP_DELAY);
}

SYSCONFIG_WEAK void SYSCFG_DL_GPIO_init(void)
{

    DL_GPIO_initPeripheralOutputFunction(GPIO_PWM_DRIVE_LEFT_C1_IOMUX,GPIO_PWM_DRIVE_LEFT_C1_IOMUX_FUNC);
    DL_GPIO_enableOutput(GPIO_PWM_DRIVE_LEFT_C1_PORT, GPIO_PWM_DRIVE_LEFT_C1_PIN);
    DL_GPIO_initPeripheralOutputFunction(GPIO_PWM_DRIVE_RIGHT_C0_IOMUX,GPIO_PWM_DRIVE_RIGHT_C0_IOMUX_FUNC);
    DL_GPIO_enableOutput(GPIO_PWM_DRIVE_RIGHT_C0_PORT, GPIO_PWM_DRIVE_RIGHT_C0_PIN);
    DL_GPIO_initPeripheralOutputFunction(GPIO_PWM_SERVO_1_C1_IOMUX,GPIO_PWM_SERVO_1_C1_IOMUX_FUNC);
    DL_GPIO_enableOutput(GPIO_PWM_SERVO_1_C1_PORT, GPIO_PWM_SERVO_1_C1_PIN);
    DL_GPIO_initPeripheralOutputFunction(GPIO_PWM_SERVO_2_C1_IOMUX,GPIO_PWM_SERVO_2_C1_IOMUX_FUNC);
    DL_GPIO_enableOutput(GPIO_PWM_SERVO_2_C1_PORT, GPIO_PWM_SERVO_2_C1_PIN);

    DL_GPIO_initPeripheralInputFunction(GPIO_QEI_RIGHT_PHA_IOMUX,GPIO_QEI_RIGHT_PHA_IOMUX_FUNC);
    DL_GPIO_initPeripheralInputFunction(GPIO_QEI_RIGHT_PHB_IOMUX,GPIO_QEI_RIGHT_PHB_IOMUX_FUNC);
    DL_GPIO_initPeripheralInputFunction(GPIO_QEI_LEFT_PHA_IOMUX,GPIO_QEI_LEFT_PHA_IOMUX_FUNC);
    DL_GPIO_initPeripheralInputFunction(GPIO_QEI_LEFT_PHB_IOMUX,GPIO_QEI_LEFT_PHB_IOMUX_FUNC);

    DL_GPIO_initPeripheralInputFunctionFeatures(GPIO_I2C_AUX_IOMUX_SDA,
        GPIO_I2C_AUX_IOMUX_SDA_FUNC, DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE, DL_GPIO_HYSTERESIS_DISABLE,
        DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_initPeripheralInputFunctionFeatures(GPIO_I2C_AUX_IOMUX_SCL,
        GPIO_I2C_AUX_IOMUX_SCL_FUNC, DL_GPIO_INVERSION_DISABLE,
        DL_GPIO_RESISTOR_NONE, DL_GPIO_HYSTERESIS_DISABLE,
        DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_enableHiZ(GPIO_I2C_AUX_IOMUX_SDA);
    DL_GPIO_enableHiZ(GPIO_I2C_AUX_IOMUX_SCL);

    DL_GPIO_initPeripheralOutputFunction(
        GPIO_UART_STEPPER_BUS_IOMUX_TX, GPIO_UART_STEPPER_BUS_IOMUX_TX_FUNC);
    DL_GPIO_initPeripheralInputFunction(
        GPIO_UART_STEPPER_BUS_IOMUX_RX, GPIO_UART_STEPPER_BUS_IOMUX_RX_FUNC);
    DL_GPIO_initPeripheralOutputFunction(
        GPIO_UART_HOST_LINK_IOMUX_TX, GPIO_UART_HOST_LINK_IOMUX_TX_FUNC);
    DL_GPIO_initPeripheralInputFunction(
        GPIO_UART_HOST_LINK_IOMUX_RX, GPIO_UART_HOST_LINK_IOMUX_RX_FUNC);
    DL_GPIO_initPeripheralOutputFunction(
        GPIO_UART_VISION_IOMUX_TX, GPIO_UART_VISION_IOMUX_TX_FUNC);
    DL_GPIO_initPeripheralInputFunction(
        GPIO_UART_VISION_IOMUX_RX, GPIO_UART_VISION_IOMUX_RX_FUNC);
    DL_GPIO_initPeripheralOutputFunction(
        GPIO_UART_IMU_IOMUX_TX, GPIO_UART_IMU_IOMUX_TX_FUNC);
    DL_GPIO_initPeripheralInputFunction(
        GPIO_UART_IMU_IOMUX_RX, GPIO_UART_IMU_IOMUX_RX_FUNC);
    DL_GPIO_initPeripheralOutputFunction(
        GPIO_UART_BSL_ENTRY_IOMUX_TX, GPIO_UART_BSL_ENTRY_IOMUX_TX_FUNC);
    DL_GPIO_initPeripheralInputFunction(
        GPIO_UART_BSL_ENTRY_IOMUX_RX, GPIO_UART_BSL_ENTRY_IOMUX_RX_FUNC);

    DL_GPIO_initDigitalOutputFeatures(GPIO_STATUS_LED_PIN_22_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_DOWN,
		 DL_GPIO_DRIVE_STRENGTH_LOW, DL_GPIO_HIZ_DISABLE);

    DL_GPIO_initDigitalOutput(GPIO_BEACON_BUZZER_IOMUX);

    DL_GPIO_initDigitalInputFeatures(GPIO_GRP_KEY_K1_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_GRP_KEY_K2_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_GRP_KEY_K3_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_GRP_KEY_K4_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_LINE_SENSOR_PIN_IN1_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_LINE_SENSOR_PIN_IN2_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_LINE_SENSOR_PIN_IN3_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_LINE_SENSOR_PIN_IN4_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_LINE_SENSOR_PIN_IN5_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_LINE_SENSOR_PIN_IN6_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_LINE_SENSOR_PIN_IN7_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_LINE_SENSOR_PIN_IN8_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_LINE_SENSOR_PIN_IN9_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_LINE_SENSOR_PIN_IN10_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_LINE_SENSOR_PIN_IN11_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalInputFeatures(GPIO_LINE_SENSOR_PIN_IN12_IOMUX,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);

    DL_GPIO_initDigitalOutput(GPIO_DRIVE_DIR_BIN1_IOMUX);

    DL_GPIO_initDigitalOutput(GPIO_DRIVE_DIR_BIN2_IOMUX);

    DL_GPIO_initDigitalOutput(GPIO_DRIVE_DIR_AIN1_IOMUX);

    DL_GPIO_initDigitalOutput(GPIO_DRIVE_DIR_AIN2_IOMUX);

    DL_GPIO_clearPins(GPIOA, GPIO_DRIVE_DIR_BIN1_PIN |
		GPIO_DRIVE_DIR_BIN2_PIN |
		GPIO_DRIVE_DIR_AIN1_PIN |
		GPIO_DRIVE_DIR_AIN2_PIN);
    DL_GPIO_enableOutput(GPIOA, GPIO_DRIVE_DIR_BIN1_PIN |
		GPIO_DRIVE_DIR_BIN2_PIN |
		GPIO_DRIVE_DIR_AIN1_PIN |
		GPIO_DRIVE_DIR_AIN2_PIN);
    DL_GPIO_setLowerPinsPolarity(GPIOA, DL_GPIO_PIN_14_EDGE_FALL);
    DL_GPIO_clearInterruptStatus(GPIOA, GPIO_GRP_KEY_K4_PIN);
    DL_GPIO_enableInterrupt(GPIOA, GPIO_GRP_KEY_K4_PIN);
    DL_GPIO_setPublisherChanID(GPIOA, DL_GPIO_PUBLISHER_INDEX_0, GPIOA_EVENT_PUBLISHER_0_CHANNEL);
    DL_GPIO_enableEvents(GPIOA, DL_GPIO_EVENT_ROUTE_1, GPIO_GRP_KEY_K4_PIN);
    DL_GPIO_clearPins(GPIOB, GPIO_STATUS_LED_PIN_22_PIN |
		GPIO_BEACON_BUZZER_PIN);
    DL_GPIO_enableOutput(GPIOB, GPIO_STATUS_LED_PIN_22_PIN |
		GPIO_BEACON_BUZZER_PIN);
    DL_GPIO_setLowerPinsPolarity(GPIOB, DL_GPIO_PIN_4_EDGE_FALL |
		DL_GPIO_PIN_5_EDGE_FALL);
    DL_GPIO_setUpperPinsPolarity(GPIOB, DL_GPIO_PIN_25_EDGE_FALL);
    DL_GPIO_clearInterruptStatus(GPIOB, GPIO_GRP_KEY_K1_PIN |
		GPIO_GRP_KEY_K2_PIN |
		GPIO_GRP_KEY_K3_PIN);
    DL_GPIO_enableInterrupt(GPIOB, GPIO_GRP_KEY_K1_PIN |
		GPIO_GRP_KEY_K2_PIN |
		GPIO_GRP_KEY_K3_PIN);
    DL_GPIO_setPublisherChanID(GPIOB, DL_GPIO_PUBLISHER_INDEX_0, GPIOB_EVENT_PUBLISHER_0_CHANNEL);
    DL_GPIO_enableEvents(GPIOB, DL_GPIO_EVENT_ROUTE_1, GPIO_GRP_KEY_K1_PIN |
		GPIO_GRP_KEY_K2_PIN);
    DL_GPIO_setPublisherChanID(GPIOB, DL_GPIO_PUBLISHER_INDEX_1, GPIOB_EVENT_PUBLISHER_1_CHANNEL);
    DL_GPIO_enableEvents(GPIOB, DL_GPIO_EVENT_ROUTE_2, GPIO_GRP_KEY_K3_PIN);

}


static const DL_SYSCTL_SYSPLLConfig gSYSPLLConfig = {
    .inputFreq              = DL_SYSCTL_SYSPLL_INPUT_FREQ_16_32_MHZ,
	.rDivClk2x              = 3,
	.rDivClk1               = 0,
	.rDivClk0               = 0,
	.enableCLK2x            = DL_SYSCTL_SYSPLL_CLK2X_ENABLE,
	.enableCLK1             = DL_SYSCTL_SYSPLL_CLK1_DISABLE,
	.enableCLK0             = DL_SYSCTL_SYSPLL_CLK0_DISABLE,
	.sysPLLMCLK             = DL_SYSCTL_SYSPLL_MCLK_CLK2X,
	.sysPLLRef              = DL_SYSCTL_SYSPLL_REF_SYSOSC,
	.qDiv                   = 9,
	.pDiv                   = DL_SYSCTL_SYSPLL_PDIV_2
};

SYSCONFIG_WEAK bool SYSCFG_DL_SYSCTL_SYSPLL_init(void)
{
    bool fFCCRatioStatus = false;
    uint32_t fFCCSysoscCount;
    uint32_t fFCCPllCount;
    uint32_t fFCCRatio;
    uint32_t fccTimeOutCounter;

    DL_SYSCTL_setFCCPeriods( DL_SYSCTL_FCC_TRIG_CNT_01 );

    /* Measuring PLL. */
    DL_SYSCTL_configFCC(DL_SYSCTL_FCC_TRIG_TYPE_RISE_RISE,
                        DL_SYSCTL_FCC_TRIG_SOURCE_LFCLK,
                        DL_SYSCTL_FCC_CLOCK_SOURCE_SYSPLLCLK2X);
    /* Get SYSPLL frequency using FCC */
    fccTimeOutCounter = 0;
    DL_SYSCTL_startFCC();
    while (DL_SYSCTL_isFCCDone() == 0) {
        delay_cycles(977);  /* 1x LFCLK cycle = 32MHz/32.768kHz = 977, 30.5us */
        fccTimeOutCounter++;
        if(fccTimeOutCounter > 65){
            /* Timeout set to approximately 2ms (user-customizable) */
            break;
        }
    }

    /* get measA= SYSPLLCLK2X freq wrt LFOSC*/
    fFCCPllCount = DL_SYSCTL_readFCC();

    /* Measuring SYSPLL Source */
    DL_SYSCTL_configFCC(DL_SYSCTL_FCC_TRIG_TYPE_RISE_RISE,
                        DL_SYSCTL_FCC_TRIG_SOURCE_LFCLK,
                        DL_SYSCTL_FCC_CLOCK_SOURCE_SYSOSC);
    /* Get SYSPLL frequency using FCC */
    fccTimeOutCounter = 0;
    DL_SYSCTL_startFCC();
    while (DL_SYSCTL_isFCCDone() == 0) {
        delay_cycles(977);  /* 1x LFCLK cycle = 32MHz/32.768kHz = 977, 30.5us */
        fccTimeOutCounter++;
        if(fccTimeOutCounter > 65){
            /* Timeout set to approximately 2ms (user-customizable) */
            break;
        }
    }

    /* get measB= SYSOSC freq wrt LFOSC*/
    fFCCSysoscCount = DL_SYSCTL_readFCC();

    /* Get ratio of both measurements*/
    fFCCRatio = (fFCCPllCount * FLOAT_TO_INT_SCALE) / fFCCSysoscCount;
    /* Check ratio is within bounds*/
    if ((FCC_LOWER_BOUND <  fFCCRatio) && (fFCCRatio < FCC_UPPER_BOUND))
    {
        /* ratio is good for proceeding into application code. */
        fFCCRatioStatus = true;
    }

    return fFCCRatioStatus;
}
SYSCONFIG_WEAK void SYSCFG_DL_SYSCTL_init(void)
{

	//Low Power Mode is configured to be SLEEP0
    DL_SYSCTL_setBORThreshold(DL_SYSCTL_BOR_THRESHOLD_LEVEL_0);
    DL_SYSCTL_setFlashWaitState(DL_SYSCTL_FLASH_WAIT_STATE_2);

    
	DL_SYSCTL_setSYSOSCFreq(DL_SYSCTL_SYSOSC_FREQ_BASE);
	/* Set default configuration */
	DL_SYSCTL_disableHFXT();
	DL_SYSCTL_disableSYSPLL();
    DL_SYSCTL_configSYSPLL((DL_SYSCTL_SYSPLLConfig *) &gSYSPLLConfig);

    /*
     * [SYSPLL_ERR_01]
     * PLL Incorrect locking WA start.
     * Insert after every PLL enable.
     * This can lead an infinite loop if the condition persists
     * and can block entry to the application code.
     */

    while (SYSCFG_DL_SYSCTL_SYSPLL_init() == false)
    {
        /* Toggle SYSPLL enable to re-enable SYSPLL and re-check incorrect locking */
        DL_SYSCTL_disableSYSPLL();
        DL_SYSCTL_enableSYSPLL();

        /* Wait until SYSPLL startup is stabilized*/
        while ((DL_SYSCTL_getClockStatus() & SYSCTL_CLKSTATUS_SYSPLLGOOD_MASK) != DL_SYSCTL_CLK_STATUS_SYSPLL_GOOD){}
    }
    DL_SYSCTL_setULPCLKDivider(DL_SYSCTL_ULPCLK_DIV_2);
    DL_SYSCTL_setMCLKSource(SYSOSC, HSCLK, DL_SYSCTL_HSCLK_SOURCE_SYSPLL);

}


/*
 * Timer clock configuration to be sourced by  / 1 (80000000 Hz)
 * timerClkFreq = (timerClkSrc / (timerClkDivRatio * (timerClkPrescale + 1)))
 *   80000000 Hz = 80000000 Hz / (1 * (0 + 1))
 */
static const DL_TimerA_ClockConfig gPWM_DRIVE_LEFTClockConfig = {
    .clockSel = DL_TIMER_CLOCK_BUSCLK,
    .divideRatio = DL_TIMER_CLOCK_DIVIDE_1,
    .prescale = 0U
};

static const DL_TimerA_PWMConfig gPWM_DRIVE_LEFTConfig = {
    .pwmMode = DL_TIMER_PWM_MODE_EDGE_ALIGN_UP,
    .period = 7999,
    .isTimerWithFourCC = true,
    .startTimer = DL_TIMER_STOP,
};

SYSCONFIG_WEAK void SYSCFG_DL_PWM_DRIVE_LEFT_init(void) {

    DL_TimerA_setClockConfig(
        PWM_DRIVE_LEFT_INST, (DL_TimerA_ClockConfig *) &gPWM_DRIVE_LEFTClockConfig);

    DL_TimerA_initPWMMode(
        PWM_DRIVE_LEFT_INST, (DL_TimerA_PWMConfig *) &gPWM_DRIVE_LEFTConfig);

    // Set Counter control to the smallest CC index being used
    DL_TimerA_setCounterControl(PWM_DRIVE_LEFT_INST,DL_TIMER_CZC_CCCTL1_ZCOND,DL_TIMER_CAC_CCCTL1_ACOND,DL_TIMER_CLC_CCCTL1_LCOND);

    DL_TimerA_setCaptureCompareOutCtl(PWM_DRIVE_LEFT_INST, DL_TIMER_CC_OCTL_INIT_VAL_LOW,
		DL_TIMER_CC_OCTL_INV_OUT_DISABLED, DL_TIMER_CC_OCTL_SRC_FUNCVAL,
		DL_TIMERA_CAPTURE_COMPARE_1_INDEX);

    DL_TimerA_setCaptCompUpdateMethod(PWM_DRIVE_LEFT_INST, DL_TIMER_CC_UPDATE_METHOD_IMMEDIATE, DL_TIMERA_CAPTURE_COMPARE_1_INDEX);
    DL_TimerA_setCaptureCompareValue(PWM_DRIVE_LEFT_INST, 0, DL_TIMER_CC_1_INDEX);

    DL_TimerA_enableClock(PWM_DRIVE_LEFT_INST);


    
    DL_TimerA_setCCPDirection(PWM_DRIVE_LEFT_INST , DL_TIMER_CC1_OUTPUT );


}
/*
 * Timer clock configuration to be sourced by  / 1 (80000000 Hz)
 * timerClkFreq = (timerClkSrc / (timerClkDivRatio * (timerClkPrescale + 1)))
 *   80000000 Hz = 80000000 Hz / (1 * (0 + 1))
 */
static const DL_TimerA_ClockConfig gPWM_DRIVE_RIGHTClockConfig = {
    .clockSel = DL_TIMER_CLOCK_BUSCLK,
    .divideRatio = DL_TIMER_CLOCK_DIVIDE_1,
    .prescale = 0U
};

static const DL_TimerA_PWMConfig gPWM_DRIVE_RIGHTConfig = {
    .pwmMode = DL_TIMER_PWM_MODE_EDGE_ALIGN_UP,
    .period = 7999,
    .isTimerWithFourCC = false,
    .startTimer = DL_TIMER_STOP,
};

SYSCONFIG_WEAK void SYSCFG_DL_PWM_DRIVE_RIGHT_init(void) {

    DL_TimerA_setClockConfig(
        PWM_DRIVE_RIGHT_INST, (DL_TimerA_ClockConfig *) &gPWM_DRIVE_RIGHTClockConfig);

    DL_TimerA_initPWMMode(
        PWM_DRIVE_RIGHT_INST, (DL_TimerA_PWMConfig *) &gPWM_DRIVE_RIGHTConfig);

    // Set Counter control to the smallest CC index being used
    DL_TimerA_setCounterControl(PWM_DRIVE_RIGHT_INST,DL_TIMER_CZC_CCCTL0_ZCOND,DL_TIMER_CAC_CCCTL0_ACOND,DL_TIMER_CLC_CCCTL0_LCOND);

    DL_TimerA_setCaptureCompareOutCtl(PWM_DRIVE_RIGHT_INST, DL_TIMER_CC_OCTL_INIT_VAL_LOW,
		DL_TIMER_CC_OCTL_INV_OUT_DISABLED, DL_TIMER_CC_OCTL_SRC_FUNCVAL,
		DL_TIMERA_CAPTURE_COMPARE_0_INDEX);

    DL_TimerA_setCaptCompUpdateMethod(PWM_DRIVE_RIGHT_INST, DL_TIMER_CC_UPDATE_METHOD_IMMEDIATE, DL_TIMERA_CAPTURE_COMPARE_0_INDEX);
    DL_TimerA_setCaptureCompareValue(PWM_DRIVE_RIGHT_INST, 0, DL_TIMER_CC_0_INDEX);

    DL_TimerA_enableClock(PWM_DRIVE_RIGHT_INST);


    
    DL_TimerA_setCCPDirection(PWM_DRIVE_RIGHT_INST , DL_TIMER_CC0_OUTPUT );


}
/*
 * Timer clock configuration to be sourced by  / 8 (10000000 Hz)
 * timerClkFreq = (timerClkSrc / (timerClkDivRatio * (timerClkPrescale + 1)))
 *   2500000 Hz = 10000000 Hz / (8 * (3 + 1))
 */
static const DL_TimerG_ClockConfig gPWM_SERVO_1ClockConfig = {
    .clockSel = DL_TIMER_CLOCK_BUSCLK,
    .divideRatio = DL_TIMER_CLOCK_DIVIDE_8,
    .prescale = 3U
};

static const DL_TimerG_PWMConfig gPWM_SERVO_1Config = {
    .pwmMode = DL_TIMER_PWM_MODE_EDGE_ALIGN_UP,
    .period = 49999,
    .isTimerWithFourCC = true,
    .startTimer = DL_TIMER_STOP,
};

SYSCONFIG_WEAK void SYSCFG_DL_PWM_SERVO_1_init(void) {

    DL_TimerG_setClockConfig(
        PWM_SERVO_1_INST, (DL_TimerG_ClockConfig *) &gPWM_SERVO_1ClockConfig);

    DL_TimerG_initPWMMode(
        PWM_SERVO_1_INST, (DL_TimerG_PWMConfig *) &gPWM_SERVO_1Config);

    // Set Counter control to the smallest CC index being used
    DL_TimerG_setCounterControl(PWM_SERVO_1_INST,DL_TIMER_CZC_CCCTL1_ZCOND,DL_TIMER_CAC_CCCTL1_ACOND,DL_TIMER_CLC_CCCTL1_LCOND);

    DL_TimerG_setCaptureCompareOutCtl(PWM_SERVO_1_INST, DL_TIMER_CC_OCTL_INIT_VAL_LOW,
		DL_TIMER_CC_OCTL_INV_OUT_DISABLED, DL_TIMER_CC_OCTL_SRC_FUNCVAL,
		DL_TIMERG_CAPTURE_COMPARE_1_INDEX);

    DL_TimerG_setCaptCompUpdateMethod(PWM_SERVO_1_INST, DL_TIMER_CC_UPDATE_METHOD_IMMEDIATE, DL_TIMERG_CAPTURE_COMPARE_1_INDEX);
    DL_TimerG_setCaptureCompareValue(PWM_SERVO_1_INST, 0, DL_TIMER_CC_1_INDEX);

    DL_TimerG_enableClock(PWM_SERVO_1_INST);


    
    DL_TimerG_setCCPDirection(PWM_SERVO_1_INST , DL_TIMER_CC1_OUTPUT );


}
/*
 * Timer clock configuration to be sourced by  / 8 (5000000 Hz)
 * timerClkFreq = (timerClkSrc / (timerClkDivRatio * (timerClkPrescale + 1)))
 *   2500000 Hz = 5000000 Hz / (8 * (1 + 1))
 */
static const DL_TimerG_ClockConfig gPWM_SERVO_2ClockConfig = {
    .clockSel = DL_TIMER_CLOCK_BUSCLK,
    .divideRatio = DL_TIMER_CLOCK_DIVIDE_8,
    .prescale = 1U
};

static const DL_TimerG_PWMConfig gPWM_SERVO_2Config = {
    .pwmMode = DL_TIMER_PWM_MODE_EDGE_ALIGN_UP,
    .period = 49999,
    .isTimerWithFourCC = true,
    .startTimer = DL_TIMER_STOP,
};

SYSCONFIG_WEAK void SYSCFG_DL_PWM_SERVO_2_init(void) {

    DL_TimerG_setClockConfig(
        PWM_SERVO_2_INST, (DL_TimerG_ClockConfig *) &gPWM_SERVO_2ClockConfig);

    DL_TimerG_initPWMMode(
        PWM_SERVO_2_INST, (DL_TimerG_PWMConfig *) &gPWM_SERVO_2Config);

    // Set Counter control to the smallest CC index being used
    DL_TimerG_setCounterControl(PWM_SERVO_2_INST,DL_TIMER_CZC_CCCTL1_ZCOND,DL_TIMER_CAC_CCCTL1_ACOND,DL_TIMER_CLC_CCCTL1_LCOND);

    DL_TimerG_setCaptureCompareOutCtl(PWM_SERVO_2_INST, DL_TIMER_CC_OCTL_INIT_VAL_LOW,
		DL_TIMER_CC_OCTL_INV_OUT_DISABLED, DL_TIMER_CC_OCTL_SRC_FUNCVAL,
		DL_TIMERG_CAPTURE_COMPARE_1_INDEX);

    DL_TimerG_setCaptCompUpdateMethod(PWM_SERVO_2_INST, DL_TIMER_CC_UPDATE_METHOD_IMMEDIATE, DL_TIMERG_CAPTURE_COMPARE_1_INDEX);
    DL_TimerG_setCaptureCompareValue(PWM_SERVO_2_INST, 0, DL_TIMER_CC_1_INDEX);

    DL_TimerG_enableClock(PWM_SERVO_2_INST);


    
    DL_TimerG_setCCPDirection(PWM_SERVO_2_INST , DL_TIMER_CC1_OUTPUT );


}


static const DL_TimerG_ClockConfig gQEI_RIGHTClockConfig = {
    .clockSel = DL_TIMER_CLOCK_BUSCLK,
    .divideRatio = DL_TIMER_CLOCK_DIVIDE_1,
    .prescale = 0U
};


SYSCONFIG_WEAK void SYSCFG_DL_QEI_RIGHT_init(void) {

    DL_TimerG_setClockConfig(
        QEI_RIGHT_INST, (DL_TimerG_ClockConfig *) &gQEI_RIGHTClockConfig);

    DL_TimerG_configQEI(QEI_RIGHT_INST, DL_TIMER_QEI_MODE_2_INPUT,
        DL_TIMER_CC_INPUT_INV_NOINVERT, DL_TIMER_CC_0_INDEX);
    DL_TimerG_configQEI(QEI_RIGHT_INST, DL_TIMER_QEI_MODE_2_INPUT,
        DL_TIMER_CC_INPUT_INV_NOINVERT, DL_TIMER_CC_1_INDEX);
    DL_TimerG_setLoadValue(QEI_RIGHT_INST, 65535);
    DL_TimerG_enableClock(QEI_RIGHT_INST);
}
static const DL_TimerG_ClockConfig gQEI_LEFTClockConfig = {
    .clockSel = DL_TIMER_CLOCK_BUSCLK,
    .divideRatio = DL_TIMER_CLOCK_DIVIDE_1,
    .prescale = 0U
};


SYSCONFIG_WEAK void SYSCFG_DL_QEI_LEFT_init(void) {

    DL_TimerG_setClockConfig(
        QEI_LEFT_INST, (DL_TimerG_ClockConfig *) &gQEI_LEFTClockConfig);

    DL_TimerG_configQEI(QEI_LEFT_INST, DL_TIMER_QEI_MODE_2_INPUT,
        DL_TIMER_CC_INPUT_INV_NOINVERT, DL_TIMER_CC_0_INDEX);
    DL_TimerG_configQEI(QEI_LEFT_INST, DL_TIMER_QEI_MODE_2_INPUT,
        DL_TIMER_CC_INPUT_INV_NOINVERT, DL_TIMER_CC_1_INDEX);
    DL_TimerG_setLoadValue(QEI_LEFT_INST, 65535);
    DL_TimerG_enableClock(QEI_LEFT_INST);
}


static const DL_I2C_ClockConfig gI2C_AUXClockConfig = {
    .clockSel = DL_I2C_CLOCK_BUSCLK,
    .divideRatio = DL_I2C_CLOCK_DIVIDE_1,
};

SYSCONFIG_WEAK void SYSCFG_DL_I2C_AUX_init(void) {

    DL_I2C_setClockConfig(I2C_AUX_INST,
        (DL_I2C_ClockConfig *) &gI2C_AUXClockConfig);
    DL_I2C_disableAnalogGlitchFilter(I2C_AUX_INST);

    /* Configure Controller Mode */
    DL_I2C_resetControllerTransfer(I2C_AUX_INST);
    /* Set frequency to 400000 Hz*/
    DL_I2C_setTimerPeriod(I2C_AUX_INST, 9);
    DL_I2C_setControllerTXFIFOThreshold(I2C_AUX_INST, DL_I2C_TX_FIFO_LEVEL_EMPTY);
    DL_I2C_setControllerRXFIFOThreshold(I2C_AUX_INST, DL_I2C_RX_FIFO_LEVEL_BYTES_1);
    DL_I2C_enableControllerClockStretching(I2C_AUX_INST);


    /* Enable module */
    DL_I2C_enableController(I2C_AUX_INST);


}

static const DL_UART_Main_ClockConfig gUART_STEPPER_BUSClockConfig = {
    .clockSel    = DL_UART_MAIN_CLOCK_BUSCLK,
    .divideRatio = DL_UART_MAIN_CLOCK_DIVIDE_RATIO_1
};

static const DL_UART_Main_Config gUART_STEPPER_BUSConfig = {
    .mode        = DL_UART_MAIN_MODE_NORMAL,
    .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
    .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
    .parity      = DL_UART_MAIN_PARITY_NONE,
    .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
    .stopBits    = DL_UART_MAIN_STOP_BITS_ONE
};

SYSCONFIG_WEAK void SYSCFG_DL_UART_STEPPER_BUS_init(void)
{
    DL_UART_Main_setClockConfig(UART_STEPPER_BUS_INST, (DL_UART_Main_ClockConfig *) &gUART_STEPPER_BUSClockConfig);

    DL_UART_Main_init(UART_STEPPER_BUS_INST, (DL_UART_Main_Config *) &gUART_STEPPER_BUSConfig);
    /*
     * Configure baud rate by setting oversampling and baud rate divisors.
     *  Target baud rate: 256000
     *  Actual baud rate: 256000
     */
    DL_UART_Main_setOversampling(UART_STEPPER_BUS_INST, DL_UART_OVERSAMPLING_RATE_16X);
    DL_UART_Main_setBaudRateDivisor(UART_STEPPER_BUS_INST, UART_STEPPER_BUS_IBRD_40_MHZ_256000_BAUD, UART_STEPPER_BUS_FBRD_40_MHZ_256000_BAUD);


    /* Configure Interrupts */
    DL_UART_Main_enableInterrupt(UART_STEPPER_BUS_INST,
                                 DL_UART_MAIN_INTERRUPT_DMA_DONE_RX);

    /* Configure DMA Receive Event */
    DL_UART_Main_enableDMAReceiveEvent(UART_STEPPER_BUS_INST, DL_UART_DMA_INTERRUPT_RX);
    /* Configure DMA Transmit Event */
    DL_UART_Main_enableDMATransmitEvent(UART_STEPPER_BUS_INST);
    /* Configure FIFOs */
    DL_UART_Main_enableFIFOs(UART_STEPPER_BUS_INST);
    DL_UART_Main_setRXFIFOThreshold(UART_STEPPER_BUS_INST, DL_UART_RX_FIFO_LEVEL_1_2_FULL);
    DL_UART_Main_setTXFIFOThreshold(UART_STEPPER_BUS_INST, DL_UART_TX_FIFO_LEVEL_1_2_EMPTY);

    DL_UART_Main_enable(UART_STEPPER_BUS_INST);
}
static const DL_UART_Main_ClockConfig gUART_HOST_LINKClockConfig = {
    .clockSel    = DL_UART_MAIN_CLOCK_BUSCLK,
    .divideRatio = DL_UART_MAIN_CLOCK_DIVIDE_RATIO_1
};

static const DL_UART_Main_Config gUART_HOST_LINKConfig = {
    .mode        = DL_UART_MAIN_MODE_NORMAL,
    .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
    .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
    .parity      = DL_UART_MAIN_PARITY_NONE,
    .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
    .stopBits    = DL_UART_MAIN_STOP_BITS_ONE
};

SYSCONFIG_WEAK void SYSCFG_DL_UART_HOST_LINK_init(void)
{
    DL_UART_Main_setClockConfig(UART_HOST_LINK_INST, (DL_UART_Main_ClockConfig *) &gUART_HOST_LINKClockConfig);

    DL_UART_Main_init(UART_HOST_LINK_INST, (DL_UART_Main_Config *) &gUART_HOST_LINKConfig);
    /*
     * Configure baud rate by setting oversampling and baud rate divisors.
     *  Target baud rate: 230400
     *  Actual baud rate: 230381.57
     */
    DL_UART_Main_setOversampling(UART_HOST_LINK_INST, DL_UART_OVERSAMPLING_RATE_16X);
    DL_UART_Main_setBaudRateDivisor(UART_HOST_LINK_INST, UART_HOST_LINK_IBRD_80_MHZ_230400_BAUD, UART_HOST_LINK_FBRD_80_MHZ_230400_BAUD);


    /* Configure Interrupts */
    DL_UART_Main_enableInterrupt(UART_HOST_LINK_INST,
                                 DL_UART_MAIN_INTERRUPT_DMA_DONE_RX |
                                 DL_UART_MAIN_INTERRUPT_DMA_DONE_TX);

    /* Configure DMA Receive Event */
    DL_UART_Main_enableDMAReceiveEvent(UART_HOST_LINK_INST, DL_UART_DMA_INTERRUPT_RX);
    /* Configure DMA Transmit Event */
    DL_UART_Main_enableDMATransmitEvent(UART_HOST_LINK_INST);
    /* Configure FIFOs */
    DL_UART_Main_enableFIFOs(UART_HOST_LINK_INST);
    DL_UART_Main_setRXFIFOThreshold(UART_HOST_LINK_INST, DL_UART_RX_FIFO_LEVEL_1_2_FULL);
    DL_UART_Main_setTXFIFOThreshold(UART_HOST_LINK_INST, DL_UART_TX_FIFO_LEVEL_1_2_EMPTY);

    DL_UART_Main_enable(UART_HOST_LINK_INST);
}
static const DL_UART_Main_ClockConfig gUART_VISIONClockConfig = {
    .clockSel    = DL_UART_MAIN_CLOCK_BUSCLK,
    .divideRatio = DL_UART_MAIN_CLOCK_DIVIDE_RATIO_1
};

static const DL_UART_Main_Config gUART_VISIONConfig = {
    .mode        = DL_UART_MAIN_MODE_NORMAL,
    .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
    .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
    .parity      = DL_UART_MAIN_PARITY_NONE,
    .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
    .stopBits    = DL_UART_MAIN_STOP_BITS_ONE
};

SYSCONFIG_WEAK void SYSCFG_DL_UART_VISION_init(void)
{
    DL_UART_Main_setClockConfig(UART_VISION_INST, (DL_UART_Main_ClockConfig *) &gUART_VISIONClockConfig);

    DL_UART_Main_init(UART_VISION_INST, (DL_UART_Main_Config *) &gUART_VISIONConfig);
    /*
     * Configure baud rate by setting oversampling and baud rate divisors.
     *  Target baud rate: 230400
     *  Actual baud rate: 230547.55
     */
    DL_UART_Main_setOversampling(UART_VISION_INST, DL_UART_OVERSAMPLING_RATE_16X);
    DL_UART_Main_setBaudRateDivisor(UART_VISION_INST, UART_VISION_IBRD_40_MHZ_230400_BAUD, UART_VISION_FBRD_40_MHZ_230400_BAUD);


    /* Configure Interrupts */
    DL_UART_Main_enableInterrupt(UART_VISION_INST,
                                 DL_UART_MAIN_INTERRUPT_DMA_DONE_RX |
                                 DL_UART_MAIN_INTERRUPT_DMA_DONE_TX);

    /* Configure DMA Receive Event */
    DL_UART_Main_enableDMAReceiveEvent(UART_VISION_INST, DL_UART_DMA_INTERRUPT_RX);
    /* Configure DMA Transmit Event */
    DL_UART_Main_enableDMATransmitEvent(UART_VISION_INST);
    /* Configure FIFOs */
    DL_UART_Main_enableFIFOs(UART_VISION_INST);
    DL_UART_Main_setRXFIFOThreshold(UART_VISION_INST, DL_UART_RX_FIFO_LEVEL_1_2_FULL);
    DL_UART_Main_setTXFIFOThreshold(UART_VISION_INST, DL_UART_TX_FIFO_LEVEL_1_2_EMPTY);

    DL_UART_Main_enable(UART_VISION_INST);
}
static const DL_UART_Main_ClockConfig gUART_IMUClockConfig = {
    .clockSel    = DL_UART_MAIN_CLOCK_BUSCLK,
    .divideRatio = DL_UART_MAIN_CLOCK_DIVIDE_RATIO_1
};

static const DL_UART_Main_Config gUART_IMUConfig = {
    .mode        = DL_UART_MAIN_MODE_NORMAL,
    .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
    .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
    .parity      = DL_UART_MAIN_PARITY_NONE,
    .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
    .stopBits    = DL_UART_MAIN_STOP_BITS_ONE
};

SYSCONFIG_WEAK void SYSCFG_DL_UART_IMU_init(void)
{
    DL_UART_Main_setClockConfig(UART_IMU_INST, (DL_UART_Main_ClockConfig *) &gUART_IMUClockConfig);

    DL_UART_Main_init(UART_IMU_INST, (DL_UART_Main_Config *) &gUART_IMUConfig);
    /*
     * Configure baud rate by setting oversampling and baud rate divisors.
     *  Target baud rate: 230400
     *  Actual baud rate: 230381.57
     */
    DL_UART_Main_setOversampling(UART_IMU_INST, DL_UART_OVERSAMPLING_RATE_16X);
    DL_UART_Main_setBaudRateDivisor(UART_IMU_INST, UART_IMU_IBRD_80_MHZ_230400_BAUD, UART_IMU_FBRD_80_MHZ_230400_BAUD);


    /* Configure Interrupts */
    DL_UART_Main_enableInterrupt(UART_IMU_INST,
                                 DL_UART_MAIN_INTERRUPT_RX);

    /* Configure FIFOs */
    DL_UART_Main_enableFIFOs(UART_IMU_INST);
    DL_UART_Main_setRXFIFOThreshold(UART_IMU_INST, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
    DL_UART_Main_setTXFIFOThreshold(UART_IMU_INST, DL_UART_TX_FIFO_LEVEL_1_4_EMPTY);

    DL_UART_Main_enable(UART_IMU_INST);
}
static const DL_UART_Main_ClockConfig gUART_BSL_ENTRYClockConfig = {
    .clockSel    = DL_UART_MAIN_CLOCK_BUSCLK,
    .divideRatio = DL_UART_MAIN_CLOCK_DIVIDE_RATIO_1
};

static const DL_UART_Main_Config gUART_BSL_ENTRYConfig = {
    .mode        = DL_UART_MAIN_MODE_NORMAL,
    .direction   = DL_UART_MAIN_DIRECTION_TX_RX,
    .flowControl = DL_UART_MAIN_FLOW_CONTROL_NONE,
    .parity      = DL_UART_MAIN_PARITY_NONE,
    .wordLength  = DL_UART_MAIN_WORD_LENGTH_8_BITS,
    .stopBits    = DL_UART_MAIN_STOP_BITS_ONE
};

SYSCONFIG_WEAK void SYSCFG_DL_UART_BSL_ENTRY_init(void)
{
    DL_UART_Main_setClockConfig(UART_BSL_ENTRY_INST, (DL_UART_Main_ClockConfig *) &gUART_BSL_ENTRYClockConfig);

    DL_UART_Main_init(UART_BSL_ENTRY_INST, (DL_UART_Main_Config *) &gUART_BSL_ENTRYConfig);
    /*
     * Configure baud rate by setting oversampling and baud rate divisors.
     *  Target baud rate: 9600
     *  Actual baud rate: 9599.81
     */
    DL_UART_Main_setOversampling(UART_BSL_ENTRY_INST, DL_UART_OVERSAMPLING_RATE_16X);
    DL_UART_Main_setBaudRateDivisor(UART_BSL_ENTRY_INST, UART_BSL_ENTRY_IBRD_40_MHZ_9600_BAUD, UART_BSL_ENTRY_FBRD_40_MHZ_9600_BAUD);


    /* Configure Interrupts */
    DL_UART_Main_enableInterrupt(UART_BSL_ENTRY_INST,
                                 DL_UART_MAIN_INTERRUPT_RX);

    /* Configure FIFOs */
    DL_UART_Main_enableFIFOs(UART_BSL_ENTRY_INST);
    DL_UART_Main_setRXFIFOThreshold(UART_BSL_ENTRY_INST, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
    DL_UART_Main_setTXFIFOThreshold(UART_BSL_ENTRY_INST, DL_UART_TX_FIFO_LEVEL_1_2_EMPTY);

    DL_UART_Main_enable(UART_BSL_ENTRY_INST);
}

static const DL_DMA_Config gDMA_CH4Config = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_UNCHANGED,
    .srcIncrement   = DL_DMA_ADDR_UNCHANGED,
    .destWidth      = DL_DMA_WIDTH_WORD,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = DMA_CH4_TRIGGER_SEL_SW,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DMA_CH4_init(void)
{
    DL_DMA_initChannel(DMA, DMA_CH4_CHAN_ID , (DL_DMA_Config *) &gDMA_CH4Config);
}
static const DL_DMA_Config gDMA_CH0Config = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_INCREMENT,
    .srcIncrement   = DL_DMA_ADDR_UNCHANGED,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = UART_STEPPER_BUS_INST_DMA_TRIGGER_0,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DMA_CH0_init(void)
{
    DL_DMA_clearInterruptStatus(DMA, DL_DMA_INTERRUPT_CHANNEL0);
    DL_DMA_enableInterrupt(DMA, DL_DMA_INTERRUPT_CHANNEL0);
    DL_DMA_initChannel(DMA, DMA_CH0_CHAN_ID , (DL_DMA_Config *) &gDMA_CH0Config);
}
static const DL_DMA_Config gDMA_CH3Config = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_UNCHANGED,
    .srcIncrement   = DL_DMA_ADDR_INCREMENT,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = UART_STEPPER_BUS_INST_DMA_TRIGGER_1,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DMA_CH3_init(void)
{
    DL_DMA_clearInterruptStatus(DMA, DL_DMA_INTERRUPT_CHANNEL3);
    DL_DMA_enableInterrupt(DMA, DL_DMA_INTERRUPT_CHANNEL3);
    DL_DMA_initChannel(DMA, DMA_CH3_CHAN_ID , (DL_DMA_Config *) &gDMA_CH3Config);
}
static const DL_DMA_Config gDMA_CH1Config = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_UNCHANGED,
    .srcIncrement   = DL_DMA_ADDR_INCREMENT,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = UART_HOST_LINK_INST_DMA_TRIGGER_0,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DMA_CH1_init(void)
{
    DL_DMA_clearInterruptStatus(DMA, DL_DMA_INTERRUPT_CHANNEL4);
    DL_DMA_enableInterrupt(DMA, DL_DMA_INTERRUPT_CHANNEL4);
    DL_DMA_initChannel(DMA, DMA_CH1_CHAN_ID , (DL_DMA_Config *) &gDMA_CH1Config);
}
static const DL_DMA_Config gDMA_CH2Config = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_INCREMENT,
    .srcIncrement   = DL_DMA_ADDR_UNCHANGED,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = UART_HOST_LINK_INST_DMA_TRIGGER_1,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DMA_CH2_init(void)
{
    DL_DMA_clearInterruptStatus(DMA, DL_DMA_INTERRUPT_CHANNEL2);
    DL_DMA_enableInterrupt(DMA, DL_DMA_INTERRUPT_CHANNEL2);
    DL_DMA_initChannel(DMA, DMA_CH2_CHAN_ID , (DL_DMA_Config *) &gDMA_CH2Config);
}
static const DL_DMA_Config gDMA_CH8Config = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_UNCHANGED,
    .srcIncrement   = DL_DMA_ADDR_INCREMENT,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = UART_VISION_INST_DMA_TRIGGER_0,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DMA_CH8_init(void)
{
    DL_DMA_clearInterruptStatus(DMA, DL_DMA_INTERRUPT_CHANNEL5);
    DL_DMA_enableInterrupt(DMA, DL_DMA_INTERRUPT_CHANNEL5);
    DL_DMA_initChannel(DMA, DMA_CH8_CHAN_ID , (DL_DMA_Config *) &gDMA_CH8Config);
}
static const DL_DMA_Config gDMA_CH7Config = {
    .transferMode   = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode   = DL_DMA_NORMAL_MODE,
    .destIncrement  = DL_DMA_ADDR_INCREMENT,
    .srcIncrement   = DL_DMA_ADDR_UNCHANGED,
    .destWidth      = DL_DMA_WIDTH_BYTE,
    .srcWidth       = DL_DMA_WIDTH_BYTE,
    .trigger        = UART_VISION_INST_DMA_TRIGGER_1,
    .triggerType    = DL_DMA_TRIGGER_TYPE_EXTERNAL,
};

SYSCONFIG_WEAK void SYSCFG_DL_DMA_CH7_init(void)
{
    DL_DMA_clearInterruptStatus(DMA, DL_DMA_INTERRUPT_CHANNEL1);
    DL_DMA_enableInterrupt(DMA, DL_DMA_INTERRUPT_CHANNEL1);
    DL_DMA_initChannel(DMA, DMA_CH7_CHAN_ID , (DL_DMA_Config *) &gDMA_CH7Config);
}
SYSCONFIG_WEAK void SYSCFG_DL_DMA_init(void){
    SYSCFG_DL_DMA_CH4_init();
    SYSCFG_DL_DMA_CH0_init();
    SYSCFG_DL_DMA_CH3_init();
    SYSCFG_DL_DMA_CH1_init();
    SYSCFG_DL_DMA_CH2_init();
    SYSCFG_DL_DMA_CH8_init();
    SYSCFG_DL_DMA_CH7_init();
}


