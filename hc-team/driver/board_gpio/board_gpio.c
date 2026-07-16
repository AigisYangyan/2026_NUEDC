/**
 * @file    board_gpio.c
 * @brief   Board-level GPIO raw snapshot implementation (transitional).
 *
 * Until P1.5 completes the shared GPIO IRQ refactor, this implementation
 * forwards to the existing Runtime encoder count getter. The public
 * contract (BoardEncoderRawSnapshot) is stable and will remain unchanged
 * when the underlying IRQ owner moves from Runtime to Board GPIO.
 */
#include "driver/board_gpio/board_gpio.h"

#include "driver/mspm0_runtime/mspm0_runtime.h"
#include "ti_msp_dl_config.h"

#include <ti/driverlib/dl_gpio.h>

static bool board_gpio_key_pressed(GPIO_Regs *port, uint32_t pin)
{
    return (DL_GPIO_readPins(port, pin) == 0u);
}

bool BoardGpio_GetEncoderRawSnapshot(BoardEncoderRawSnapshot *out)
{
    if (out == NULL) {
        return false;
    }

    Mspm0Runtime_GetEncoderCounts(&out->left, &out->right);
    return true;
}

uint8_t BoardGpio_ConsumeKeyIrqEdges(void)
{
    return Mspm0Runtime_ConsumeKeyIrqEdges();
}

uint8_t BoardGpio_GetKeyRawLevels(void)
{
    uint8_t pressed_bits = 0u;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();

    if (board_gpio_key_pressed(GPIO_GRP_KEY_K1_PORT, GPIO_GRP_KEY_K1_PIN)) {
        pressed_bits |= (1u << 0);
    }
    if (board_gpio_key_pressed(GPIO_GRP_KEY_K2_PORT, GPIO_GRP_KEY_K2_PIN)) {
        pressed_bits |= (1u << 1);
    }
    if (board_gpio_key_pressed(GPIO_GRP_KEY_K3_PORT, GPIO_GRP_KEY_K3_PIN)) {
        pressed_bits |= (1u << 2);
    }
    if (board_gpio_key_pressed(GPIO_GRP_KEY_K4_PORT, GPIO_GRP_KEY_K4_PIN)) {
        pressed_bits |= (1u << 3);
    }

    __set_PRIMASK(primask);
    return pressed_bits;
}
