/**
 * @file    vision_coord.c
 * @brief   视觉串口协议模块实现
 *
 * 本文件实现老板协议在二维平台工程中的解析、缓存维护与对外读取逻辑。
 *
 * 功能范围：
 * - 校验协议帧头、长度与校验和
 * - 解析选题、目标中心坐标和丢失目标预测坐标
 * - 维护最新选题与最新坐标状态
 * - 提供目标坐标读取、状态读取与更新弱钩子
 *
 * 实现说明：
 * 1. Topic 与坐标状态分开缓存，避免选题帧打断坐标控制链路
 * 2. 坐标链路区分“真实目标”和“丢失预测”两种状态
 * 3. 解析链路分为头部检查、长度检查、校验和检查与字段提取
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "vision_coord.h"
#include "driver/clock/clock.h"

#include <string.h>

 /* ---- 内部类型定义 ------------------------------------------------------- */

 /*
  * 作用：
  * 维护视觉坐标模块的内部运行状态，
  * 保存视觉模块当前的选题信息和最近一次坐标状态。
  *
  * 字段说明：
  * latest_coord：最近一次被模块接受的坐标，可能是真实目标也可能是丢失预测。
  * coord_status：latest_coord 对应的坐标状态。
  * state_update_tick_ms：最近一次坐标状态更新时的毫秒时间戳。
  * state_update_seq：最近一次坐标状态更新序号（单调递增）。
  * latest_topic：最近一次收到的选题编号。
  * topic_valid：latest_topic 是否已经收到过。
  *
  * 读写关系：
  * 该结构体由 VisionCoord_Init 负责整体清零，
  * 由 VisionCoord_HandleFrame 间接调用状态更新函数进行更新，
  * 由 VisionCoord_GetLatest / VisionCoord_GetState / VisionCoord_GetTopic 读取。
  */
typedef struct {
    VisionCoord_Coordinates_t latest_coord;
    VisionCoord_Status_e coord_status;
    uint32_t state_update_tick_ms;
    uint32_t state_update_seq;
    int8_t latest_topic;
    bool topic_valid;
} VisionCoord_State_t;

/*
 * 作用：
 * 保存单帧协议解析后的中间结果，
 * 将“帧是否合法”“命令类型”“解析出的选题/坐标”从解析阶段传递到对外处理阶段。
 *
 * 字段说明：
 * cmd：当前帧的协议命令字。
 * topic：当前帧解析出的选题编号；仅 Topic 帧有效。
 * coord：当前帧解析出的坐标；仅坐标类命令有效。
 *
 * 读写关系：
 * 该结构体由 vision_coord_parse_frame 填充，
 * 由 VisionCoord_HandleFrame 读取并决定如何更新内部状态。
 */
typedef struct {
    uint8_t cmd;
    int8_t topic;
    VisionCoord_Coordinates_t coord;
} VisionCoord_ParseResult_t;

/* ---- 模块状态 ----------------------------------------------------------- */

/* 保存视觉坐标模块当前的最新有效结果。 */
static VisionCoord_State_t s_vision_coord_state;

/* ---- 静态函数声明 ------------------------------------------------------- */

static bool vision_coord_is_supported_frame(uint8_t cmd, uint8_t payload_len);
static uint16_t vision_coord_get_frame_length(uint8_t cmd, uint8_t payload_len);
static bool vision_coord_has_valid_header(const uint8_t* frame, uint16_t length);
static bool vision_coord_validate_length(uint8_t cmd,
    uint8_t payload_len,
    uint16_t length);
static uint8_t vision_coord_calc_checksum(const uint8_t* frame, uint8_t payload_len);
static bool vision_coord_has_valid_checksum(const uint8_t* frame,
    uint16_t length,
    uint8_t payload_len);
static void vision_coord_extract_topic(const uint8_t* frame, int8_t* p_topic);
static void vision_coord_extract_target_coord(const uint8_t* frame,
    VisionCoord_Coordinates_t* p_coord);
static bool vision_coord_parse_frame(const uint8_t* frame,
    uint16_t length,
    VisionCoord_ParseResult_t* p_out);
static void vision_coord_store_topic(int8_t topic);
static void vision_coord_store_state(VisionCoord_Status_e status,
    const VisionCoord_Coordinates_t* p_coord);

/* ---- 公开 API ----------------------------------------------------------- */

/*
 * 作用：
 * 初始化视觉坐标模块的内部状态，将选题缓存与坐标状态全部清零。
 *
 * 参数：
 * 无。
 *
 * 返回：
 * 无。
 *
 * 副作用：
 * 会覆盖 s_vision_coord_state 中的全部内容，使模块回到初始状态。
 *
 * 调用关系：
 * 该函数由 VisionBus_Init 调用，
 * 为 VisionCoord_HandleFrame 写入状态以及各类查询接口读取状态提供起点。
 */
void VisionCoord_Init(void)
{
    memset(&s_vision_coord_state, 0, sizeof(s_vision_coord_state));
}

/*
 * 作用：
 * 接收一帧完整的视觉协议数据，驱动内部解析流程，
 * 并根据解析结果更新最新选题或最新坐标状态。
 *
 * 参数：
 * frame：指向待处理完整协议帧的指针。
 * length：完整协议帧长度，单位为字节。
 *
 * 返回：
 * true：当前帧被成功接受，包括选题帧、目标帧和丢失目标帧。
 * false：当前帧为空、头部错误、长度不匹配或校验失败。
 *
 * 副作用：
 * 成功处理 Topic 帧时会更新 latest_topic；
 * 成功处理目标/丢失目标帧时会更新 latest_coord 与 coord_status；
 * 成功处理目标帧时还会进一步触发 VisionCoord_OnUpdate。
 *
 * 调用关系：
 * 该函数服务于 vision_bus_try_consume_frame，
 * 内部直接依赖 vision_coord_parse_frame、vision_coord_store_topic、
 * vision_coord_store_state 和 VisionCoord_OnUpdate。
 */
bool VisionCoord_HandleFrame(const uint8_t* frame, uint16_t length)
{
    VisionCoord_ParseResult_t parse_result;

    if (vision_coord_parse_frame(frame, length, &parse_result) == false) {
        return false;
    }

    if (parse_result.cmd == VISION_COORD_FRAME_CMD_TOPIC) {
        vision_coord_store_topic(parse_result.topic);
        return true;
    }

    // 目标帧
    if (parse_result.cmd == VISION_COORD_FRAME_CMD_TARGET) {
        vision_coord_store_state(VISION_COORD_STATUS_TARGET, &parse_result.coord);
        VisionCoord_OnUpdate(&parse_result.coord);
        return true;
    }

    // 丢失目标帧
    if (parse_result.cmd == VISION_COORD_FRAME_CMD_LOST_TARGET) {
        vision_coord_store_state(VISION_COORD_STATUS_LOST_TARGET,
            &parse_result.coord);
        return true;
    }

    return false;
}

/*
 * 作用：
 * 向外提供最近一次“真实目标坐标”的只读访问接口。
 *
 * 参数：
 * p_out：输出参数，用于接收当前缓存的真实目标坐标。
 *
 * 返回：
 * true：当前缓存存在真实目标坐标，且已拷贝到 p_out。
 * false：p_out 为空，或当前没有真实目标坐标。
 *
 * 副作用：
 * 无状态修改，仅在成功时向 p_out 写入数据。
 *
 * 调用关系：
 * 该函数服务于 VisionHdl_Run10ms 和 Task_VofaService，
 * 仅当最新坐标状态为 TARGET 时才会对外返回坐标。
 */
bool VisionCoord_GetLatest(VisionCoord_Coordinates_t* p_out)
{
    if ((p_out == NULL) ||
        (s_vision_coord_state.coord_status != VISION_COORD_STATUS_TARGET)) {
        return false;
    }

    *p_out = s_vision_coord_state.latest_coord;
    return true;
}

/*
 * 作用：
 * 向外提供最近一次坐标状态的只读访问接口。
 *
 * 参数：
 * p_out：输出参数，用于接收当前缓存的坐标状态快照。
 *
 * 返回：
 * true：当前缓存存在坐标状态，且已拷贝到 p_out。
 * false：p_out 为空，或当前尚未收到任何坐标类帧。
 *
 * 副作用：
 * 无状态修改，仅在成功时向 p_out 写入数据。
 */
bool VisionCoord_GetState(VisionCoord_FrameState_t* p_out)
{
    if ((p_out == NULL) ||
        (s_vision_coord_state.coord_status == VISION_COORD_STATUS_NONE)) {
        return false;
    }

    p_out->status = s_vision_coord_state.coord_status;
    p_out->coord = s_vision_coord_state.latest_coord;
    return true;
}

/*
 * 作用：
 * 读取最近一次坐标状态更新对应的时间戳和序号。
 *
 * 参数：
 * p_tick_ms：输出最近一次状态更新时间戳（ms）。
 * p_seq：输出最近一次状态更新序号。
 *
 * 返回：
 * true：当前存在有效坐标状态，且输出参数写入成功。
 * false：输出参数为空，或当前尚未收到坐标类帧。
 */
bool VisionCoord_GetStateUpdateMeta(uint32_t* p_tick_ms, uint32_t* p_seq)
{
    if ((p_tick_ms == NULL) || (p_seq == NULL) ||
        (s_vision_coord_state.coord_status == VISION_COORD_STATUS_NONE) ||
        (s_vision_coord_state.state_update_seq == 0u)) {
        return false;
    }

    *p_tick_ms = s_vision_coord_state.state_update_tick_ms;
    *p_seq = s_vision_coord_state.state_update_seq;
    return true;
}

/*
 * 作用：
 * 向外提供最近一次选题编号的只读访问接口。
 *
 * 参数：
 * p_topic：输出参数，用于接收当前缓存的选题编号。
 *
 * 返回：
 * true：当前已经收到过选题帧，且已写入 p_topic。
 * false：p_topic 为空，或当前还没有选题信息。
 */
bool VisionCoord_GetTopic(int8_t* p_topic)
{
    if ((p_topic == NULL) ||
        (s_vision_coord_state.topic_valid == false)) {
        return false;
    }

    *p_topic = s_vision_coord_state.latest_topic;
    return true;
}

/* ---- 协议解析层 --------------------------------------------------------- */

/*
 * 作用：
 * 判断命令字和载荷长度组合是否属于当前模块支持的视觉协议帧类型。
 *
 * 参数：
 * cmd：协议命令字。
 * payload_len：协议载荷长度。
 *
 * 返回：
 * true：当前命令字与载荷长度组合受支持。
 * false：当前组合不属于模块支持的帧类型。
 *
 * 调用关系：
 * 该函数服务于 vision_coord_validate_length，
 * 用于在长度校验前先过滤掉不支持的帧类型。
 */
static bool vision_coord_is_supported_frame(uint8_t cmd, uint8_t payload_len)
{
    if (cmd == VISION_COORD_FRAME_CMD_TOPIC) {
        return (bool)((payload_len == VISION_COORD_TOPIC_PAYLOAD_LEN) ||
            (payload_len == VISION_COORD_TOPIC_PAYLOAD_COMPAT_LEN));
    }

    if (cmd == VISION_COORD_FRAME_CMD_TARGET) {
        return (bool)(payload_len == VISION_COORD_TARGET_PAYLOAD_LEN);
    }

    if (cmd == VISION_COORD_FRAME_CMD_LOST_TARGET) {
        return (bool)(payload_len == VISION_COORD_LOST_TARGET_PAYLOAD_LEN);
    }

    return false;
}

/*
 * 作用：
 * 根据命令字和载荷长度计算当前协议帧的总长度。
 *
 * 参数：
 * cmd：协议命令字。
 * payload_len：协议载荷长度。
 *
 * 返回：
 * 当前帧应有的总字节数。
 *
 * 调用关系：
 * 该函数服务于 vision_coord_validate_length，
 * 用于生成和实参 length 进行比较的期望长度。
 */
static uint16_t vision_coord_get_frame_length(uint8_t cmd, uint8_t payload_len)
{
    ((void)(cmd));
    return (uint16_t)(VISION_COORD_FRAME_MIN_LEN + payload_len);
}

/*
 * 作用：
 * 检查协议帧是否具备最基本的头部合法性，包括空指针、最小长度和固定帧头。
 *
 * 参数：
 * frame：待检查的协议帧指针。
 * length：当前协议帧长度。
 *
 * 返回：
 * true：当前帧具备继续深入解析的基础条件。
 * false：当前帧为空、长度过短或帧头不正确。
 *
 * 调用关系：
 * 该函数服务于 vision_coord_parse_frame，
 * 是整条解析链路中的第一道快速过滤。
 */
static bool vision_coord_has_valid_header(const uint8_t* frame, uint16_t length)
{
    if (frame == NULL) {
        return false;
    }

    if (length < VISION_COORD_FRAME_MIN_LEN) {
        return false;
    }

    return (bool)((frame[0] == 0x55u) &&
        (frame[1] == 0xAAu));
}

/*
 * 作用：
 * 校验命令字、载荷长度和整帧长度三者是否匹配当前支持的协议格式。
 *
 * 参数：
 * cmd：协议命令字。
 * payload_len：协议载荷长度。
 * length：当前帧总长度。
 *
 * 返回：
 * true：命令字受支持，且当前帧长度与理论值一致。
 * false：命令字不受支持，或理论长度与实参长度不一致。
 *
 * 调用关系：
 * 该函数服务于 vision_coord_parse_frame，
 * 内部继续依赖 vision_coord_is_supported_frame 和 vision_coord_get_frame_length。
 */
static bool vision_coord_validate_length(uint8_t cmd,
    uint8_t payload_len,
    uint16_t length)
{
    uint16_t expected_length = 0u;

    if (vision_coord_is_supported_frame(cmd, payload_len) == false) {
        return false;
    }

    expected_length = vision_coord_get_frame_length(cmd, payload_len);
    return (bool)(length == expected_length);
}

/*
 * 作用：
 * 按当前视觉协议规则计算帧校验和，
 * 供后续校验函数与帧尾校验字节进行比较。
 *
 * 参数：
 * frame：完整协议帧指针，默认要求至少包含命令字、长度、载荷和校验字节。
 * payload_len：载荷长度，用于确定参与累加的范围。
 *
 * 返回：
 * 按协议算法得到的 8 位校验和结果。
 *
 * 调用关系：
 * 该函数服务于 vision_coord_has_valid_checksum，
 * 自身不直接参与对外处理流程。
 */
static uint8_t vision_coord_calc_checksum(const uint8_t* frame, uint8_t payload_len)
{
    uint16_t index = 0u;
    uint8_t checksum = 0u;

    checksum = (uint8_t)(frame[2] + frame[3]);
    for (index = 4u; index < (uint16_t)(4u + payload_len); index++) {
        checksum = (uint8_t)(checksum + frame[index]);
    }

    return checksum;
}

/*
 * 作用：
 * 校验当前协议帧的校验和是否与帧尾校验字节一致。
 *
 * 参数：
 * frame：完整协议帧指针。
 * length：当前帧总长度。
 * payload_len：当前帧载荷长度。
 *
 * 返回：
 * true：校验和正确。
 * false：校验和错误。
 *
 * 调用关系：
 * 该函数服务于 vision_coord_parse_frame，
 * 内部通过 vision_coord_calc_checksum 生成理论校验值。
 */
static bool vision_coord_has_valid_checksum(const uint8_t* frame,
    uint16_t length,
    uint8_t payload_len)
{
    uint8_t checksum = 0u;

    checksum = vision_coord_calc_checksum(frame, payload_len);
    return (bool)(checksum == frame[length - 1u]);
}

/*
 * 作用：
 * 从一帧合法目标帧中提取目标坐标，
 * 并按协议的小端格式写入 VisionCoord_Coordinates_t。
 *
 * 参数：
 * frame：已经通过前置校验的完整目标帧指针。
 * p_coord：用于接收解析结果的坐标结构体指针。
 *
 * 返回：
 * 无。
 *
 * 副作用：
 * 在参数合法时会向 p_coord 写入 x、y 两个坐标值。
 *
 * 调用关系：
 * 该函数服务于 vision_coord_parse_frame，
 * 是目标帧从字节流转成结构体坐标的最后一步。
 */
static void vision_coord_extract_topic(const uint8_t* frame, int8_t* p_topic)
{
    if ((frame == NULL) || (p_topic == NULL)) {
        return;
    }

    *p_topic = (int8_t)frame[4];
}

/*
 * 作用：
 * 从一帧合法坐标帧中提取目标坐标，
 * 并按协议的小端格式写入 VisionCoord_Coordinates_t。
 *
 * 参数：
 * frame：已经通过前置校验的完整坐标帧指针。
 * p_coord：用于接收解析结果的坐标结构体指针。
 *
 * 返回：
 * 无。
 *
 * 副作用：
 * 在参数合法时会向 p_coord 写入 x、y 两个坐标值。
 *
 * 调用关系：
 * 该函数服务于 vision_coord_parse_frame，
 * 是坐标类命令从字节流转成结构体坐标的最后一步。
 */
static void vision_coord_extract_target_coord(const uint8_t* frame,
    VisionCoord_Coordinates_t* p_coord)
{
    if ((frame == NULL) || (p_coord == NULL)) {
        return;
    }

    p_coord->x = (int16_t)((uint16_t)frame[4] | ((uint16_t)frame[5] << 8));
    p_coord->y = (int16_t)((uint16_t)frame[6] | ((uint16_t)frame[7] << 8));
}

/*
 * 作用：
 * 完成单帧视觉协议的完整解析流程，
 * 将字节流转换为可供上层消费的 VisionCoord_ParseResult_t。
 *
 * 参数：
 * frame：待解析的完整协议帧指针。
 * length：当前帧总长度。
 * p_out：用于接收解析结果的输出结构体指针。
 *
 * 返回：
 * true：解析成功，p_out 已被填充。
 * false：前置条件不满足，或头部、长度、校验任一阶段失败。
 *
 * 副作用：
 * 成功时会写入 p_out->cmd、p_out->topic 和 p_out->coord；
 * 其中非对应类型的字段会保持清零。
 *
 * 调用关系：
 * 该函数服务于 VisionCoord_HandleFrame，
 * 内部按顺序依赖 vision_coord_has_valid_header、
 * vision_coord_validate_length、vision_coord_has_valid_checksum、
 * vision_coord_extract_topic 和 vision_coord_extract_target_coord。
 */
static bool vision_coord_parse_frame(const uint8_t* frame,
    uint16_t length,
    VisionCoord_ParseResult_t* p_out)
{
    uint8_t cmd = 0u;
    uint8_t payload_len = 0u;

    if ((frame == NULL) || (p_out == NULL)) {
        return false;
    }

    if (vision_coord_has_valid_header(frame, length) == false) {
        return false;
    }

    cmd = frame[2];
    payload_len = frame[3];

    if (vision_coord_validate_length(cmd, payload_len, length) == false) {
        return false;
    }

    if (vision_coord_has_valid_checksum(frame, length, payload_len) ==
        false) {
        return false;
    }

    p_out->cmd = cmd;
    p_out->topic = 0;
    memset(&p_out->coord, 0, sizeof(p_out->coord));

    if (cmd == VISION_COORD_FRAME_CMD_TOPIC) {
        vision_coord_extract_topic(frame, &p_out->topic);
        return true;
    }

    if ((cmd == VISION_COORD_FRAME_CMD_TARGET) ||
        (cmd == VISION_COORD_FRAME_CMD_LOST_TARGET)) {
        vision_coord_extract_target_coord(frame, &p_out->coord);
    }

    return true;
}

/* ---- 状态更新层 --------------------------------------------------------- */

/*
 * 作用：
 * 保存最新选题编号，并标记选题已有效。
 *
 * 参数：
 * topic：待缓存的选题编号。
 *
 * 返回：
 * 无。
 *
 * 副作用：
 * 会覆盖 s_vision_coord_state.latest_topic，并将 topic_valid 置为有效。
 *
 * 调用关系：
 * 该函数服务于 VisionCoord_HandleFrame，
 * 用于在收到合法 Topic 帧后更新选题信息。
 */
static void vision_coord_store_topic(int8_t topic)
{
    s_vision_coord_state.latest_topic = topic;
    s_vision_coord_state.topic_valid = true;
}

/*
 * 作用：
 * 将一份新解析出的坐标状态写入模块缓存。
 *
 * 参数：
 * status：待写入缓存的坐标状态。
 * p_coord：待写入缓存的坐标指针。
 *
 * 返回：
 * 无。
 *
 * 副作用：
 * 在参数合法时会更新 s_vision_coord_state.latest_coord 和 coord_status。
 *
 * 调用关系：
 * 该函数服务于 VisionCoord_HandleFrame，
 * 用于在成功处理目标帧或丢失目标帧后保存最新结果。
 */
static void vision_coord_store_state(VisionCoord_Status_e status,
    const VisionCoord_Coordinates_t* p_coord)
{
    uint32_t tick_ms = 0u;

    if ((p_coord == NULL) || (status == VISION_COORD_STATUS_NONE)) {
        return;
    }

    s_vision_coord_state.latest_coord = *p_coord;
    s_vision_coord_state.coord_status = status;

    tick_ms = Clock_NowMs();
    s_vision_coord_state.state_update_tick_ms = tick_ms;
    s_vision_coord_state.state_update_seq++;
}

/* ---- 弱钩子 ------------------------------------------------------------- */

/*
 * 作用：
 * 提供默认的坐标更新弱实现，在没有上层覆写时不执行任何额外逻辑。
 *
 * 参数：
 * p_coord：本次刚刚解析成功的目标坐标指针。
 *
 * 返回：
 * 无。
 *
 * 副作用：
 * 默认实现仅消除未使用参数告警，不修改任何状态。
 *
 * 调用关系：
 * 该函数由 VisionCoord_HandleFrame 在成功处理目标帧后调用，
 * 服务于后续可能的算法层或业务层覆写扩展。
 */
__attribute__((weak)) void VisionCoord_OnUpdate(const VisionCoord_Coordinates_t* p_coord)
{
    ((void)(p_coord));
}
