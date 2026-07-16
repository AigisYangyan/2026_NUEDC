/**
 * @file    mpu6050.c
 * @brief   MPU6050 六轴 IMU 驱动模块实现
 *
 * 本文件实现 MPU6050 的基础驱动逻辑，作为 IMU 执行层
 * （寄存器访问 + 物理量换算 + 卡尔曼姿态融合）。
 *
 * 功能范围：
 * - WHO_AM_I 校验、唤醒、采样率/量程寄存器初始化
 * - 读取加速度 / 陀螺仪 / 温度原始值并换算为工程量
 * - 基于卡尔曼滤波的 Roll/Pitch 角度估计
 *
 * 不负责的内容：
 * - DMP 数字运动处理 (需加载运动固件，当前实现未启用)
 * - 外部磁力计 (AK8963) / 辅助 I2C 桥接
 * - 多实例设备管理
 *
 * 实现说明：
 * 1. 使用“全局实例 + Init 填充硬件绑定”方式管理 MPU6050 的 I2C 资源
 * 2. 底层 I2C 走 SysConfig I2C_IMU_INST + 阻塞 DriverLib 事务
 * 3. 卡尔曼滤波采用经典二维状态 (angle, bias)，实现来自 TKJElectronics KalmanFilter
 *
 * 硬件绑定项：
 * - I2C_IMU_INST (I2C0, 400 kHz)
 * - MPU6050 7-bit 设备地址 (默认 0x68, AD0 接地时)
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "driver/MPU6050/mpu6050.h"

#include "driver/clock/clock.h"
#include "ti_msp_dl_config.h"

#include <math.h>
#include <ti/driverlib/dl_i2c.h>

/* ---- 静态配置 ----------------------------------------------------------- */

#define MPU6050_DEV_ADDR_7BIT   0x68u   /* AD0=0 时的 7-bit 地址；AD0=1 时为 0x69 */
#define MPU6050_WHO_AM_I_VAL    0x68u
#define MPU6050_I2C_TIMEOUT_LOOPS 50000u

/* 寄存器地址 */
#define REG_SMPLRT_DIV          0x19u
#define REG_GYRO_CONFIG         0x1Bu
#define REG_ACCEL_CONFIG        0x1Cu
#define REG_ACCEL_XOUT_H        0x3Bu
#define REG_TEMP_OUT_H          0x41u
#define REG_GYRO_XOUT_H         0x43u
#define REG_PWR_MGMT_1          0x6Bu
#define REG_WHO_AM_I            0x75u

/* 量程换算常量 (FS_SEL = 0) */
#define ACCEL_LSB_PER_G         16384.0
#define GYRO_LSB_PER_DPS        131.0
#define ACCEL_Z_CORRECTOR       14418.0

#define RAD_TO_DEG              57.295779513082320876798154814105

/* ---- 全局实例 ----------------------------------------------------------- */

MPU6050_T g_tMpu6050 = {
    .dev_addr = MPU6050_DEV_ADDR_7BIT,
};

/* ---- 静态卡尔曼滤波状态 ------------------------------------------------- */

static Kalman_T s_tKalmanX = {
    .Q_angle   = 0.001,
    .Q_bias    = 0.003,
    .R_measure = 0.03,
};

static Kalman_T s_tKalmanY = {
    .Q_angle   = 0.001,
    .Q_bias    = 0.003,
    .R_measure = 0.03,
};

static uint32_t s_tick_last_ms = 0u;

/* ---- 静态辅助函数 ------------------------------------------------------- */

static uint32_t mpu_tick_ms(void)
{
    return Clock_NowMs();
}

static Mpu6050_Status_e mpu_i2c_wait_not_busy(void)
{
    uint32_t timeout = MPU6050_I2C_TIMEOUT_LOOPS;

    while ((DL_I2C_getControllerStatus(I2C_IMU_INST) &
            DL_I2C_CONTROLLER_STATUS_BUSY) != 0u) {
        if (timeout == 0u) {
            return MPU6050_ERR_TIMEOUT;
        }
        timeout--;
    }
    return MPU6050_OK;
}

static Mpu6050_Status_e mpu_i2c_write(uint8_t addr7,
                                const uint8_t *p_data,
                                uint16_t len)
{
    uint16_t i = 0u;
    uint32_t timeout_fifo;
    Mpu6050_Status_e wait_err;

    if ((p_data == NULL) || (len == 0u)) {
        return MPU6050_ERR_NULL_PTR;
    }

    wait_err = mpu_i2c_wait_not_busy();
    if (wait_err != MPU6050_OK) {
        return wait_err;
    }

    while ((i < len) &&
           (DL_I2C_isControllerTXFIFOFull(I2C_IMU_INST) == false)) {
        DL_I2C_transmitControllerData(I2C_IMU_INST, p_data[i]);
        i++;
    }

    DL_I2C_startControllerTransfer(I2C_IMU_INST,
                                   (uint32_t)addr7,
                                   DL_I2C_CONTROLLER_DIRECTION_TX,
                                   len);

    while (i < len) {
        timeout_fifo = MPU6050_I2C_TIMEOUT_LOOPS;
        while (DL_I2C_isControllerTXFIFOFull(I2C_IMU_INST) != false) {
            if (timeout_fifo == 0u) {
                return MPU6050_ERR_TIMEOUT;
            }
            timeout_fifo--;
        }
        DL_I2C_transmitControllerData(I2C_IMU_INST, p_data[i]);
        i++;
    }

    wait_err = mpu_i2c_wait_not_busy();
    if (wait_err != MPU6050_OK) {
        return wait_err;
    }

    if ((DL_I2C_getControllerStatus(I2C_IMU_INST) &
         DL_I2C_CONTROLLER_STATUS_ERROR) != 0u) {
        return MPU6050_ERR_UNKNOWN;
    }

    return MPU6050_OK;
}

static Mpu6050_Status_e mpu_write_reg(const MPU6050_T *p, uint8_t reg, uint8_t val)
{
    uint8_t buf[2];

    buf[0] = reg;
    buf[1] = val;
    return mpu_i2c_write(p->dev_addr, buf, 2u);
}

static Mpu6050_Status_e mpu_read_regs(const MPU6050_T *p, uint8_t reg, uint8_t *buf, uint16_t len)
{
    Mpu6050_Status_e res;
    uint16_t i;
    uint32_t timeout;

    if ((buf == NULL) || (len == 0u)) {
        return MPU6050_ERR_NULL_PTR;
    }

    res = mpu_i2c_write(p->dev_addr, &reg, 1u);
    if (res != MPU6050_OK) {
        return res;
    }

    DL_I2C_startControllerTransfer(I2C_IMU_INST,
                                   (uint32_t)p->dev_addr,
                                   DL_I2C_CONTROLLER_DIRECTION_RX,
                                   len);

    for (i = 0u; i < len; i++) {
        timeout = MPU6050_I2C_TIMEOUT_LOOPS;
        while (DL_I2C_isControllerRXFIFOEmpty(I2C_IMU_INST) != false) {
            if (timeout == 0u) {
                return MPU6050_ERR_TIMEOUT;
            }
            timeout--;
        }
        buf[i] = DL_I2C_receiveControllerData(I2C_IMU_INST);
    }

    res = mpu_i2c_wait_not_busy();
    if (res != MPU6050_OK) {
        return res;
    }

    return MPU6050_OK;
}

/* ---- 公开 API ----------------------------------------------------------- */

/**
 * @brief MPU6050 子系统初始化
 * @note  绑定设备地址、校验 WHO_AM_I、唤醒并配置量程寄存器
 */
Mpu6050_Status_e MPU6050_Init(void)
{
    MPU6050_T *p = &g_tMpu6050;
    uint8_t who = 0u;
    Mpu6050_Status_e err;

    p->dev_addr = MPU6050_DEV_ADDR_7BIT;

    /* I2C 控制器已由 SYSCFG_DL_init() / SYSCFG_DL_I2C_IMU_init 完成 */
    if ((DL_I2C_getControllerStatus(I2C_IMU_INST) &
         DL_I2C_CONTROLLER_STATUS_BUSY_BUS) == 0u) {
        DL_I2C_enableController(I2C_IMU_INST);
    }

    /* 校验 WHO_AM_I 寄存器，返回 0x68 表示通信正常 */
    err = mpu_read_regs(p, REG_WHO_AM_I, &who, 1u);
    if (err != MPU6050_OK) {
        return err;
    }
    if (who != MPU6050_WHO_AM_I_VAL) {
        return MPU6050_ERR_NOT_READY;
    }

    /* 唤醒设备 (清零 PWR_MGMT_1) */
    (void)mpu_write_reg(p, REG_PWR_MGMT_1, 0x00u);
    /* 采样率分频: Fs = 1kHz / (1 + 7) = 125Hz */
    (void)mpu_write_reg(p, REG_SMPLRT_DIV, 0x07u);
    /* 加速度量程: ±2g  (FS_SEL = 0) */
    (void)mpu_write_reg(p, REG_ACCEL_CONFIG, 0x00u);
    /* 陀螺仪量程: ±250°/s (FS_SEL = 0) */
    (void)mpu_write_reg(p, REG_GYRO_CONFIG, 0x00u);

    /* 初始化卡尔曼时间基 */
    s_tick_last_ms = mpu_tick_ms();
    return MPU6050_OK;
}

/**
 * @brief 读取加速度并换算为 g (±2g 量程下，LSB = 1/16384 g)
 */
void MPU6050_Read_Accel(MPU6050_T *p)
{
    uint8_t buf[6];
    if (mpu_read_regs(p, REG_ACCEL_XOUT_H, buf, 6u) != MPU6050_OK) {
        return;
    }

    p->Accel_X_RAW = (int16_t)((buf[0] << 8) | buf[1]);
    p->Accel_Y_RAW = (int16_t)((buf[2] << 8) | buf[3]);
    p->Accel_Z_RAW = (int16_t)((buf[4] << 8) | buf[5]);

    p->Ax = p->Accel_X_RAW / ACCEL_LSB_PER_G;
    p->Ay = p->Accel_Y_RAW / ACCEL_LSB_PER_G;
    p->Az = p->Accel_Z_RAW / ACCEL_Z_CORRECTOR;
}

/**
 * @brief 读取陀螺仪并换算为 °/s (±250°/s 量程下，LSB = 1/131 °/s)
 */
void MPU6050_Read_Gyro(MPU6050_T *p)
{
    uint8_t buf[6];
    if (mpu_read_regs(p, REG_GYRO_XOUT_H, buf, 6u) != MPU6050_OK) {
        return;
    }

    p->Gyro_X_RAW = (int16_t)((buf[0] << 8) | buf[1]);
    p->Gyro_Y_RAW = (int16_t)((buf[2] << 8) | buf[3]);
    p->Gyro_Z_RAW = (int16_t)((buf[4] << 8) | buf[5]);

    p->Gx = p->Gyro_X_RAW / GYRO_LSB_PER_DPS;
    p->Gy = p->Gyro_Y_RAW / GYRO_LSB_PER_DPS;
    p->Gz = p->Gyro_Z_RAW / GYRO_LSB_PER_DPS;
}

/**
 * @brief 读取温度并换算为 °C
 * @note  数据手册: T(°C) = TEMP_OUT / 340 + 36.53
 */
void MPU6050_Read_Temp(MPU6050_T *p)
{
    uint8_t buf[2];
    if (mpu_read_regs(p, REG_TEMP_OUT_H, buf, 2u) != MPU6050_OK) {
        return;
    }
    int16_t temp = (int16_t)((buf[0] << 8) | buf[1]);
    p->Temperature = (float)((float)temp / 340.0f + 36.53f);
}

/**
 * @brief 一次性读取 14 字节 (Accel + Temp + Gyro) 并更新卡尔曼角度
 * @note  推荐上层周期任务 (10ms / 20ms) 调用本接口；单次 I2C 事务更高效且数据同步
 */
void MPU6050_Read_All(MPU6050_T *p)
{
    uint8_t buf[14];
    if (mpu_read_regs(p, REG_ACCEL_XOUT_H, buf, 14u) != MPU6050_OK) {
        return;
    }

    int16_t temp = 0;
    p->Accel_X_RAW = (int16_t)((buf[0]  << 8) | buf[1]);
    p->Accel_Y_RAW = (int16_t)((buf[2]  << 8) | buf[3]);
    p->Accel_Z_RAW = (int16_t)((buf[4]  << 8) | buf[5]);
    temp           = (int16_t)((buf[6]  << 8) | buf[7]);
    p->Gyro_X_RAW  = (int16_t)((buf[8]  << 8) | buf[9]);
    p->Gyro_Y_RAW  = (int16_t)((buf[10] << 8) | buf[11]);
    p->Gyro_Z_RAW  = (int16_t)((buf[12] << 8) | buf[13]);

    p->Ax = p->Accel_X_RAW / ACCEL_LSB_PER_G;
    p->Ay = p->Accel_Y_RAW / ACCEL_LSB_PER_G;
    p->Az = p->Accel_Z_RAW / ACCEL_Z_CORRECTOR;
    p->Temperature = (float)((float)temp / 340.0f + 36.53f);
    p->Gx = p->Gyro_X_RAW / GYRO_LSB_PER_DPS;
    p->Gy = p->Gyro_Y_RAW / GYRO_LSB_PER_DPS;
    p->Gz = p->Gyro_Z_RAW / GYRO_LSB_PER_DPS;

    /* ---- 卡尔曼角度解算 ------------------------------------------------ */
    uint32_t now_ms = mpu_tick_ms();
    double dt = (double)(now_ms - s_tick_last_ms) / 1000.0;
    s_tick_last_ms = now_ms;

    double roll;
    double roll_sqrt = sqrt((double)p->Accel_X_RAW * p->Accel_X_RAW +
                            (double)p->Accel_Z_RAW * p->Accel_Z_RAW);
    if (roll_sqrt != 0.0) {
        roll = atan((double)p->Accel_Y_RAW / roll_sqrt) * RAD_TO_DEG;
    } else {
        roll = 0.0;
    }

    double pitch = atan2(-(double)p->Accel_X_RAW, (double)p->Accel_Z_RAW) * RAD_TO_DEG;
    if ((pitch < -90.0 && p->KalmanAngleY >  90.0) ||
        (pitch >  90.0 && p->KalmanAngleY < -90.0)) {
        s_tKalmanY.angle = pitch;
        p->KalmanAngleY  = pitch;
    } else {
        p->KalmanAngleY = Kalman_GetAngle(&s_tKalmanY, pitch, p->Gy, dt);
    }

    if (fabs(p->KalmanAngleY) > 90.0) {
        p->Gx = -p->Gx;
    }
    p->KalmanAngleX = Kalman_GetAngle(&s_tKalmanX, roll, p->Gx, dt);
}

/**
 * @brief 卡尔曼单轴角度估计
 * @param k         卡尔曼状态 (每个轴一份)
 * @param newAngle  加速度计测量到的绝对角度 (°)
 * @param newRate   陀螺仪测量到的角速度    (°/s)
 * @param dt        自上次更新以来的时间间隔 (s)
 * @return          融合后的角度 (°)
 */
double Kalman_GetAngle(Kalman_T *k, double newAngle, double newRate, double dt)
{
    double rate = newRate - k->bias;
    k->angle += dt * rate;

    k->P[0][0] += dt * (dt * k->P[1][1] - k->P[0][1] - k->P[1][0] + k->Q_angle);
    k->P[0][1] -= dt * k->P[1][1];
    k->P[1][0] -= dt * k->P[1][1];
    k->P[1][1] += k->Q_bias * dt;

    double S = k->P[0][0] + k->R_measure;
    double K[2];
    K[0] = k->P[0][0] / S;
    K[1] = k->P[1][0] / S;

    double y = newAngle - k->angle;
    k->angle += K[0] * y;
    k->bias  += K[1] * y;

    double P00 = k->P[0][0];
    double P01 = k->P[0][1];

    k->P[0][0] -= K[0] * P00;
    k->P[0][1] -= K[0] * P01;
    k->P[1][0] -= K[1] * P00;
    k->P[1][1] -= K[1] * P01;

    return k->angle;
}
