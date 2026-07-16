#include "IMU.h"
#include "driver/board_uart/imu_uart.h"
#include "driver/clock/clock.h"
#include "driver/mspm0_runtime/mspm0_runtime.h"
#include "ti_msp_dl_config.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* 定义 delay_cycles 宏 */
#ifndef delay_cycles
#define delay_cycles(x) DL_Common_delayCycles(x)
#endif


/* ---------- 环形缓冲 / RX ring buffer ---------- */
static volatile uint8_t  s_rx_buffer[IMU_UART_RX_BUF_SIZE];
static volatile uint16_t s_rx_write_index = 0;  /* 写入位置 / write index */
static volatile uint16_t s_rx_read_index  = 0;  /* 读取位置 / read index */

/* ---------- 角速度积分状态 / Gyro integration state ---------- */
static float    s_integrated_yaw_deg    = 0.0f;  /* 积分得到的偏航角 (多圈) */
static float    s_yaw_reference_deg     = 0.0f;  /* 相对角度零点 */
static float    s_gyro_bias_rad         = 0.0f;  /* Z 轴角速度零偏 (rad/s) */
static uint8_t  s_gyro_bias_valid       = 0u;    /* 零偏是否有效 */
static uint8_t  s_yaw_synced_from_euler = 0u;    /* 是否已与欧拉角同步 */
static uint8_t  s_integrator_tick_init  = 0u;    /* 是否已建立时间基准 */
static uint32_t s_last_integrate_tick   = 0u;    /* 上次积分时刻 (ms) */

/* 死区与转换常量 / Dead-zone and conversion helpers */
static const float GYRO_DEADBAND_RAD_S = 0.002f;                 /* 约 0.11°/s */
static const float RAD2DEG             = 57.29577951308232f;

/* ---------- 内部状态/ Internal cached state ---------- */
static volatile float s_ax = 0.0f, s_ay = 0.0f, s_az = 0.0f;
static volatile float s_gx = 0.0f, s_gy = 0.0f, s_gz = 0.0f;
static volatile float s_mx = 0.0f, s_my = 0.0f, s_mz = 0.0f;
static volatile float s_roll = 0.0f, s_pitch = 0.0f, s_yaw = 0.0f;
static volatile float s_q0 = 0.0f, s_q1 = 0.0f, s_q2 = 0.0f, s_q3 = 0.0f;
static volatile float s_height = 0.0f, s_temperature = 0.0f, s_pressure = 0.0f, s_pressure_contrast = 0.0f;
static volatile int   s_version_high = -1, s_version_mid = 0, s_version_low = 0;
static volatile uint8_t s_last_rx_function = 0;
static volatile int16_t s_last_rx_state = 0;

static float _wrap_deg180(float deg)
{
    if (deg >= 180.0f || deg < -180.0f) {
        deg = fmodf(deg, 360.0f);
        if (deg >= 180.0f) {
            deg -= 360.0f;
        } else if (deg < -180.0f) {
            deg += 360.0f;
        }
    }
    return deg;
}

static void _gyro_integrator_step(float gz_rad, float dt_sec)
{
    if (dt_sec <= 0.0f) {
        return;
    }

    float gz = gz_rad - (s_gyro_bias_valid ? s_gyro_bias_rad : 0.0f);

    if (fabsf(gz) < GYRO_DEADBAND_RAD_S) {
        gz = 0.0f;
    }

    s_integrated_yaw_deg += gz * dt_sec * RAD2DEG;
}

static void _gyro_integrator_update_auto(void)
{
    uint32_t now_tick;

    now_tick = Clock_NowMs();

    if (!s_integrator_tick_init) {
        s_last_integrate_tick  = now_tick;
        s_integrator_tick_init = 1u;
        return;
    }

    uint32_t delta_ms = now_tick - s_last_integrate_tick; /* 无符号差值自动处理溢出 */
    s_last_integrate_tick = now_tick;

    if (delta_ms == 0u || delta_ms > 100u) {
        return;
    }

    float dt = (float)delta_ms * 0.001f;
    _gyro_integrator_step(s_gz, dt);
}

static int _gyro_calibrate_bias(uint16_t sample_count, uint16_t interval_ms)
{
    if (sample_count == 0u) {
        return -1;
    }

    float yaw_snapshot = s_integrated_yaw_deg;
    float sum = 0.0f;
    
    /* 添加超时保护（最多等待 3 秒） */
    uint32_t start_tick = Clock_NowMs();
    const uint32_t timeout_ms = 3000;  /* 3 秒超时 */
    
    for (uint16_t i = 0; i < sample_count; ++i) {
        /* 检查超时 */
        uint32_t now_tick = Clock_NowMs();
        if ((now_tick - start_tick) > timeout_ms) {
            printf("IMU: Calibration timeout (no data received)\r\n");
            return -2;  /* 超时错误 */
        }
        
        IMU_UART_Process();
        sum += s_gz;
        if (interval_ms > 0u) {
            // MSPM0: delay_cycles (32MHz, 1ms = 32000 cycles)
            delay_cycles(interval_ms * 32000);
        }
    }

    s_gyro_bias_rad   = sum / (float)sample_count;
    s_gyro_bias_valid = 1u;
    s_integrated_yaw_deg = yaw_snapshot;
    s_integrator_tick_init = 0u;
    return 0;
}

static inline uint16_t _rxbuf_next(uint16_t index)
{
    return (uint16_t)((index + 1u) % IMU_UART_RX_BUF_SIZE);
}

static inline int _rxbuf_is_empty(void)
{
    return s_rx_write_index == s_rx_read_index;
}

static inline void _rxbuf_push(uint8_t byte_value)
{
    uint16_t next_index = _rxbuf_next(s_rx_write_index);
    if (next_index == s_rx_read_index) {
        /* 缓冲区满时丢弃最旧数据 / drop oldest byte when buffer is full */
        s_rx_read_index = _rxbuf_next(s_rx_read_index);
    }
    s_rx_buffer[s_rx_write_index] = byte_value;
    s_rx_write_index = next_index;
}

void IMU_UART_RxByte(uint8_t data)
{
    _rxbuf_push(data);
}

static inline int _rxbuf_pop(uint8_t *out_byte)
{
    if (_rxbuf_is_empty()) {
        return -1;
    }
    *out_byte = s_rx_buffer[s_rx_read_index];
    s_rx_read_index = _rxbuf_next(s_rx_read_index);
    return 0;
}

/** 将两个字节转换为 int16 / Convert two bytes to int16 */
static int16_t to_int16(const uint8_t *bytes)
{
    return (int16_t)((bytes[1] << 8) + bytes[0]);
}

/** 将四个字节转换为 float / Convert four bytes to float */
static float to_float(const uint8_t *bytes)
{
    float v;
    memcpy(&v, bytes, sizeof(float));
    return v;
}


/* ---------- 解析数据帧 / Parse one complete frame ---------- */
static void _parse_frame_data(uint8_t frame_function, const uint8_t *frame_data)
{
    if (frame_function == IMU_FUNC_RAW_ACCEL) {
        float accel_ratio = 16.0f / 32767.0f;
        s_ax = to_int16(&frame_data[0])  * accel_ratio;
        s_ay = to_int16(&frame_data[2])  * accel_ratio;
        s_az = to_int16(&frame_data[4])  * accel_ratio;

        float deg_to_rad = 3.14159265358979323846f / 180.0f;
        float gyro_ratio  = (2000.0f / 32767.0f) * deg_to_rad;
        s_gx = to_int16(&frame_data[6])  * gyro_ratio;
        s_gy = to_int16(&frame_data[8])  * gyro_ratio;
        s_gz = to_int16(&frame_data[10]) * gyro_ratio;

        _gyro_integrator_update_auto();

        float mag_ratio = 800.0f / 32767.0f;
        s_mx = to_int16(&frame_data[12]) * mag_ratio;
        s_my = to_int16(&frame_data[14]) * mag_ratio;
        s_mz = to_int16(&frame_data[16]) * mag_ratio;
    } else if (frame_function == IMU_FUNC_EULER) {
        s_roll  = to_float(&frame_data[0]);
        s_pitch = to_float(&frame_data[4]);
        s_yaw   = to_float(&frame_data[8]);

        if (!s_yaw_synced_from_euler) {
            s_integrated_yaw_deg = s_yaw * RAD2DEG;
            s_yaw_synced_from_euler = 1u;
        }
    } else if (frame_function == IMU_FUNC_QUAT) {
        s_q0 = to_float(&frame_data[0]);
        s_q1 = to_float(&frame_data[4]);
        s_q2 = to_float(&frame_data[8]);
        s_q3 = to_float(&frame_data[12]);
    } else if (frame_function == IMU_FUNC_BARO) {
        s_height            = to_float(&frame_data[0]);
        s_temperature       = to_float(&frame_data[4]);
        s_pressure          = to_float(&frame_data[8]);
        s_pressure_contrast = to_float(&frame_data[12]);
    } else if (frame_function == IMU_FUNC_VERSION) {
        s_version_high = frame_data[0];
        s_version_mid  = frame_data[1];
        s_version_low  = frame_data[2];
    } else if (frame_function == IMU_FUNC_RETURN_STATE) {
        s_last_rx_function = frame_data[0];
        s_last_rx_state    = (int16_t)frame_data[1];
    }
}


/* ---------- 帧发送接口 / Command sender ---------- */
int IMU_UART_SendCommand(uint8_t function, const uint8_t *params, uint8_t param_len)
{
    if (param_len > 3 || (param_len > 0 && params == NULL)) {
        return -1;
    }

    uint8_t frame[8] = {FRAME_HEAD1, FRAME_HEAD2, 0, function, 0, 0, 0, 0};

    for (uint8_t i = 0; i < param_len; ++i) {
        frame[4 + i] = params[i];
    }

    uint8_t frame_len = (uint8_t)(4 + param_len + 1);
    frame[2] = frame_len;

    uint8_t checksum = 0;
    for (uint8_t i = 0; i < frame_len - 1; ++i) {
        checksum = (uint8_t)(checksum + frame[i]);
    }
    frame[frame_len - 1] = checksum;

    Send_IMU_Array(frame, frame_len);
    return 0;

}

/**
 * @brief 中断接收入口，将新数据写入环形缓冲
 *        ISR entry to push received bytes into ring buffer
 */
void IMU_UART_RxBytes(volatile uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return;
    for (uint16_t i = 0; i < len; ++i) {
        _rxbuf_push(data[i]);
    }
}


/**
 * @brief 解析环形缓冲中的数据，提取完整帧并更新缓存
 *        Process RX ring buffer, parse frames and update internal cache
 */
void IMU_UART_Process(void)
{
    enum {
        RX_STATE_EXPECT_HEAD1 = 0,
        RX_STATE_EXPECT_HEAD2,
        RX_STATE_EXPECT_LENGTH,
        RX_STATE_EXPECT_FUNCTION,
        RX_STATE_COLLECT_DATA
    };

    static uint8_t  rx_state = RX_STATE_EXPECT_HEAD1;
    static uint8_t  frame_length = 0;
    static uint8_t  frame_function = 0;
    static uint8_t  frame_buffer[64]; /* 数据区 + 校验 / data section + checksum */
    static uint16_t frame_index = 0;

    uint8_t current_byte = 0;

    while (_rxbuf_pop(&current_byte) == 0) {
        switch (rx_state) {
        case RX_STATE_EXPECT_HEAD1:
            rx_state = (current_byte == FRAME_HEAD1) ? RX_STATE_EXPECT_HEAD2 : RX_STATE_EXPECT_HEAD1;
            break;

        case RX_STATE_EXPECT_HEAD2:
            rx_state = (current_byte == FRAME_HEAD2) ? RX_STATE_EXPECT_LENGTH : RX_STATE_EXPECT_HEAD1;
            break;

        case RX_STATE_EXPECT_LENGTH:
            frame_length = current_byte;
            rx_state = RX_STATE_EXPECT_FUNCTION;
            break;

        case RX_STATE_EXPECT_FUNCTION:
            frame_function = current_byte;
            frame_index = 0;
            rx_state = RX_STATE_COLLECT_DATA;
            break;

        case RX_STATE_COLLECT_DATA: {
            uint16_t data_length = (frame_length >= 4) ? (uint16_t)(frame_length - 4) : 0;
            if (data_length == 0 || data_length > sizeof(frame_buffer)) {
                rx_state = RX_STATE_EXPECT_HEAD1;
                break;
            }

            frame_buffer[frame_index++] = current_byte;
            if (frame_index >= data_length) {
                uint8_t calculated_checksum = (uint8_t)(FRAME_HEAD1 + FRAME_HEAD2 + frame_length + frame_function);
                for (uint16_t i = 0; i < data_length - 1; ++i) {
                    calculated_checksum = (uint8_t)(calculated_checksum + frame_buffer[i]);
                }

                uint8_t received_checksum = frame_buffer[data_length - 1];
                if (calculated_checksum == received_checksum) {
                    _parse_frame_data(frame_function, frame_buffer);
                }
                rx_state = RX_STATE_EXPECT_HEAD1;
            }
        } break;

        default:
            rx_state = RX_STATE_EXPECT_HEAD1;
            break;
        }
    }
}

/* ---------------- 读取数据 / Read Data ---------------- */
int IMU_UART_GetAccelerometer(float out[3])
{
    if (!out) return -1;
    out[0] = s_ax; out[1] = s_ay; out[2] = s_az;
    return 0;
}

/**
 * @brief 读取角速度数据（rad/s）
 *        Read angular velocity in rad/s.
 */
int IMU_UART_GetGyroscope(float out[3])
{
    if (!out) return -1;
    out[0] = s_gx; out[1] = s_gy; out[2] = s_gz;
    return 0;
}

/**
 * @brief 读取磁场数据（uT）
 *        Read magnetic field in micro tesla.
 */
int IMU_UART_GetMagnetometer(float out[3])
{
    if (!out) return -1;
    out[0] = s_mx; out[1] = s_my; out[2] = s_mz;
    return 0;
}

/**
 * @brief 读取四元数
 *        Read quaternion (w, x, y, z).
 */
int IMU_UART_GetQuaternion(float out[4])
{
    if (!out) return -1;
    out[0] = s_q0; out[1] = s_q1; out[2] = s_q2; out[3] = s_q3;
    return 0;
}

/**
 * @brief 读取欧拉角（角度）
 *        Read Euler angles in degrees (as reported by the IMU firmware).
 */
int IMU_UART_GetEuler(float out[3])
{
    if (!out) return -1;

    out[0] = s_roll  * RAD2DEG;
    out[1] = s_pitch * RAD2DEG;
    out[2] = s_yaw * RAD2DEG;
    return 0;
}

/**
 * @brief 读取气压相关数据：高度、温度、气压、气压差
 *        Read barometric data: height, temperature, pressure, delta.
 */
int IMU_UART_GetBarometer(float out[4])
{
    if (!out) return -1;
    out[0] = s_height; out[1] = s_temperature; out[2] = s_pressure; out[3] = s_pressure_contrast;
    return 0;
}


/**
 * @brief 读取固件版本字符串
 *        Read firmware version string.
 */
void IMU_UART_GetVersion(void)
{
   if (s_version_high < 0) {
    uint8_t payload[2] = {IMU_FUNC_VERSION, 0x00};
        IMU_UART_SendCommand(IMU_FUNC_REQUEST_DATA, payload, (uint8_t)sizeof(payload));

        for (int i = 0; i < 20; ++i) {
            IMU_UART_Process();
            if (s_version_high >= 0) {
                printf("Version:%d.%d.%d\r\n", s_version_high, s_version_mid, s_version_low);
                return;
            }
            // MSPM0: delay_cycles (32MHz, 5ms = 160000 cycles)
            delay_cycles(160000);
        }
        printf("Version:-1\r\n");
        return;
    }
}

/**
 * @brief 一次性读取全部常用数据
 *        Read all common sensor values at once.
 */
int IMU_UART_GetAll(imu_measurement_t *out)
{
    if (!out) return -1;
    IMU_UART_GetAccelerometer(out->accel);
    IMU_UART_GetGyroscope(out->gyro);
    IMU_UART_GetMagnetometer(out->mag);
    IMU_UART_GetQuaternion(out->quat);
    IMU_UART_GetEuler(out->euler);
    IMU_UART_GetBarometer(out->baro);
    return 0;
}

/* ---------- 清理缓存 / Clear cached auto-reported data ---------- */
void IMU_UART_ClearAutoReportData(void)
{
    s_ax = s_ay = s_az = 0.0f;
    s_gx = s_gy = s_gz = 0.0f;
    s_mx = s_my = s_mz = 0.0f;
    s_roll = s_pitch = s_yaw = 0.0f;
    s_q0 = s_q1 = s_q2 = s_q3 = 0.0f;
    s_height = s_temperature = s_pressure = s_pressure_contrast = 0.0f;
    s_integrated_yaw_deg = 0.0f;
    s_yaw_reference_deg = 0.0f;
    s_gyro_bias_rad = 0.0f;
    s_gyro_bias_valid = 0u;
    s_yaw_synced_from_euler = 0u;
    s_integrator_tick_init = 0u;
}

static int _calibration_with_wait(uint8_t function, const uint8_t *payload, uint8_t payload_len,
                                  const char *label, uint32_t timeout_ms)
{
    s_last_rx_function = 0;
    s_last_rx_state = -1;

    int rc = IMU_UART_SendCommand(function, payload, payload_len);
    if (rc != 0) {
        return rc;
    }

    int result = IMU_UART_WaitCalibration(function, timeout_ms);
    if (!label) {
        label = "unknown";
    }

    if (result == -1) {
        printf("[IMU] Calibration %s timeout\r\n", label);
    } else if (result == 1) {
        printf("[IMU] Calibration %s success\r\n", label);
    } else {
        printf("[IMU] Calibration %s failed (code=%d)\r\n", label, result);
    }

    return result;
}

/*校准API*/
int IMU_UART_CalibrationImu(void)
{
    uint8_t payload[2] = {0x01, 0x5F};
    return _calibration_with_wait(IMU_FUNC_CALIB_IMU, payload, (uint8_t)sizeof(payload), "imu", 7000);
}

int IMU_UART_CalibrationMag(void)
{
    uint8_t payload[2] = {0x01, 0x5F};
    return _calibration_with_wait(IMU_FUNC_CALIB_MAG, payload, (uint8_t)sizeof(payload), "mag", 0);
}

int IMU_UART_CalibrationTemp(float now_temperature)
{
    if (now_temperature > 50.0f || now_temperature < -50.0f) {
        return -1;
    }
    int16_t temperature_raw = (int16_t)(now_temperature * 100.0f);
    uint8_t param_low  = (uint8_t)(temperature_raw & 0xFF);
    uint8_t param_high = (uint8_t)((temperature_raw >> 8) & 0xFF);
    uint8_t payload[3] = {param_low, param_high, 0x5F};
    return _calibration_with_wait(IMU_FUNC_CALIB_TEMP, payload, (uint8_t)sizeof(payload), "temp", 2000);
}

int IMU_UART_ResetUserData(void)
{
    uint8_t payload[2] = {0x01, 0x5F};
    return IMU_UART_SendCommand(IMU_FUNC_RESET_FLASH, payload, (uint8_t)sizeof(payload));
}

int IMU_UART_WaitCalibration(uint8_t function, uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0;
    while (1) {
        IMU_UART_Process();

        if (s_last_rx_function == function) {
            return s_last_rx_state;
        }

        if (timeout_ms != 0 && elapsed_ms >= timeout_ms) {
            return -1;
        }

        // MSPM0: delay_cycles (32MHz, 1ms = 32000 cycles)
        delay_cycles(32000);
        if (timeout_ms != 0) {
            ++elapsed_ms;
        }
    }
}

void Send_IMU_Data(uint8_t Data)
{
    (void)ImuUart_TryWrite(&Data, 1u);
}

void Send_IMU_Array(uint8_t *pData, uint8_t Length)
{
    if ((pData == NULL) || (Length == 0u)) {
        return;
    }

    (void)ImuUart_TryWrite(pData, (uint32_t)Length);
}

void IMU_Update_Yaw_Integration(float dt_sec)
{
    _gyro_integrator_step(s_gz, dt_sec);
    s_integrator_tick_init = 0u;
}

float IMU_Get_User_Yaw(void)
{
    return s_integrated_yaw_deg;
}

void IMU_Reset_User_Yaw(void)
{
    s_integrated_yaw_deg = 0.0f;
    s_yaw_reference_deg  = 0.0f;
    s_yaw_synced_from_euler = 0u;
    s_integrator_tick_init = 0u;
}

void IMU_Reset_Yaw(void)
{
    s_yaw_reference_deg = s_integrated_yaw_deg;
}

float IMU_Get_Relative_Yaw(void)
{
    float relative = s_integrated_yaw_deg - s_yaw_reference_deg;
    return _wrap_deg180(relative);
}

int IMU_Calibrate_Gyro_Offset(void)
{
    printf("IMU: Gyro calibrating...\r\n");
    int rc = _gyro_calibrate_bias(500u, 2u);
    if (rc == 0) {
        printf("IMU: Offset = %.6f rad/s\r\n", s_gyro_bias_rad);
    } else {
        printf("IMU: Gyro calibration failed (rc=%d)\r\n", rc);
    }
    return rc;
}

int IMU_Calibrate_Gyro_Offset_Custom(uint16_t sample_count, uint16_t interval_ms)
{
    int rc = _gyro_calibrate_bias(sample_count, interval_ms);
    if (rc == 0) {
        printf("IMU: Offset = %.6f rad/s\r\n", s_gyro_bias_rad);
    }
    return rc;
}


