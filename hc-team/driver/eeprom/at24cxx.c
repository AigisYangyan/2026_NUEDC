/*
 * @file    at24cxx.c
 * @brief   AT24C02 EEPROM 读写实现（DriverLib I2C_OLED 总线）
 *
 * Address note: DL_I2C_* expects 7-bit address 0x50 (not shifted 0xA0).
 */

#include "at24cxx.h"

#include "driver/mspm0_runtime/mspm0_runtime.h"
#include "ti_msp_dl_config.h"

#include <stddef.h>
#include <ti/driverlib/dl_i2c.h>

#define AT24_I2C_TIMEOUT_LOOPS 50000u
#define AT24_WRITE_CYCLE_MS    5u

static uint8_t at24cxx_check(void);

static int at24_i2c_wait_not_busy(void)
{
    uint32_t timeout = AT24_I2C_TIMEOUT_LOOPS;

    while ((DL_I2C_getControllerStatus(I2C_AUX_INST) &
            DL_I2C_CONTROLLER_STATUS_BUSY) != 0u) {
        if (timeout == 0u) {
            return -1;
        }
        timeout--;
    }
    return 0;
}

static int at24_i2c_write(uint8_t addr7, const uint8_t *p_data, uint16_t len)
{
    uint16_t i = 0u;
    uint32_t timeout_fifo;

    if ((p_data == NULL) || (len == 0u)) {
        return -1;
    }

    if (at24_i2c_wait_not_busy() != 0) {
        return -1;
    }

    while ((i < len) &&
           (DL_I2C_isControllerTXFIFOFull(I2C_AUX_INST) == false)) {
        DL_I2C_transmitControllerData(I2C_AUX_INST, p_data[i]);
        i++;
    }

    DL_I2C_startControllerTransfer(I2C_AUX_INST,
                                   (uint32_t)addr7,
                                   DL_I2C_CONTROLLER_DIRECTION_TX,
                                   len);

    while (i < len) {
        timeout_fifo = AT24_I2C_TIMEOUT_LOOPS;
        while (DL_I2C_isControllerTXFIFOFull(I2C_AUX_INST) != false) {
            if (timeout_fifo == 0u) {
                return -1;
            }
            timeout_fifo--;
        }
        DL_I2C_transmitControllerData(I2C_AUX_INST, p_data[i]);
        i++;
    }

    if (at24_i2c_wait_not_busy() != 0) {
        return -1;
    }

    if ((DL_I2C_getControllerStatus(I2C_AUX_INST) &
         DL_I2C_CONTROLLER_STATUS_ERROR) != 0u) {
        return -1;
    }

    return 0;
}

static int at24_i2c_mem_write(uint8_t addr7,
                              uint8_t mem_addr,
                              const uint8_t *p_data,
                              uint16_t len)
{
    uint8_t buf[1u + AT24_PAGE_SIZE];
    uint16_t i;

    if ((p_data == NULL) || (len == 0u) || (len > AT24_PAGE_SIZE)) {
        return -1;
    }

    buf[0] = mem_addr;
    for (i = 0u; i < len; i++) {
        buf[i + 1u] = p_data[i];
    }

    return at24_i2c_write(addr7, buf, (uint16_t)(len + 1u));
}

static int at24_i2c_mem_read(uint8_t addr7,
                             uint8_t mem_addr,
                             uint8_t *p_data,
                             uint16_t len)
{
    uint16_t i;
    uint32_t timeout;

    if ((p_data == NULL) || (len == 0u)) {
        return -1;
    }

    if (at24_i2c_write(addr7, &mem_addr, 1u) != 0) {
        return -1;
    }

    DL_I2C_startControllerTransfer(I2C_AUX_INST,
                                   (uint32_t)addr7,
                                   DL_I2C_CONTROLLER_DIRECTION_RX,
                                   len);

    for (i = 0u; i < len; i++) {
        timeout = AT24_I2C_TIMEOUT_LOOPS;
        while (DL_I2C_isControllerRXFIFOEmpty(I2C_AUX_INST) != false) {
            if (timeout == 0u) {
                return -1;
            }
            timeout--;
        }
        p_data[i] = DL_I2C_receiveControllerData(I2C_AUX_INST);
    }

    if (at24_i2c_wait_not_busy() != 0) {
        return -1;
    }

    return 0;
}

static void at24_page_write(uint16_t mem_addr,
                            const uint8_t *buffer,
                            uint16_t buffer_size)
{
    uint16_t remaining_bytes = buffer_size;
    uint16_t current_addr = mem_addr;
    const uint8_t *buffer_ptr = buffer;

    while (remaining_bytes > 0u) {
        uint16_t page_offset = current_addr % AT24_PAGE_SIZE;
        uint16_t bytes_in_page = (uint16_t)AT24_PAGE_SIZE - page_offset;
        uint16_t write_size =
            (remaining_bytes < bytes_in_page) ? remaining_bytes : bytes_in_page;

        (void)at24_i2c_mem_write(AT24C_DEV_ADDR,
                                 (uint8_t)current_addr,
                                 buffer_ptr,
                                 write_size);
        remaining_bytes = (uint16_t)(remaining_bytes - write_size);
        current_addr = (uint16_t)(current_addr + write_size);
        buffer_ptr += write_size;
        Mspm0Runtime_DelayMs(AT24_WRITE_CYCLE_MS);
    }
}

void at24cxx_init(void)
{
    uint8_t res;

    if ((DL_I2C_getControllerStatus(I2C_AUX_INST) &
         DL_I2C_CONTROLLER_STATUS_BUSY_BUS) == 0u) {
        DL_I2C_enableController(I2C_AUX_INST);
    }

    res = at24cxx_check();
    if (!res) {
        printf("AT24CXX OK!\r\n");
    }
    else {
        printf("AT24CXX ERROR!\r\n");
    }
}

/**
 * @brief  将数据写入 AT24C02。
 * @param  _usWriteAddr  起始 EEPROM 地址，范围 0~255。
 * @param  _ucpBuffer    指向待写入数据的缓冲区。
 * @param  _usNumToWrite 计划写入的字节数，0 表示不写。
 * @note   函数会自动限制写入区域不越界，并按页写入。
 */
void at24cxx_write(uint16_t _usWriteAddr, uint8_t* _ucpBuffer, uint16_t _usNumToWrite)
{
    if ((_ucpBuffer == NULL) || (_usNumToWrite == 0U) || (_usWriteAddr >= AT24_TOTAL_SIZE)) {
        return;
    }

    uint16_t writable = AT24_TOTAL_SIZE - _usWriteAddr; /* 剩余可写字节数 */
    if (_usNumToWrite > writable) {
        _usNumToWrite = writable;
    }

    at24_page_write(_usWriteAddr, _ucpBuffer, _usNumToWrite);
    Mspm0Runtime_DelayMs(AT24_WRITE_CYCLE_MS);
}

/**
 * @brief  从 AT24C02 读取数据。
 * @param  _usReadAddr  起始 EEPROM 地址，范围 0~255。
 * @param  _ucpBuffer   指向接收数据的缓冲区。
 * @param  _usNumToRead 计划读取的字节数，0 表示不读。
 * @note   函数会自动裁剪读取长度，避免越界读取。
 */
void at24cxx_read(uint16_t _usReadAddr, uint8_t* _ucpBuffer, uint16_t _usNumToRead)
{
    if ((_ucpBuffer == NULL) || (_usNumToRead == 0U) || (_usReadAddr >= AT24_TOTAL_SIZE)) {
        return;
    }

    uint16_t readable = AT24_TOTAL_SIZE - _usReadAddr; /* 剩余可读字节数 */
    if (_usNumToRead > readable) {
        _usNumToRead = readable;
    }

    (void)at24_i2c_mem_read(AT24C_DEV_ADDR,
                            (uint8_t)_usReadAddr,
                            _ucpBuffer,
                            _usNumToRead);
}

static uint8_t at24cxx_check(void)
{
    uint8_t _ucTemp;
    uint8_t _ucData = 0XAB;

    at24cxx_read(E2PROM_TYPE, &_ucTemp, 1);

    if (_ucTemp != 0XAB) {

        at24cxx_write(E2PROM_TYPE, &_ucData, 1);
        at24cxx_read(E2PROM_TYPE, &_ucTemp, 1);
        if (_ucTemp != 0XAB)
            return 1;
    }
    else {
        return 0;
    }

    return 0;
}
