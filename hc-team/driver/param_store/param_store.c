/**
 * @file    param_store.c
 * @brief   参数 blob 存储框定/CRC 逻辑（可主机测试；不触硬件）。
 *
 * 记录布局（扇区起始，小端）：
 *   [0..1] magic       = PARAM_STORE_MAGIC
 *   [2]    format_ver  = PARAM_STORE_FORMAT_VER（框定层版本，非 payload 语义版本）
 *   [3]    reserved    = 0（对齐填充）
 *   [4..5] len         = payload 字节数
 *   [6..7] crc16       = CRC16-CCITT(payload)（poly 0x1021, init 0xFFFF）
 *   [8..]  payload
 * 记录尾按 8 字节对齐用 0xFF 补齐（flash 64-bit 编程粒度）。
 *
 * 擦前写：Save 恒 erase 整扇区再 program，无残留旧记录混读。
 * buf 不动保证：Read 先读到私有临时区校验，仅 CRC 通过才 memcpy 到调用方 buf。
 */
#include "driver/param_store/param_store.h"
#include "driver/param_store/param_store_port.h"

#include <string.h>

#define PARAM_STORE_MAGIC       0x50A5u
#define PARAM_STORE_FORMAT_VER  1u
#define PARAM_STORE_HEADER_SIZE 8u

/* 记录组装缓冲 / 读回临时区：static 而非栈局部——目标栈仅 256B，1KB 级局部会溢出。
 * Driver 单线程非重入，static 复用安全。 */
static uint8_t s_record[PARAM_STORE_HEADER_SIZE + PARAM_STORE_MAX_PAYLOAD];
static uint8_t s_verify[PARAM_STORE_MAX_PAYLOAD];   /* Read 内部读回临时区 */
static uint8_t s_readback[PARAM_STORE_MAX_PAYLOAD]; /* Save 写后校验目标（与 s_verify 分离，避免自拷贝） */

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t n)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    uint8_t bit;

    for (i = 0u; i < n; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

bool ParamStore_Read(uint8_t *buf, uint16_t len)
{
    uint8_t header[PARAM_STORE_HEADER_SIZE];
    uint16_t stored_len;
    uint16_t stored_crc;

    if ((buf == NULL) || (len == 0u) || (len > PARAM_STORE_MAX_PAYLOAD)) {
        return false;
    }

    param_store_port_read(0u, header, PARAM_STORE_HEADER_SIZE);

    if (rd_u16(&header[0]) != PARAM_STORE_MAGIC) {
        return false;                       /* 空扇区(0xFFFF)或异物 */
    }
    if (header[2] != PARAM_STORE_FORMAT_VER) {
        return false;
    }
    stored_len = rd_u16(&header[4]);
    if (stored_len != len) {
        return false;
    }
    stored_crc = rd_u16(&header[6]);

    param_store_port_read(PARAM_STORE_HEADER_SIZE, s_verify, len);
    if (crc16_ccitt(s_verify, len) != stored_crc) {
        return false;
    }

    memcpy(buf, s_verify, len);             /* 仅校验通过才落调用方 buf */
    return true;
}

bool ParamStore_Save(const uint8_t *buf, uint16_t len)
{
    uint16_t crc;
    uint16_t total;
    uint16_t padded;
    uint16_t i;

    if ((buf == NULL) || (len == 0u) || (len > PARAM_STORE_MAX_PAYLOAD)) {
        return false;
    }
    total = (uint16_t)(PARAM_STORE_HEADER_SIZE + len);
    if (total > param_store_port_capacity()) {
        return false;
    }

    crc = crc16_ccitt(buf, len);
    s_record[0] = (uint8_t)(PARAM_STORE_MAGIC & 0xFFu);
    s_record[1] = (uint8_t)(PARAM_STORE_MAGIC >> 8);
    s_record[2] = (uint8_t)PARAM_STORE_FORMAT_VER;
    s_record[3] = 0u;
    s_record[4] = (uint8_t)(len & 0xFFu);
    s_record[5] = (uint8_t)(len >> 8);
    s_record[6] = (uint8_t)(crc & 0xFFu);
    s_record[7] = (uint8_t)(crc >> 8);
    memcpy(&s_record[PARAM_STORE_HEADER_SIZE], buf, len);

    padded = (uint16_t)((total + 7u) & (uint16_t)~7u);
    for (i = total; i < padded; i++) {
        s_record[i] = 0xFFu;                /* 尾部对齐填充（=擦除态） */
    }

    if (!param_store_port_erase()) {
        return false;
    }
    if (!param_store_port_program(0u, s_record, padded)) {
        return false;
    }

    /* 读回校验：写后确认可完整读回原 payload。 */
    if (!ParamStore_Read(s_readback, len)) {
        return false;
    }
    return (memcmp(s_readback, buf, len) == 0);
}
