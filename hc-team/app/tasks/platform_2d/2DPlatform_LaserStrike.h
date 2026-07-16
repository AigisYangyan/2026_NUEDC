/**
 * @file    2DPlatform_LaserStrike.h
 * @brief   二维平台激光打击模块对外接口定义
 *
 * 本模块对外暴露二维平台激光打击控制的初始化、进入退出与周期运行接口。
 *
 * 功能范围：
 * - 初始化二维平台串级 PD 跟踪模块的内部状态
 * - 提供 10ms 外环更新与 5ms 内环控制入口
 * - 提供 DEBUG_Smooth 单轴 EMM42 机械平顺性 / 固件 PID 调参测试入口
 *
 * 设计约定：
 * - 本模块依赖视觉坐标模块提供最新目标坐标
 * - 本模块依赖 EMM42 步进电机传输层执行实际轴控制
 * - 跟踪链路采用“外环坐标 PD + 内环速度模式 PD（伪反馈平滑）”
 * - 本模块只负责上层跟踪策略，不负责底层串口传输细节
 * - DEBUG_Smooth 采用 5ms 控制 + 10ms 遥测节拍
 * - Stepper_X / Stepper_Y 采用单轴独立 PID 调参，不做双轴同时联动调参
 * - Emm 固件口径下，VOFA 中 `speed` 使用上层 RPM（0~100），驱动层发协议前会内部换算为 0.1RPM；`accel` 单位为 0~255 档位
 */

#ifndef PLATFORM_2D_LASER_STRIKE_H
#define PLATFORM_2D_LASER_STRIKE_H

#ifdef __cplusplus
extern "C" {
#endif

    /* ---- 视觉跟踪接口 ------------------------------------------------------- */

    /** 初始化视觉跟踪控制器。 */
    void VisionHdl_Init(void);

    /** 进入二维平台串级控制运行态。 */
    void VisionHdl_Enter(void);

    /** 退出二维平台串级控制运行态。 */
    void VisionHdl_Exit(void);

    /** 视觉跟踪 10ms 外环更新入口。 */
    void VisionHdl_Run10ms(void);

    /** 二维平台 5ms 内环控制入口。 */
    void VisionHdl_Control5ms(void);

    /** 二维平台 10ms 遥测入口。 */
    void VisionHdl_Telemetry10ms(void);

    /* ---- 单轴 Stepper 调参接口 --------------------------------------------- */

    /** 进入 Stepper_X 单轴 PID 调参测试。 */
    void StepperTestX_Enter(void);

    /** 退出 Stepper_X 单轴 PID 调参测试。 */
    void StepperTestX_Exit(void);

    /** 进入 Stepper_Y 单轴 PID 调参测试。 */
    void StepperTestY_Enter(void);

    /** 退出 Stepper_Y 单轴 PID 调参测试。 */
    void StepperTestY_Exit(void);

    /* ---- DEBUG_Smooth 接口 -------------------------------------------------- */

    /**
     * @brief DEBUG_Smooth 模块初始化
     * @note  重置单轴调参测试台状态机、PID 参数镜像与遥测通道
     */
    void DebugSmooth_Init(void);

    /**
     * @brief 进入 DEBUG_Smooth 测试
     * @note  建立 VOFA profile、打开步进总线控制门控并让双轴先回到 0 RPM
     */
    void DebugSmooth_Enter(void);

    /**
     * @brief 退出 DEBUG_Smooth 测试
     * @note  双轴停机并冲刷控制链路，最后关闭控制门控
     */
    void DebugSmooth_Exit(void);

    /**
     * @brief DEBUG_Smooth 5ms 控制周期
     * @note  推进“急停循环 / 手动保持 / 换向压力”状态机，并按需一次性下发 EMM42 固件 PID
     */
    void DebugSmooth_Control5ms(void);

    /**
     * @brief DEBUG_Smooth 10ms 遥测周期
     * @note  输出当前命令速度、PID 镜像、状态码与累计错误到 VOFA
     */
    void DebugSmooth_Telemetry10ms(void);

    /* ---- DEBUG_Vision_data 接口 --------------------------------------------- */

    /**
     * @brief DEBUG_Vision_data 模块初始化
     * @note  复位运行态遥测变量，不触发控制动作
     */
    void DebugVisionData_Init(void);

    /**
     * @brief 进入 DEBUG_Vision_data 测试
     * @note  建立 VOFA profile，输出视觉误差与帧间隔
     */
    void DebugVisionData_Enter(void);

    /**
     * @brief 退出 DEBUG_Vision_data 测试
     * @note  清空 VOFA profile，避免污染其它 DEBUG 任务
     */
    void DebugVisionData_Exit(void);

    /**
     * @brief DEBUG_Vision_data 10ms 遥测周期
     * @note  输出 pixel_err_X/pixel_err_Y/frame_dt_ms/status 到 VOFA
     */
    void DebugVisionData_Telemetry10ms(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_2D_LASER_STRIKE_H */
