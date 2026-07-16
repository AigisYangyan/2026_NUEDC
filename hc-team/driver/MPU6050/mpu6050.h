/**
 * @file    mpu6050.h
 * @brief   MPU6050 六轴 IMU 驱动模块对外接口定义
 *
 * @details
 * 模块职责：
 * - 通过 I2C 对 MPU6050 进行初始化、原始数据读取与单位换算
 * - 在读取加速度/陀螺仪原始数据基础上，提供基于卡尔曼滤波的横滚/俯仰角度解算
 * - 作为 IMU 执行层（寄存器读写 + 物理量换算 + 姿态融合），为上层应用提供统一数据接口
 *
 * 设计约定：
 * 1. 底层 I2C 绑定私有到 SysConfig I2C_IMU_INST，使用阻塞 DriverLib 事务
 * 2. 读 API 直接写入 MPU6050_T 实例，不返回错误码；底层通信失败时数据字段保持上一次值
 * 3. 加速度量程默认 ±2g (FS_SEL = 0)，陀螺仪量程默认 ±250°/s (FS_SEL = 0)
 * 4. 本模块仅支持单一 MPU6050 设备，多实例时需要扩展通道/地址配置
 * 5. 卡尔曼角度解算依赖 MPU6050_Read_All()，单独调用加速度/陀螺仪接口不会更新角度
 *
 * 单位与口径：
 * - Ax / Ay / Az          : 单位 g    (1 g = 9.8 m/s^2)
 * - Gx / Gy / Gz          : 单位 °/s
 * - Temperature           : 单位 °C
 * - KalmanAngleX (Roll)   : 单位 °
 * - KalmanAngleY (Pitch)  : 单位 °
 *
 * 依赖：
 * - SYSCFG 中已配置 I2C_MPU6050 外设 (见 board.syscfg)
 * - SysTick/runtime 已初始化 (用于 dt 计算)
 *
 * 使用方式：
 * 1. 先调用 MPU6050_Init()
 * 2. 周期任务中调用 MPU6050_Read_All()（或单独调用 Read_Accel / Read_Gyro / Read_Temp）
 * 3. 通过 g_tMpu6050 读取最新数据
 *
 * 注意：
 * - MPU6050 7-bit 器件地址由 AD0 引脚决定 (AD0=0 -> 0x68, AD0=1 -> 0x69)
 * - 本模块不保证线程安全；若在中断与主循环中同时调用，需自行加保护
 */

#ifndef __MPU6050_H__
#define __MPU6050_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 类型定义 ----------------------------------------------------------- */

/**
 * @brief MPU6050 初始化/底层通信状态
 * @note  0 表示成功；负值表示失败原因（WHO_AM_I 失败用 NOT_READY）
 */
typedef enum {
    MPU6050_OK = 0,
    MPU6050_ERR_UNKNOWN = -1,
    MPU6050_ERR_INVALID = -2,
    MPU6050_ERR_NULL_PTR = -3,
    MPU6050_ERR_NOT_READY = -10,
    MPU6050_ERR_TIMEOUT = -12,
} Mpu6050_Status_e;

/** 卡尔曼滤波器状态 (用于单轴角度估计) */
typedef struct {
    double Q_angle;     /**< 过程噪声协方差 - 角度       */
    double Q_bias;      /**< 过程噪声协方差 - 偏置       */
    double R_measure;   /**< 测量噪声协方差             */
    double angle;       /**< 估计角度 (°)               */
    double bias;        /**< 估计偏置 (°/s)             */
    double P[2][2];     /**< 误差协方差矩阵             */
} Kalman_T;

/** MPU6050 实例：硬件绑定 + 运行时状态 */
typedef struct {
    /* 硬件配置 (初始化时设定，之后只读) */
    uint8_t           dev_addr;    /**< 7-bit 设备地址      */

    /* 原始数据 (补码整型) */
    int16_t Accel_X_RAW;
    int16_t Accel_Y_RAW;
    int16_t Accel_Z_RAW;
    int16_t Gyro_X_RAW;
    int16_t Gyro_Y_RAW;
    int16_t Gyro_Z_RAW;

    /* 物理量 */
    double  Ax;                  /**< X 轴加速度 (g)     */
    double  Ay;                  /**< Y 轴加速度 (g)     */
    double  Az;                  /**< Z 轴加速度 (g)     */
    double  Gx;                  /**< X 轴角速度 (°/s)   */
    double  Gy;                  /**< Y 轴角速度 (°/s)   */
    double  Gz;                  /**< Z 轴角速度 (°/s)   */
    float   Temperature;         /**< 芯片温度 (°C)      */

    /* 卡尔曼融合角度 */
    double  KalmanAngleX;        /**< 横滚角 Roll (°)    */
    double  KalmanAngleY;        /**< 俯仰角 Pitch (°)   */
} MPU6050_T;

/* ---- 全局实例 ----------------------------------------------------------- */

extern MPU6050_T g_tMpu6050;

/* ---- 公开 API ----------------------------------------------------------- */

/**
 * @brief 初始化 MPU6050 子系统 (在 SysInit 中调用一次)
 * @return MPU6050_OK 成功；其他值表示失败 (WHO_AM_I 校验失败或 I2C 通信异常)
 */
Mpu6050_Status_e MPU6050_Init(void);

/** 读取加速度原始值并换算为 g */
void MPU6050_Read_Accel(MPU6050_T *p);

/** 读取陀螺仪原始值并换算为 °/s */
void MPU6050_Read_Gyro(MPU6050_T *p);

/** 读取温度并换算为 °C */
void MPU6050_Read_Temp(MPU6050_T *p);

/** 一次性读取 14 字节 (Accel + Temp + Gyro) 并更新卡尔曼角度 */
void MPU6050_Read_All(MPU6050_T *p);

/** 卡尔曼单轴角度估计 (供 Read_All 内部使用，也对外公开) */
double Kalman_GetAngle(Kalman_T *k, double newAngle, double newRate, double dt);

#ifdef __cplusplus
}
#endif

#endif /* __MPU6050_H__ */
