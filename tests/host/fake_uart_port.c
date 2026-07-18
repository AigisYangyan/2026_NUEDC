#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/board_uart/imu_uart.h"
#include "driver/board_uart/stepmotor_uart.h"
#include "driver/board_uart/vision_uart.h"
#include "driver/board_uart/vofa_uart.h"

void VisionUart_TestPushRxByte(uint8_t data);
void VofaUart_TestPushRxByte(uint8_t data);
void StepmotorUart_TestPushRxByte(uint8_t data);
void ImuUart_TestPushRxByte(uint8_t data);
void VisionUart_TestCompleteTx(void);
void VofaUart_TestCompleteTx(void);
void StepmotorUart_TestCompleteTx(void);
uint32_t VisionUart_TestCopyLastTx(uint8_t *out, uint32_t capacity);
uint32_t VofaUart_TestCopyLastTx(uint8_t *out, uint32_t capacity);
uint32_t StepmotorUart_TestCopyLastTx(uint8_t *out, uint32_t capacity);
uint32_t ImuUart_TestCopyTxLog(uint8_t *out, uint32_t capacity);

void FakeUartPort_ResetAll(void)
{
    VisionUart_Init();
    VofaUart_Init();
    StepmotorUart_Init();
    ImuUart_Init();
}

void FakeUartPort_PushVisionBytes(const uint8_t *data, uint32_t length)
{
    uint32_t index = 0u;

    for (index = 0u; index < length; index++) {
        VisionUart_TestPushRxByte(data[index]);
    }
}

void FakeUartPort_PushVofaBytes(const uint8_t *data, uint32_t length)
{
    uint32_t index = 0u;

    for (index = 0u; index < length; index++) {
        VofaUart_TestPushRxByte(data[index]);
    }
}

void FakeUartPort_PushStepmotorBytes(const uint8_t *data, uint32_t length)
{
    uint32_t index = 0u;

    for (index = 0u; index < length; index++) {
        StepmotorUart_TestPushRxByte(data[index]);
    }
}

void FakeUartPort_PushImuBytes(const uint8_t *data, uint32_t length)
{
    uint32_t index = 0u;

    for (index = 0u; index < length; index++) {
        ImuUart_TestPushRxByte(data[index]);
    }
}

uint32_t FakeUartPort_CopyImuTxLog(uint8_t *out, uint32_t capacity)
{
    if ((out != NULL) && (capacity > 0u)) {
        memset(out, 0, capacity);
    }

    return ImuUart_TestCopyTxLog(out, capacity);
}

void FakeUartPort_CompleteVisionTx(void)
{
    VisionUart_TestCompleteTx();
}

uint32_t FakeUartPort_CopyVisionTx(uint8_t *out, uint32_t capacity)
{
    if ((out != NULL) && (capacity > 0u)) {
        memset(out, 0, capacity);
    }

    return VisionUart_TestCopyLastTx(out, capacity);
}

void FakeUartPort_CompleteVofaTx(void)
{
    VofaUart_TestCompleteTx();
}

void FakeUartPort_CompleteStepmotorTx(void)
{
    StepmotorUart_TestCompleteTx();
}

uint32_t FakeUartPort_CopyVofaTx(uint8_t *out, uint32_t capacity)
{
    if ((out != NULL) && (capacity > 0u)) {
        memset(out, 0, capacity);
    }

    return VofaUart_TestCopyLastTx(out, capacity);
}

uint32_t FakeUartPort_CopyStepmotorTx(uint8_t *out, uint32_t capacity)
{
    if ((out != NULL) && (capacity > 0u)) {
        memset(out, 0, capacity);
    }

    return StepmotorUart_TestCopyLastTx(out, capacity);
}
