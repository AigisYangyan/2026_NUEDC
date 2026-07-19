/**
 * @file    param_store_port.h
 * @brief   参数存储 Driver 的硬件边界（HAL port）
 *
 * 本头是 param_store.c（框定/CRC 逻辑，可主机测试）与 param_store_hw.c
 * （唯一允许碰 DL_FlashCTL 的实现）之间的唯一契约，范式与 gray.h / gray_port.h 一致。
 *
 * 主机测试用 tests/host/fake_param_store_port.c 顶替 param_store_hw.c，从而在不接触
 * TI HAL 的前提下验证「擦前写 + 框定 + CRC 校验」核心逻辑。
 *
 * 端口约定：param_store.c 保证 program 的 offset 与 len 均为 8 字节倍数（片内 flash 64-bit
 * 编程粒度 + ECC），故硬件实现按 64-bit 字编程即可，无需处理非对齐。
 *
 * 本头**不属于**公共 API：上层只应包含 param_store.h。
 */
#ifndef HC_TEAM_DRIVER_PARAM_STORE_PARAM_STORE_PORT_H
#define HC_TEAM_DRIVER_PARAM_STORE_PARAM_STORE_PORT_H

#include <stdbool.h>
#include <stdint.h>

/** 参数扇区可用字节容量（片内 = 1 个 flash 扇区）。 */
uint16_t param_store_port_capacity(void);

/** 擦除整个参数扇区（擦后全 0xFF）。@return true 成功。 */
bool param_store_port_erase(void);

/**
 * @brief 从扇区起 offset 处编程 len 字节。
 * @param offset 字节偏移，8 字节对齐（由 param_store.c 保证）。
 * @param src    源数据。
 * @param len    字节数，8 字节倍数（由 param_store.c 保证）。
 * @return true 成功。前置：目标区已擦除。
 */
bool param_store_port_program(uint16_t offset, const uint8_t *src, uint16_t len);

/** 从扇区起 offset 处读 len 字节到 dst（片内即 memcpy 自 flash 地址）。 */
void param_store_port_read(uint16_t offset, uint8_t *dst, uint16_t len);

#endif /* HC_TEAM_DRIVER_PARAM_STORE_PARAM_STORE_PORT_H */
