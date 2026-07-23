/*
 * Copyright (c) 2023, Texas Instruments Incorporated - http://www.ti.com
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
 *  ============ ti_msp_dl_config.h =============
 *  Configured MSPM0 DriverLib module declarations
 *
 *  DO NOT EDIT - This file is generated for the MSPM0G350X
 *  by the SysConfig tool.
 */
#ifndef ti_msp_dl_config_h
#define ti_msp_dl_config_h

#define CONFIG_MSPM0G350X
#define CONFIG_MSPM0G3507

#if defined(__ti_version__) || defined(__TI_COMPILER_VERSION__)
#define SYSCONFIG_WEAK __attribute__((weak))
#elif defined(__IAR_SYSTEMS_ICC__)
#define SYSCONFIG_WEAK __weak
#elif defined(__GNUC__)
#define SYSCONFIG_WEAK __attribute__((weak))
#endif

#include <ti/devices/msp/msp.h>
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/m0p/dl_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  ======== SYSCFG_DL_init ========
 *  Perform all required MSP DL initialization
 *
 *  This function should be called once at a point before any use of
 *  MSP DL.
 */


/* clang-format off */

#define POWER_STARTUP_DELAY                                                (16)



#define CPUCLK_FREQ                                                     80000000
/* Defines for SYSPLL_ERR_01 Workaround */
/* Represent 1.000 as 1000 */
#define FLOAT_TO_INT_SCALE                                               (1000U)
#define FCC_EXPECTED_RATIO                                                  2500
#define FCC_UPPER_BOUND                       (FCC_EXPECTED_RATIO * (1 + 0.003))
#define FCC_LOWER_BOUND                       (FCC_EXPECTED_RATIO * (1 - 0.003))

bool SYSCFG_DL_SYSCTL_SYSPLL_init(void);


/* Defines for PWM_DRIVE_LEFT */
#define PWM_DRIVE_LEFT_INST                                                TIMA0
#define PWM_DRIVE_LEFT_INST_IRQHandler                          TIMA0_IRQHandler
#define PWM_DRIVE_LEFT_INST_INT_IRQN                            (TIMA0_INT_IRQn)
#define PWM_DRIVE_LEFT_INST_CLK_FREQ                                    80000000
/* GPIO defines for channel 1 */
#define GPIO_PWM_DRIVE_LEFT_C1_PORT                                        GPIOA
#define GPIO_PWM_DRIVE_LEFT_C1_PIN                                DL_GPIO_PIN_22
#define GPIO_PWM_DRIVE_LEFT_C1_IOMUX                             (IOMUX_PINCM47)
#define GPIO_PWM_DRIVE_LEFT_C1_IOMUX_FUNC             IOMUX_PINCM47_PF_TIMA0_CCP1
#define GPIO_PWM_DRIVE_LEFT_C1_IDX                           DL_TIMER_CC_1_INDEX

/* Defines for PWM_DRIVE_RIGHT */
#define PWM_DRIVE_RIGHT_INST                                               TIMA1
#define PWM_DRIVE_RIGHT_INST_IRQHandler                         TIMA1_IRQHandler
#define PWM_DRIVE_RIGHT_INST_INT_IRQN                           (TIMA1_INT_IRQn)
#define PWM_DRIVE_RIGHT_INST_CLK_FREQ                                   80000000
/* GPIO defines for channel 0 */
#define GPIO_PWM_DRIVE_RIGHT_C0_PORT                                       GPIOB
#define GPIO_PWM_DRIVE_RIGHT_C0_PIN                                DL_GPIO_PIN_2
#define GPIO_PWM_DRIVE_RIGHT_C0_IOMUX                            (IOMUX_PINCM15)
#define GPIO_PWM_DRIVE_RIGHT_C0_IOMUX_FUNC             IOMUX_PINCM15_PF_TIMA1_CCP0
#define GPIO_PWM_DRIVE_RIGHT_C0_IDX                          DL_TIMER_CC_0_INDEX




/* Defines for I2C_AUX */
#define I2C_AUX_INST                                                        I2C1
#define I2C_AUX_INST_IRQHandler                                  I2C1_IRQHandler
#define I2C_AUX_INST_INT_IRQN                                      I2C1_INT_IRQn
#define I2C_AUX_BUS_SPEED_HZ                                              400000
#define GPIO_I2C_AUX_SDA_PORT                                              GPIOA
#define GPIO_I2C_AUX_SDA_PIN                                      DL_GPIO_PIN_30
#define GPIO_I2C_AUX_IOMUX_SDA                                    (IOMUX_PINCM5)
#define GPIO_I2C_AUX_IOMUX_SDA_FUNC                     IOMUX_PINCM5_PF_I2C1_SDA
#define GPIO_I2C_AUX_SCL_PORT                                              GPIOA
#define GPIO_I2C_AUX_SCL_PIN                                      DL_GPIO_PIN_29
#define GPIO_I2C_AUX_IOMUX_SCL                                    (IOMUX_PINCM4)
#define GPIO_I2C_AUX_IOMUX_SCL_FUNC                     IOMUX_PINCM4_PF_I2C1_SCL

/* Defines for I2C_IMU */
#define I2C_IMU_INST                                                        I2C0
#define I2C_IMU_INST_IRQHandler                                  I2C0_IRQHandler
#define I2C_IMU_INST_INT_IRQN                                      I2C0_INT_IRQn
#define I2C_IMU_BUS_SPEED_HZ                                              400000
#define GPIO_I2C_IMU_SDA_PORT                                              GPIOA
#define GPIO_I2C_IMU_SDA_PIN                                      DL_GPIO_PIN_28
#define GPIO_I2C_IMU_IOMUX_SDA                                    (IOMUX_PINCM3)
#define GPIO_I2C_IMU_IOMUX_SDA_FUNC                     IOMUX_PINCM3_PF_I2C0_SDA
#define GPIO_I2C_IMU_SCL_PORT                                              GPIOA
#define GPIO_I2C_IMU_SCL_PIN                                      DL_GPIO_PIN_31
#define GPIO_I2C_IMU_IOMUX_SCL                                    (IOMUX_PINCM6)
#define GPIO_I2C_IMU_IOMUX_SCL_FUNC                     IOMUX_PINCM6_PF_I2C0_SCL


/* Defines for UART_STEPPER_BUS */
#define UART_STEPPER_BUS_INST                                              UART2
#define UART_STEPPER_BUS_INST_FREQUENCY                                 40000000
#define UART_STEPPER_BUS_INST_IRQHandler                        UART2_IRQHandler
#define UART_STEPPER_BUS_INST_INT_IRQN                            UART2_INT_IRQn
#define GPIO_UART_STEPPER_BUS_RX_PORT                                      GPIOB
#define GPIO_UART_STEPPER_BUS_TX_PORT                                      GPIOB
#define GPIO_UART_STEPPER_BUS_RX_PIN                              DL_GPIO_PIN_16
#define GPIO_UART_STEPPER_BUS_TX_PIN                              DL_GPIO_PIN_15
#define GPIO_UART_STEPPER_BUS_IOMUX_RX                           (IOMUX_PINCM33)
#define GPIO_UART_STEPPER_BUS_IOMUX_TX                           (IOMUX_PINCM32)
#define GPIO_UART_STEPPER_BUS_IOMUX_RX_FUNC               IOMUX_PINCM33_PF_UART2_RX
#define GPIO_UART_STEPPER_BUS_IOMUX_TX_FUNC               IOMUX_PINCM32_PF_UART2_TX
#define UART_STEPPER_BUS_BAUD_RATE                                      (230400)
#define UART_STEPPER_BUS_IBRD_40_MHZ_230400_BAUD                            (10)
#define UART_STEPPER_BUS_FBRD_40_MHZ_230400_BAUD                            (54)
/* Defines for UART_HOST_LINK */
#define UART_HOST_LINK_INST                                                UART0
#define UART_HOST_LINK_INST_FREQUENCY                                   40000000
#define UART_HOST_LINK_INST_IRQHandler                          UART0_IRQHandler
#define UART_HOST_LINK_INST_INT_IRQN                              UART0_INT_IRQn
#define GPIO_UART_HOST_LINK_RX_PORT                                        GPIOA
#define GPIO_UART_HOST_LINK_TX_PORT                                        GPIOA
#define GPIO_UART_HOST_LINK_RX_PIN                                DL_GPIO_PIN_11
#define GPIO_UART_HOST_LINK_TX_PIN                                DL_GPIO_PIN_10
#define GPIO_UART_HOST_LINK_IOMUX_RX                             (IOMUX_PINCM22)
#define GPIO_UART_HOST_LINK_IOMUX_TX                             (IOMUX_PINCM21)
#define GPIO_UART_HOST_LINK_IOMUX_RX_FUNC               IOMUX_PINCM22_PF_UART0_RX
#define GPIO_UART_HOST_LINK_IOMUX_TX_FUNC               IOMUX_PINCM21_PF_UART0_TX
#define UART_HOST_LINK_BAUD_RATE                                        (230400)
#define UART_HOST_LINK_IBRD_40_MHZ_230400_BAUD                              (10)
#define UART_HOST_LINK_FBRD_40_MHZ_230400_BAUD                              (54)
/* Defines for UART_VISION */
#define UART_VISION_INST                                                   UART1
#define UART_VISION_INST_FREQUENCY                                      40000000
#define UART_VISION_INST_IRQHandler                             UART1_IRQHandler
#define UART_VISION_INST_INT_IRQN                                 UART1_INT_IRQn
#define GPIO_UART_VISION_RX_PORT                                           GPIOA
#define GPIO_UART_VISION_TX_PORT                                           GPIOA
#define GPIO_UART_VISION_RX_PIN                                    DL_GPIO_PIN_9
#define GPIO_UART_VISION_TX_PIN                                    DL_GPIO_PIN_8
#define GPIO_UART_VISION_IOMUX_RX                                (IOMUX_PINCM20)
#define GPIO_UART_VISION_IOMUX_TX                                (IOMUX_PINCM19)
#define GPIO_UART_VISION_IOMUX_RX_FUNC                 IOMUX_PINCM20_PF_UART1_RX
#define GPIO_UART_VISION_IOMUX_TX_FUNC                 IOMUX_PINCM19_PF_UART1_TX
#define UART_VISION_BAUD_RATE                                           (230400)
#define UART_VISION_IBRD_40_MHZ_230400_BAUD                                 (10)
#define UART_VISION_FBRD_40_MHZ_230400_BAUD                                 (54)
/* Defines for UART_IMU */
#define UART_IMU_INST                                                      UART3
#define UART_IMU_INST_FREQUENCY                                         80000000
#define UART_IMU_INST_IRQHandler                                UART3_IRQHandler
#define UART_IMU_INST_INT_IRQN                                    UART3_INT_IRQn
#define GPIO_UART_IMU_RX_PORT                                              GPIOA
#define GPIO_UART_IMU_TX_PORT                                              GPIOA
#define GPIO_UART_IMU_RX_PIN                                      DL_GPIO_PIN_25
#define GPIO_UART_IMU_TX_PIN                                      DL_GPIO_PIN_26
#define GPIO_UART_IMU_IOMUX_RX                                   (IOMUX_PINCM55)
#define GPIO_UART_IMU_IOMUX_TX                                   (IOMUX_PINCM59)
#define GPIO_UART_IMU_IOMUX_RX_FUNC                    IOMUX_PINCM55_PF_UART3_RX
#define GPIO_UART_IMU_IOMUX_TX_FUNC                    IOMUX_PINCM59_PF_UART3_TX
#define UART_IMU_BAUD_RATE                                              (230400)
#define UART_IMU_IBRD_80_MHZ_230400_BAUD                                    (21)
#define UART_IMU_FBRD_80_MHZ_230400_BAUD                                    (45)





/* Defines for DMA_CH4 */
#define DMA_CH4_CHAN_ID                                                      (6)
#define DMA_CH4_TRIGGER_SEL_SW                               (DMA_SOFTWARE_TRIG)
/* Defines for DMA_CH0 */
#define DMA_CH0_CHAN_ID                                                      (0)
#define UART_STEPPER_BUS_INST_DMA_TRIGGER_0                  (DMA_UART2_RX_TRIG)
/* Defines for DMA_CH3 */
#define DMA_CH3_CHAN_ID                                                      (3)
#define UART_STEPPER_BUS_INST_DMA_TRIGGER_1                  (DMA_UART2_TX_TRIG)
/* Defines for DMA_CH1 */
#define DMA_CH1_CHAN_ID                                                      (4)
#define UART_HOST_LINK_INST_DMA_TRIGGER_0                    (DMA_UART0_TX_TRIG)
/* Defines for DMA_CH2 */
#define DMA_CH2_CHAN_ID                                                      (2)
#define UART_HOST_LINK_INST_DMA_TRIGGER_1                    (DMA_UART0_RX_TRIG)
/* Defines for DMA_CH8 */
#define DMA_CH8_CHAN_ID                                                      (5)
#define UART_VISION_INST_DMA_TRIGGER_0                       (DMA_UART1_TX_TRIG)
/* Defines for DMA_CH7 */
#define DMA_CH7_CHAN_ID                                                      (1)
#define UART_VISION_INST_DMA_TRIGGER_1                       (DMA_UART1_RX_TRIG)


/* Port definition for Pin Group GPIO_STATUS_LED */
#define GPIO_STATUS_LED_PORT                                             (GPIOB)

/* Defines for PIN_22: GPIOB.22 with pinCMx 50 on package pin 21 */
#define GPIO_STATUS_LED_PIN_22_PIN                              (DL_GPIO_PIN_22)
#define GPIO_STATUS_LED_PIN_22_IOMUX                             (IOMUX_PINCM50)
/* Defines for K1: GPIOB.4 with pinCMx 17 on package pin 52 */
#define GPIO_GRP_KEY_K1_PORT                                             (GPIOB)
// groups represented: ["GPIO_ENCODERS","GPIO_GRP_KEY"]
// pins affected: ["PIN_R_A","PIN_R_B","PIN_L_B","K1","K2","K3"]
#define GPIO_MULTIPLE_GPIOB_INT_IRQN                            (GPIOB_INT_IRQn)
#define GPIO_MULTIPLE_GPIOB_INT_IIDX            (DL_INTERRUPT_GROUP1_IIDX_GPIOB)
#define GPIO_GRP_KEY_K1_IIDX                                 (DL_GPIO_IIDX_DIO4)
#define GPIO_GRP_KEY_K1_PIN                                      (DL_GPIO_PIN_4)
#define GPIO_GRP_KEY_K1_IOMUX                                    (IOMUX_PINCM17)
/* Defines for K2: GPIOB.5 with pinCMx 18 on package pin 53 */
#define GPIO_GRP_KEY_K2_PORT                                             (GPIOB)
#define GPIO_GRP_KEY_K2_IIDX                                 (DL_GPIO_IIDX_DIO5)
#define GPIO_GRP_KEY_K2_PIN                                      (DL_GPIO_PIN_5)
#define GPIO_GRP_KEY_K2_IOMUX                                    (IOMUX_PINCM18)
/* Defines for K3: GPIOB.25 with pinCMx 56 on package pin 27 */
#define GPIO_GRP_KEY_K3_PORT                                             (GPIOB)
#define GPIO_GRP_KEY_K3_IIDX                                (DL_GPIO_IIDX_DIO25)
#define GPIO_GRP_KEY_K3_PIN                                     (DL_GPIO_PIN_25)
#define GPIO_GRP_KEY_K3_IOMUX                                    (IOMUX_PINCM56)
/* Defines for K4: GPIOA.14 with pinCMx 36 on package pin 7 */
#define GPIO_GRP_KEY_K4_PORT                                             (GPIOA)
// groups represented: ["GPIO_ENCODERS","GPIO_GRP_KEY"]
// pins affected: ["PIN_L_A","K4"]
#define GPIO_MULTIPLE_GPIOA_INT_IRQN                            (GPIOA_INT_IRQn)
#define GPIO_MULTIPLE_GPIOA_INT_IIDX            (DL_INTERRUPT_GROUP1_IIDX_GPIOA)
#define GPIO_GRP_KEY_K4_IIDX                                (DL_GPIO_IIDX_DIO14)
#define GPIO_GRP_KEY_K4_PIN                                     (DL_GPIO_PIN_14)
#define GPIO_GRP_KEY_K4_IOMUX                                    (IOMUX_PINCM36)
/* Defines for PIN_R_A: GPIOB.14 with pinCMx 31 on package pin 2 */
#define GPIO_ENCODERS_PIN_R_A_PORT                                       (GPIOB)
#define GPIO_ENCODERS_PIN_R_A_IIDX                          (DL_GPIO_IIDX_DIO14)
#define GPIO_ENCODERS_PIN_R_A_PIN                               (DL_GPIO_PIN_14)
#define GPIO_ENCODERS_PIN_R_A_IOMUX                              (IOMUX_PINCM31)
/* Defines for PIN_R_B: GPIOB.0 with pinCMx 12 on package pin 47 */
#define GPIO_ENCODERS_PIN_R_B_PORT                                       (GPIOB)
#define GPIO_ENCODERS_PIN_R_B_IIDX                           (DL_GPIO_IIDX_DIO0)
#define GPIO_ENCODERS_PIN_R_B_PIN                                (DL_GPIO_PIN_0)
#define GPIO_ENCODERS_PIN_R_B_IOMUX                              (IOMUX_PINCM12)
/* Defines for PIN_L_A: GPIOA.7 with pinCMx 14 on package pin 49 */
#define GPIO_ENCODERS_PIN_L_A_PORT                                       (GPIOA)
#define GPIO_ENCODERS_PIN_L_A_IIDX                           (DL_GPIO_IIDX_DIO7)
#define GPIO_ENCODERS_PIN_L_A_PIN                                (DL_GPIO_PIN_7)
#define GPIO_ENCODERS_PIN_L_A_IOMUX                              (IOMUX_PINCM14)
/* Defines for PIN_L_B: GPIOB.20 with pinCMx 48 on package pin 19 */
#define GPIO_ENCODERS_PIN_L_B_PORT                                       (GPIOB)
#define GPIO_ENCODERS_PIN_L_B_IIDX                          (DL_GPIO_IIDX_DIO20)
#define GPIO_ENCODERS_PIN_L_B_PIN                               (DL_GPIO_PIN_20)
#define GPIO_ENCODERS_PIN_L_B_IOMUX                              (IOMUX_PINCM48)
/* Port definition for Pin Group GPIO_LINE_SENSOR */
#define GPIO_LINE_SENSOR_PORT                                            (GPIOB)

/* Defines for PIN_IN1: GPIOB.13 with pinCMx 30 on package pin 1 */
#define GPIO_LINE_SENSOR_PIN_IN1_PIN                            (DL_GPIO_PIN_13)
#define GPIO_LINE_SENSOR_PIN_IN1_IOMUX                           (IOMUX_PINCM30)
/* Defines for PIN_IN2: GPIOB.17 with pinCMx 43 on package pin 14 */
#define GPIO_LINE_SENSOR_PIN_IN2_PIN                            (DL_GPIO_PIN_17)
#define GPIO_LINE_SENSOR_PIN_IN2_IOMUX                           (IOMUX_PINCM43)
/* Defines for PIN_IN3: GPIOB.26 with pinCMx 57 on package pin 28 */
#define GPIO_LINE_SENSOR_PIN_IN3_PIN                            (DL_GPIO_PIN_26)
#define GPIO_LINE_SENSOR_PIN_IN3_IOMUX                           (IOMUX_PINCM57)
/* Defines for PIN_IN4: GPIOB.19 with pinCMx 45 on package pin 16 */
#define GPIO_LINE_SENSOR_PIN_IN4_PIN                            (DL_GPIO_PIN_19)
#define GPIO_LINE_SENSOR_PIN_IN4_IOMUX                           (IOMUX_PINCM45)
/* Defines for PIN_IN5: GPIOB.21 with pinCMx 49 on package pin 20 */
#define GPIO_LINE_SENSOR_PIN_IN5_PIN                            (DL_GPIO_PIN_21)
#define GPIO_LINE_SENSOR_PIN_IN5_IOMUX                           (IOMUX_PINCM49)
/* Defines for PIN_IN6: GPIOB.10 with pinCMx 27 on package pin 62 */
#define GPIO_LINE_SENSOR_PIN_IN6_PIN                            (DL_GPIO_PIN_10)
#define GPIO_LINE_SENSOR_PIN_IN6_IOMUX                           (IOMUX_PINCM27)
/* Defines for PIN_IN7: GPIOB.11 with pinCMx 28 on package pin 63 */
#define GPIO_LINE_SENSOR_PIN_IN7_PIN                            (DL_GPIO_PIN_11)
#define GPIO_LINE_SENSOR_PIN_IN7_IOMUX                           (IOMUX_PINCM28)
/* Defines for PIN_IN8: GPIOB.24 with pinCMx 52 on package pin 23 */
#define GPIO_LINE_SENSOR_PIN_IN8_PIN                            (DL_GPIO_PIN_24)
#define GPIO_LINE_SENSOR_PIN_IN8_IOMUX                           (IOMUX_PINCM52)
/* Port definition for Pin Group GPIO_DRIVE_DIR */
#define GPIO_DRIVE_DIR_PORT                                              (GPIOA)

/* Defines for BIN1: GPIOA.17 with pinCMx 39 on package pin 10 */
#define GPIO_DRIVE_DIR_BIN1_PIN                                 (DL_GPIO_PIN_17)
#define GPIO_DRIVE_DIR_BIN1_IOMUX                                (IOMUX_PINCM39)
/* Defines for BIN2: GPIOA.24 with pinCMx 54 on package pin 25 */
#define GPIO_DRIVE_DIR_BIN2_PIN                                 (DL_GPIO_PIN_24)
#define GPIO_DRIVE_DIR_BIN2_IOMUX                                (IOMUX_PINCM54)
/* Defines for AIN1: GPIOA.13 with pinCMx 35 on package pin 6 */
#define GPIO_DRIVE_DIR_AIN1_PIN                                 (DL_GPIO_PIN_13)
#define GPIO_DRIVE_DIR_AIN1_IOMUX                                (IOMUX_PINCM35)
/* Defines for AIN2: GPIOA.12 with pinCMx 34 on package pin 5 */
#define GPIO_DRIVE_DIR_AIN2_PIN                                 (DL_GPIO_PIN_12)
#define GPIO_DRIVE_DIR_AIN2_IOMUX                                (IOMUX_PINCM34)
#define GPIOB_EVENT_PUBLISHER_0_CHANNEL                                      (1)
#define GPIOB_EVENT_PUBLISHER_1_CHANNEL                                      (1)
#define GPIOA_EVENT_PUBLISHER_0_CHANNEL                                      (1)


/* clang-format on */

void SYSCFG_DL_init(void);
void SYSCFG_DL_initPower(void);
void SYSCFG_DL_GPIO_init(void);
void SYSCFG_DL_SYSCTL_init(void);

bool SYSCFG_DL_SYSCTL_SYSPLL_init(void);
void SYSCFG_DL_PWM_DRIVE_LEFT_init(void);
void SYSCFG_DL_PWM_DRIVE_RIGHT_init(void);
void SYSCFG_DL_I2C_AUX_init(void);
void SYSCFG_DL_I2C_IMU_init(void);
void SYSCFG_DL_UART_STEPPER_BUS_init(void);
void SYSCFG_DL_UART_HOST_LINK_init(void);
void SYSCFG_DL_UART_VISION_init(void);
void SYSCFG_DL_UART_IMU_init(void);
void SYSCFG_DL_DMA_init(void);


bool SYSCFG_DL_saveConfiguration(void);
bool SYSCFG_DL_restoreConfiguration(void);

#ifdef __cplusplus
}
#endif

#endif /* ti_msp_dl_config_h */
