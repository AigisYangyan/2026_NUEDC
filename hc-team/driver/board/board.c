/**
 * @file    board.c
 * @brief   MSPM0G3519 board-level initialization implementation.
 *
 * This is the only project file allowed to call SYSCFG_DL_init(),
 * NVIC_EnableIRQ(), and __enable_irq().
 */
#include "driver/board/board.h"

#include "ti_msp_dl_config.h"

void Board_Init(void)
{
    SYSCFG_DL_init();

    /* QEI counters free-run from boot; readers only ever sample them. */
    DL_TimerG_startCounter(QEI_LEFT_INST);
    DL_TimerG_startCounter(QEI_RIGHT_INST);
}

void Board_EnableInterrupts(void)
{
    NVIC_EnableIRQ(GPIOA_INT_IRQn);
    NVIC_EnableIRQ(GPIOB_INT_IRQn);
    NVIC_EnableIRQ(DMA_INT_IRQn);
    NVIC_EnableIRQ(UART_VISION_INST_INT_IRQN);
    NVIC_EnableIRQ(UART_STEPPER_BUS_INST_INT_IRQN);
    NVIC_EnableIRQ(UART_IMU_INST_INT_IRQN);
    NVIC_EnableIRQ(UART_BSL_ENTRY_INST_INT_IRQN); /* D14：0x22 软触发 BSL 入口，逐字节 RX 中断 */

    __enable_irq();
}
