/**
 * @file    stepmotor_bus.h
 * @brief   步进电机专用串口总线服务模块对外接口定义
 *
 * 本模块负责 UART_STEPPER_BUS (UART2, 230400) 上的步进电机收发调度。
 *
 * 功能范围：
 * - 初始化步进电机总线的收发状态
 * - 提供 UART 接收 ISR 入口
 * - 提供 5ms 周期服务入口，推进电机应答帧解析与控制帧调度
 * - 提供 DEBUG 旁路开关，供 UART 压测等运行项临时接管通道
 *
 * 设计约定：
 * - 电机控制协议独占 UART2，不再与视觉协议共享
 * - 接收侧先入 FIFO，再由周期任务完成应答过滤
 * - 发送侧区分控制帧与管理帧，并统一走调度发送
 * - 旧名 HorizonBus 已弃用；视觉侧协议拆分到 vision_bus 模块
 */

#ifndef STEPMOTOR_BUS_H
#define STEPMOTOR_BUS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

    /* ---- 共享配置 ---------------------------------------------------------- */

    /* 步进总线 RX 软件环形 FIFO 容量。
     * UART_Stress 压测模式复用同一容量配置，便于统一放大接收缓冲。 */
#define STEPMOTOR_BUS_SHARED_RX_FIFO_SIZE 512u

    /**
     * @brief 步进总线运输层状态（管理帧入队 / 控制帧提交）
     * @note  0 表示成功；BUSY/NOT_READY/INVALID 供调度侧区分
     */
    typedef enum {
        STEPMOTOR_BUS_OK = 0,
        STEPMOTOR_BUS_ERR_INVALID = -2,
        STEPMOTOR_BUS_ERR_NOT_READY = -10,
        STEPMOTOR_BUS_ERR_BUSY = -11,
    } StepmotorBus_Status_e;

     /* ---- 公开 API ----------------------------------------------------------- */

     /* 初始化步进电机总线的 RX/TX 状态。 */
    void StepmotorBus_Init(void);

    /* 供 runtime UART 直接注册的步进电机接收回调。 */
    void StepmotorBus_RxISR(uint8_t data);

    /* 由 5ms 周期任务调用，推进 RX 应答过滤与 TX 调度。 */
    void StepmotorBus_Service5ms(void);

    /* 旁路开关：置 true 后 Service5ms 立即返回，不消费 RX FIFO 也不发 TX。
     * 专供 DEBUG 压测场景使用（如 UART_Stress），避免两个消费者争抢同一条 UART。
     * 注意：旁路仅冻结本模块状态机，不卸载 RX ISR 回调；调用方如需
     * 接管单字节流，需自行调用 Mspm0Runtime_SetStepmotorRxCallback 覆盖。 */
    void StepmotorBus_SetBypass(bool bypass);

    /**
     * @brief 控制帧门控开关
     * @param enable true=允许控制帧；false=拒绝控制帧
     * @note  仅影响控制类帧（速度/位置），不影响管理帧
     */
    void StepmotorBus_SetControlGate(bool enable);

    /**
     * @brief 复位控制链路诊断计数
     * @note  同时清零“累计错误计数”和“最近返回码”
     */
    void StepmotorBus_ResetDiagCounters(void);

    /**
     * @brief 获取控制链路累计错误计数
     * @return 门控拒绝、参数非法、发送失败等累计次数
     */
    uint32_t StepmotorBus_GetControlErrorCount(void);

    /**
     * @brief 获取最近一次电机返回码
     * @return 电机协议返回码
     */
    uint8_t StepmotorBus_GetLastReturnCode(void);

    /**
     * @brief 清空待发送控制帧槽位
     * @note  不影响当前已在 DMA 发送中的帧
     */
    void StepmotorBus_ClearControlFrames(void);

    /**
     * @brief 查询控制链路是否空闲
     * @return true=无待发控制帧且 TX/DMA 都不忙
     */
    bool StepmotorBus_IsControlPathIdle(void);

    /**
     * @brief 获取最近一次成功解析的“读速度 0x35”反馈帧的速度
     * @param axis_id 电机轴地址（1=Y，2=X）
     * @return 带符号整数 RPM：按当前上层显示口径，将原始字段缩放为 RPM；
     *         正值=协议 Sign=1（正转），负值=协议 Sign=0（反转）；
     *         若该轴尚未收到过 0x35 回复，返回 0
     */
    int32_t StepmotorBus_GetLastSpeedRpm(uint8_t axis_id);

    /**
     * @brief 获取最近一次成功解析的“读速度 0x35”反馈帧原始速度字段
     * @param axis_id 电机轴地址（1=Y，2=X）
     * @return 带符号原始速度字段，高低字节直接拼接后的值
     */
    int32_t StepmotorBus_GetLastSpeedRaw(uint8_t axis_id);

#ifdef __cplusplus
}
#endif

#endif /* STEPMOTOR_BUS_H */
