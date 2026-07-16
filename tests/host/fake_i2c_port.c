#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define FAKE_I2C_TX_CAPTURE_CAPACITY 8192u
#define FAKE_I2C_STATUS_BUSY  0x01u
#define FAKE_I2C_STATUS_ERROR 0x02u

typedef struct {
    uint8_t tx_bytes[FAKE_I2C_TX_CAPTURE_CAPACITY];
    uint32_t tx_count;
    uint32_t now_ms;
    uint32_t busy_polls_after_start;
    uint32_t busy_polls_remaining;
    uint32_t tx_fifo_full_polls;
    uint32_t tx_fifo_full_remaining;
    uint32_t transfer_count;
    uint32_t enable_count;
    uint32_t disable_count;
    uint8_t last_addr;
    uint16_t last_len;
    bool force_busy_stuck;
    bool force_tx_fifo_stuck;
    bool controller_error;
} FakeI2cPort_State_t;

static FakeI2cPort_State_t s_fake_i2c;

void FakeI2cPort_Reset(void)
{
    memset(&s_fake_i2c, 0, sizeof(s_fake_i2c));
    s_fake_i2c.busy_polls_after_start = 1u;
}

void FakeI2cPort_SetNowMs(uint32_t now_ms)
{
    s_fake_i2c.now_ms = now_ms;
}

uint32_t Clock_NowMs(void)
{
    return s_fake_i2c.now_ms;
}

void FakeI2cPort_SetBusyStuck(bool enabled)
{
    s_fake_i2c.force_busy_stuck = enabled;
}

void FakeI2cPort_SetTxFifoStuck(bool enabled)
{
    s_fake_i2c.force_tx_fifo_stuck = enabled;
}

void FakeI2cPort_SetControllerError(bool enabled)
{
    s_fake_i2c.controller_error = enabled;
}

uint32_t FakeI2cPort_GetWriteCount(void)
{
    return s_fake_i2c.tx_count;
}

uint32_t FakeI2cPort_GetTransferCount(void)
{
    return s_fake_i2c.transfer_count;
}

uint32_t FakeI2cPort_CopyTx(uint8_t *out, uint32_t capacity)
{
    uint32_t copy_len = s_fake_i2c.tx_count;

    if (copy_len > capacity) {
        copy_len = capacity;
    }

    if ((out != NULL) && (copy_len > 0u)) {
        memcpy(out, s_fake_i2c.tx_bytes, copy_len);
    }

    return copy_len;
}

uint32_t FakeI2cPort_GetControllerStatus(void)
{
    uint32_t status = 0u;

    if (s_fake_i2c.force_busy_stuck) {
        status |= FAKE_I2C_STATUS_BUSY;
    }
    else if (s_fake_i2c.busy_polls_remaining > 0u) {
        s_fake_i2c.busy_polls_remaining--;
        status |= FAKE_I2C_STATUS_BUSY;
    }

    if (s_fake_i2c.controller_error) {
        status |= FAKE_I2C_STATUS_ERROR;
    }

    return status;
}

bool FakeI2cPort_IsControllerTxFifoFull(void)
{
    if (s_fake_i2c.force_tx_fifo_stuck) {
        return true;
    }

    if (s_fake_i2c.tx_fifo_full_remaining > 0u) {
        s_fake_i2c.tx_fifo_full_remaining--;
        return true;
    }

    return false;
}

void FakeI2cPort_TransmitControllerData(uint8_t data)
{
    if (s_fake_i2c.tx_count < FAKE_I2C_TX_CAPTURE_CAPACITY) {
        s_fake_i2c.tx_bytes[s_fake_i2c.tx_count] = data;
    }

    s_fake_i2c.tx_count++;
}

void FakeI2cPort_StartControllerTransfer(uint8_t addr7, uint16_t len)
{
    s_fake_i2c.last_addr = addr7;
    s_fake_i2c.last_len = len;
    s_fake_i2c.transfer_count++;
    s_fake_i2c.busy_polls_remaining = s_fake_i2c.busy_polls_after_start;
    s_fake_i2c.tx_fifo_full_remaining = s_fake_i2c.tx_fifo_full_polls;
}

void FakeI2cPort_DisableController(void)
{
    s_fake_i2c.disable_count++;
}

void FakeI2cPort_EnableController(void)
{
    s_fake_i2c.enable_count++;
}
