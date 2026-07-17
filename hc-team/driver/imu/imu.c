/**
 * @file    imu.c
 * @brief   单轴姿态模组协议解析与指令组包。
 *
 * 器件事实来源：hc-team/IMU_NEW_EXAMPLE/数据手册.pdf（串口协议章节）。
 *
 * 读帧（器件 -> MCU），5 字节定长：
 *     0x5A | TYPE | DATAL | DATAH | SUM
 *     TYPE = 0xAA 角速度，0xBB 航向角
 *     SUM  = (0x5A + TYPE + DATAL + DATAH) 的低 8 位
 *
 * 写帧（MCU -> 器件），5 字节定长，无校验和：
 *     0x55 | 0xAA | ADDR | DATAL | DATAH
 *     所有设置必须 解锁 -> 延时 -> 写 -> 延时 -> 保存。
 *
 * 解析器状态机（滑动窗口，5 字节）：
 *     收字节 -> 窗口未满则继续收
 *            -> 窗口满则校验：帧头/类型/校验和三者全过 -> 采纳并清空窗口
 *                             任一不过 -> 丢弃最早一个字节，窗口左移，等下一字节
 *     丢一个字节而非整帧丢弃，是因为真正的帧起点可能就在窗口内部
 *     （例如器件上电瞬间的半个帧后面紧跟一个完整帧）。
 */
#include "driver/imu/imu.h"

#include "driver/board_uart/imu_uart.h"
#include "driver/clock/clock.h"
#include "driver/mspm0_runtime/mspm0_runtime.h"

#include <stddef.h>
#include <string.h>

/* ---- 协议常量 ----------------------------------------------------------- */

#define IMU_RX_FRAME_LEN 5u
#define IMU_RX_HEAD      0x5Au
#define IMU_RX_TYPE_GYRO 0xAAu
#define IMU_RX_TYPE_YAW  0xBBu

#define IMU_TX_FRAME_LEN 5u
#define IMU_TX_HEAD0     0x55u
#define IMU_TX_HEAD1     0xAAu

#define IMU_REG_SAVE     0x00u
#define IMU_REG_RRATE    0x02u
#define IMU_REG_KEY      0x13u
#define IMU_REG_YAW_ZERO 0x15u

/* 解锁寄存器的固定数据字节。datasheet 正文称「写入 0x8E5F」，但其给出的可用
 * 字节序列是 55 AA 13 8E 5F，与写格式的低字节在前自相矛盾。示例工程按字节
 * 序列发送且实测可用，故此处按字节序列固化，不按寄存器语义重建。 */
#define IMU_KEY_DATA_LO 0x8Eu
#define IMU_KEY_DATA_HI 0x5Fu

/* datasheet 要求每条设置指令之间延时 100ms。 */
#define IMU_CMD_SETTLE_MS 100u

/* 量程换算。角速度取 2000 而非参数指标表的 ±400：协议章节与示例代码一致取
 * 2000，且若实物量程真为 ±400，raw 不会超过 6554，换算结果不受影响。 */
#define IMU_RAW_FULL_SCALE      32768.0f
#define IMU_GYRO_FULL_SCALE_DPS 2000.0f
#define IMU_YAW_FULL_SCALE_DEG  180.0f

/* 单次从端口取走的字节数。Imu_Update() 循环到取空为止，故此值只影响循环次数。 */
#define IMU_DRAIN_CHUNK 32u

/* ---- 私有状态 ----------------------------------------------------------- */

static uint8_t s_frame[IMU_RX_FRAME_LEN];
static uint8_t s_frame_len;
static float s_yaw_deg;
static float s_yaw_rate_dps;
static uint32_t s_last_frame_ms;
static uint32_t s_frame_count;
static uint32_t s_checksum_error_count;
static bool s_valid;

/* ---- 解析 --------------------------------------------------------------- */

/**
 * @brief 按小端组合两个字节为有符号 16 位原始值。
 * @note  memcpy 型双关是标准 C 中唯一不含实现定义行为的转换方式，
 *        与 encoder.c 的 u32_mod_i32()、mspm0_runtime.c 的 runtime_u16_mod_i16()
 *        采用同一理由。
 */
static int16_t imu_raw_i16(uint8_t lo, uint8_t hi)
{
    uint16_t raw_u = (uint16_t)(((uint16_t)hi << 8) | (uint16_t)lo);
    int16_t raw_s;

    memcpy(&raw_s, &raw_u, sizeof(raw_s));
    return raw_s;
}

/**
 * @brief 校验并采纳当前 5 字节窗口。
 * @return 窗口是否是一个完整有效帧。
 * @note  只有「帧头对 + 类型已知 + 校验和对」才计 frame_count。
 *        类型未知时不计校验和错误 —— 器件会发本 Driver 不解析的帧类型
 *        （例如手册中的 0x5A 0xCC 状态回包），把它们计成错误会污染诊断。
 */
static bool imu_try_frame(void)
{
    uint8_t sum = 0u;
    int16_t raw = 0;

    if (s_frame[0] != IMU_RX_HEAD) {
        return false;
    }

    if ((s_frame[1] != IMU_RX_TYPE_GYRO) && (s_frame[1] != IMU_RX_TYPE_YAW)) {
        return false;
    }

    sum = (uint8_t)(s_frame[0] + s_frame[1] + s_frame[2] + s_frame[3]);
    if (sum != s_frame[4]) {
        s_checksum_error_count++;
        return false;
    }

    raw = imu_raw_i16(s_frame[2], s_frame[3]);
    if (s_frame[1] == IMU_RX_TYPE_GYRO) {
        s_yaw_rate_dps = ((float)raw / IMU_RAW_FULL_SCALE) * IMU_GYRO_FULL_SCALE_DPS;
    } else {
        s_yaw_deg = ((float)raw / IMU_RAW_FULL_SCALE) * IMU_YAW_FULL_SCALE_DEG;
    }

    s_last_frame_ms = Clock_NowMs();
    s_frame_count++;
    s_valid = true;

    return true;
}

static void imu_feed_byte(uint8_t byte)
{
    if (s_frame_len < IMU_RX_FRAME_LEN) {
        s_frame[s_frame_len] = byte;
        s_frame_len++;
    }

    if (s_frame_len < IMU_RX_FRAME_LEN) {
        return;
    }

    if (imu_try_frame()) {
        s_frame_len = 0u;
        return;
    }

    memmove(&s_frame[0], &s_frame[1], IMU_RX_FRAME_LEN - 1u);
    s_frame_len = IMU_RX_FRAME_LEN - 1u;
}

/* ---- 指令组包 ----------------------------------------------------------- */

static bool imu_send_frame(uint8_t reg, uint8_t data_lo, uint8_t data_hi)
{
    const uint8_t frame[IMU_TX_FRAME_LEN] = {
        IMU_TX_HEAD0, IMU_TX_HEAD1, reg, data_lo, data_hi,
    };

    return ImuUart_TryWrite(frame, sizeof(frame));
}

/**
 * @brief 按器件要求的「解锁 -> 写 -> 保存」时序写一个寄存器。
 * @return 三条指令是否都成功写入发送端口。
 * @note  阻塞约 200 ms。以 SAVE 结尾，写器件内部 flash。
 */
static bool imu_write_register(uint8_t reg, uint8_t data_lo, uint8_t data_hi)
{
    if (!imu_send_frame(IMU_REG_KEY, IMU_KEY_DATA_LO, IMU_KEY_DATA_HI)) {
        return false;
    }
    Mspm0Runtime_DelayMs(IMU_CMD_SETTLE_MS);

    if (!imu_send_frame(reg, data_lo, data_hi)) {
        return false;
    }
    Mspm0Runtime_DelayMs(IMU_CMD_SETTLE_MS);

    return imu_send_frame(IMU_REG_SAVE, 0x00u, 0x00u);
}

/* ---- 公开 API ----------------------------------------------------------- */

void Imu_Init(void)
{
    memset(s_frame, 0, sizeof(s_frame));
    s_frame_len = 0u;
    s_yaw_deg = 0.0f;
    s_yaw_rate_dps = 0.0f;
    s_last_frame_ms = 0u;
    s_frame_count = 0u;
    s_checksum_error_count = 0u;
    s_valid = false;
}

void Imu_Update(void)
{
    uint8_t chunk[IMU_DRAIN_CHUNK];
    uint32_t read_count = 0u;
    uint32_t index = 0u;

    do {
        read_count = ImuUart_Read(chunk, sizeof(chunk));
        for (index = 0u; index < read_count; index++) {
            imu_feed_byte(chunk[index]);
        }
    } while (read_count == sizeof(chunk));
}

void Imu_GetSnapshot(Imu_Snapshot_t *out)
{
    if (out == NULL) {
        return;
    }

    out->yaw_deg = s_yaw_deg;
    out->yaw_rate_dps = s_yaw_rate_dps;
    out->valid = s_valid;
    out->age_ms = (s_valid != false) ? (Clock_NowMs() - s_last_frame_ms) : 0u;
}

void Imu_GetDiag(Imu_Diag_t *out)
{
    if (out == NULL) {
        return;
    }

    out->frame_count = s_frame_count;
    out->checksum_error_count = s_checksum_error_count;
    out->rx_overflow_count = ImuUart_GetRxOverflowCount();
}

bool Imu_ZeroYaw(void)
{
    return imu_write_register(IMU_REG_YAW_ZERO, 0x00u, 0x00u);
}

bool Imu_SetOutputRate(Imu_OutputRate_t rate)
{
    /* 器件 RRATE 寄存器编码，下标与 Imu_OutputRate_t 一一对应。
     * 只增不改：新档追加在末尾，否则既有取值会静默映射到别的编码上。 */
    static const uint8_t rate_code[] = {
        0x06u, /* IMU_OUTPUT_RATE_10_HZ */
        0x08u, /* IMU_OUTPUT_RATE_50_HZ */
        0x09u, /* IMU_OUTPUT_RATE_100_HZ */
        0x0Bu, /* IMU_OUTPUT_RATE_200_HZ */
        0x0Du, /* IMU_OUTPUT_RATE_500_HZ */
    };

    if ((uint32_t)rate >= (sizeof(rate_code) / sizeof(rate_code[0]))) {
        return false;
    }

    return imu_write_register(IMU_REG_RRATE, rate_code[rate], 0x00u);
}
