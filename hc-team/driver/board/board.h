/**
 * @file    board.h
 * @brief   Board-level initialization and interrupt enable Driver.
 *
 * Owns the single call to SysConfig root initialization and the global
 * interrupt enable sequence. No other project layer may include
 * ti_msp_dl_config.h or manipulate NVIC directly.
 */
#ifndef BOARD_H
#define BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Perform the one-time board peripheral initialization.
 * @note Must be called before any Driver that uses SysConfig-generated
 *       peripherals. Leaves all PWM/compare outputs at their safe reset
 *       values; Motor PWM counters are started separately by Motor_StartPwm().
 */
void Board_Init(void);

/**
 * @brief Enable the NVIC lines used by the board and enable global interrupts.
 * @note Must be called after all Driver Init() functions have completed.
 */
void Board_EnableInterrupts(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
