/**
 * @file    vision_coord.h
 * @brief   视觉串口协议模块对外接口定义
 *
 * 本模块负责解析视觉侧通过 UART 上报的老板协议帧，并向上层提供：
 * - 选题编号
 * - 最新目标中心坐标
 * - 最新丢失目标预测坐标
 *
 * 功能范围：
 * - 定义视觉协议命令字、长度和坐标状态类型
 * - 提供完整协议帧处理入口
 * - 提供最新目标/状态/选题查询接口
 * - 提供目标更新弱钩子
 *
 * 设计约定：
 * - 帧格式固定为：帧头(0x55 0xAA) + 命令 + 长度 + 数据 + 校验和
 * - Topic 命令使用 1 字节数据；坐标类命令使用 4 字节小端数据
 * - 模块内部区分“真实目标坐标”和“丢失目标预测坐标”两种状态
 * - 上层可通过弱符号覆写更新回调，但不应修改协议解析主链路
 */

#ifndef VISION_COORD_H
#define VISION_COORD_H


#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

    /* ---- 协议常量定义 ------------------------------------------------------- */

    /* 老板协议命令字定义。 */
#define VISION_COORD_FRAME_CMD_TOPIC              0x01u
#define VISION_COORD_FRAME_CMD_TARGET             0x02u
#define VISION_COORD_FRAME_CMD_LOST_TARGET        0x03u

/* 老板协议数据区长度定义。 */
#define VISION_COORD_TOPIC_PAYLOAD_LEN            1u
#define VISION_COORD_TOPIC_PAYLOAD_COMPAT_LEN     4u
#define VISION_COORD_TARGET_PAYLOAD_LEN           4u
#define VISION_COORD_LOST_TARGET_PAYLOAD_LEN      4u

/* ---- 帧格式常量 --------------------------------------------------------- */
#define VISION_COORD_FRAME_HEADER_LEN       4u
#define VISION_COORD_FRAME_CHECKSUM_LEN     1u
#define VISION_COORD_FRAME_MIN_LEN          \
    (VISION_COORD_FRAME_HEADER_LEN + VISION_COORD_FRAME_CHECKSUM_LEN)
#define VISION_COORD_TOPIC_FRAME_LEN        \
    (VISION_COORD_FRAME_MIN_LEN + VISION_COORD_TOPIC_PAYLOAD_LEN)
#define VISION_COORD_TOPIC_COMPAT_FRAME_LEN \
    (VISION_COORD_FRAME_MIN_LEN + VISION_COORD_TOPIC_PAYLOAD_COMPAT_LEN)
#define VISION_COORD_TARGET_FRAME_LEN       \
    (VISION_COORD_FRAME_MIN_LEN + VISION_COORD_TARGET_PAYLOAD_LEN)
#define VISION_COORD_LOST_TARGET_FRAME_LEN  \
    (VISION_COORD_FRAME_MIN_LEN + VISION_COORD_LOST_TARGET_PAYLOAD_LEN)

#define VISION_COORD_FRAME_CMD          VISION_COORD_FRAME_CMD_TARGET
#define VISION_COORD_FRAME_PAYLOAD_LEN  VISION_COORD_TARGET_PAYLOAD_LEN
#define VISION_COORD_FRAME_LEN          VISION_COORD_TARGET_FRAME_LEN

/* ---- 类型定义 ----------------------------------------------------------- */

/*
 * 作用：
 * 保存一帧通过协议校验后的目标坐标结果，作为视觉坐标模块对外传递数据的基础类型。
 *
 * 字段说明：
 * x：目标在图像中的横向坐标，按协议中的小端 16 位有符号整数解析得到。
 * y：目标在图像中的纵向坐标，按协议中的小端 16 位有符号整数解析得到。
 *
 * 使用关系：
 * 该结构体由 VisionCoord_HandleFrame 在处理目标帧时写入，
 * 由 VisionCoord_GetLatest 对外输出，
 * 也会被 vision_coord_extract_target_coord 和 VisionCoord_OnUpdate 使用。
 */
    typedef struct {
        int16_t x;
        int16_t y;
    } VisionCoord_Coordinates_t;

    /* 坐标状态：无坐标 / 有目标 / 丢失目标预测。 */
    typedef enum {
        VISION_COORD_STATUS_NONE = 0,
        VISION_COORD_STATUS_TARGET,
        VISION_COORD_STATUS_LOST_TARGET,
    } VisionCoord_Status_e;

    /* 最近一次坐标状态快照。 */
    typedef struct {
        VisionCoord_Status_e status;
        VisionCoord_Coordinates_t coord;
    } VisionCoord_FrameState_t;

    /* ---- 公开 API ----------------------------------------------------------- */

        /*
         * 作用：
         * 初始化视觉协议模块，清空选题缓存和坐标状态缓存，
         * 使模块回到“尚未收到有效协议帧”的初始状态。
         *
         * 参数：
         * 无。
         *
         * 返回：
         * 无。
         *
         * 调用关系：
         * 该接口由 VisionBus_Init 调用，
         * 为后续 VisionCoord_HandleFrame 写入状态，以及
         * VisionCoord_GetLatest / VisionCoord_GetState / VisionCoord_GetTopic 读取状态提供初始起点。
         */
    void VisionCoord_Init(void);

    /*
     * 作用：
     * 处理一帧已经切分完整的老板协议数据，
     * 在校验成功后刷新模块内部缓存，并在收到目标坐标时触发更新钩子。
     *
     * 参数：
     * frame：指向完整协议帧数据的指针。
     * length：当前协议帧总长度，单位为字节。
     *
     * 返回：
     * true：当前帧格式正确且已被模块接受，包括 Topic、目标坐标和丢失目标三类。
     * false：当前帧为空、格式错误、长度不匹配或校验失败。
     *
     * 调用关系：
     * 该接口由 vision_bus_try_consume_frame 调用，
     * 内部会继续驱动 VisionCoord_OnUpdate，并为各类状态查询接口提供最新结果。
     */
    bool VisionCoord_HandleFrame(const uint8_t* frame, uint16_t length);

    /*
     * 作用：
     * 读取模块当前缓存的最近一次有效目标坐标，
     * 供上层控制或调试链路使用。
     *
     * 参数：
     * p_out：用于接收坐标结果的输出指针。
     *
     * 返回：
     * true：当前存在有效坐标，且已写入 p_out。
     * false：输出指针为空，或当前没有可用的有效坐标。
     *
     * 调用关系：
     * 该接口当前由 VisionHdl_Run10ms 和 Task_VofaService 调用，
     * 读取的数据来源于 VisionCoord_HandleFrame 成功处理后的内部缓存。
     */
    bool VisionCoord_GetLatest(VisionCoord_Coordinates_t* p_out);

    /*
     * 作用：
     * 读取模块当前保存的最近一次“坐标状态”，
     * 既可表示真实目标坐标，也可表示丢失目标后的预测坐标。
     *
     * 参数：
     * p_out：用于接收状态快照的输出指针。
     *
     * 返回：
     * true：当前存在有效坐标状态，且已写入 p_out。
     * false：输出指针为空，或当前尚未收到任何坐标类帧。
     */
    bool VisionCoord_GetState(VisionCoord_FrameState_t* p_out);

    /*
     * 作用：
     * 读取最近一次坐标状态刷新对应的时间戳与序号，
     * 用于上层估算视觉帧间隔和统计真实帧率。
     *
     * 参数：
     * p_tick_ms：输出最近一次状态更新时间戳（ms）。
     * p_seq：输出最近一次状态更新序号（单调递增）。
     *
     * 返回：
     * true：已存在有效坐标状态，且输出参数写入成功。
     * false：输出参数为空，或当前尚未收到任何坐标类帧。
     */
    bool VisionCoord_GetStateUpdateMeta(uint32_t* p_tick_ms, uint32_t* p_seq);

    /*
     * 作用：
     * 读取模块当前缓存的最新选题编号。
     *
     * 参数：
     * p_topic：用于接收 Topic 的输出指针。
     *
     * 返回：
     * true：当前存在有效选题，且已写入 p_topic。
     * false：输出指针为空，或当前尚未收到选题帧。
     */
    bool VisionCoord_GetTopic(int8_t* p_topic);

    /*
     * 作用：
     * 提供视觉坐标更新后的扩展钩子，默认实现为空，
     * 允许上层在不修改本模块核心逻辑的前提下接管“收到新目标坐标”这一事件。
     *
     * 参数：
     * p_coord：本次刚刚解析并写入缓存的目标坐标指针。
     *
     * 返回：
     * 无。
     *
     * 调用关系：
     * 该接口由 VisionCoord_HandleFrame 在成功处理“有目标帧”后调用，
     * 默认弱实现可由上层模块覆写。
     */
    __attribute__((weak)) void VisionCoord_OnUpdate(const VisionCoord_Coordinates_t* p_coord);

#ifdef __cplusplus
}
#endif

#endif /* VISION_COORD_H */
