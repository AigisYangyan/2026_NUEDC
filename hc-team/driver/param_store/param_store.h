/**
 * @file    param_store.h
 * @brief   片内 flash 参数 blob 存储 Driver（掉电保存）——公共面
 *
 * 抽象（器件能做什么）：把一段调用方定义的字节 blob **完整性**地存入片内 NV 并读回，
 * 掉电保持。框定 = magic + 格式版本 + 长度 + CRC16；写=擦前写+读回校验；读=校验通过才吐值。
 *
 * 隐藏：flash 扇区地址、DL_FlashCTL 擦/写/ECC、字对齐、记录框定与 CRC 细节
 * （全在本模块，`param_store_hw.c` 唯一触 DL HAL）。
 *
 * 分层与所有权（AGENTS.md §4，本模块属 Driver）：
 * - NV 完整性（扇区、magic/版本/长度/CRC16、擦前写、读回校验）唯一属本模块；
 * - **payload 语义不可知**：本模块不解释 blob 内容；payload 内的语义版本由调用方（param_tune）自持首字节。
 * - 无重试 / 无 wear-leveling：单扇区、SAVE 稀发，不预建（simplicity-first）。
 *
 * 调用前置条件：System 时钟/flash 控制器已由 SysConfig 上电（本模块不 Init）。
 * 真实 flash 擦/写在硬件上验证（用户上板自理），主机测试经 fake 端口验证框定与校验逻辑。
 */
#ifndef HC_TEAM_DRIVER_PARAM_STORE_PARAM_STORE_H
#define HC_TEAM_DRIVER_PARAM_STORE_PARAM_STORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 单条记录 payload 上限（字节）。header(8)+payload 必须 ≤ 端口扇区容量。 */
#define PARAM_STORE_MAX_PAYLOAD 48u

/**
 * @brief 读回持久 payload。
 * @param buf 输出缓冲，容量须 ≥ len；仅在返回 true 时被写入。
 * @param len 期望的 payload 长度（须与存储时一致）。
 * @return true = 有有效记录（magic/格式版本/长度匹配且 CRC 正确）且已拷入 buf；
 *         false = 无有效记录（空扇区/坏 CRC/版本或长度不符/len 越界），**buf 不被触碰**。
 */
bool ParamStore_Read(uint8_t *buf, uint16_t len);

/**
 * @brief 保存 payload：擦扇区 → 编程框定记录 → 读回校验。
 * @param buf 待存 payload，长度 len。
 * @param len payload 长度，须 1..PARAM_STORE_MAX_PAYLOAD 且 header+len ≤ 端口容量。
 * @return true = 擦/写/读回校验全通过；false = len 越界 / 端口擦或写失败 / 读回校验不符。
 */
bool ParamStore_Save(const uint8_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* HC_TEAM_DRIVER_PARAM_STORE_PARAM_STORE_H */
