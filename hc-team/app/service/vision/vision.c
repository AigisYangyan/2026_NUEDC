/**
 * @file    vision.c
 * @brief   视觉服务实现：握手重发节拍/确认状态唯一所有者；坐标/状态零第二处理。
 */
#include "app/service/vision/vision.h"

#include "driver/uart_vision/uart_vision.h"
#include "driver/uart_vofa/uart_vofa.h"

#define VISION_UPDATE_PERIOD_MS 10u
#define VISION_RETRY_MS         500u

static bool     s_seeded;
static uint32_t s_period_base_ms;

static bool     s_topic_pending;    /* 已发起且未确认 */
static bool     s_topic_confirmed;
static uint8_t  s_topic_main;
static uint8_t  s_topic_sub;
static uint32_t s_retry_base_ms;
static uint32_t s_retry_count;
static uint32_t s_ack_seq_baseline; /* 发起时的 ack seq：只认发起之后的回显 */

/* VisionLink 遥测镜像（注册序 = 通道序 ch0..ch7）。 */
static bool  s_telemetry_on;
static float s_tx_x;
static float s_tx_y;
static int   s_tx_coord_seq;
static int   s_tx_st0;
static int   s_tx_st1;
static int   s_tx_st_seq;
static int   s_tx_confirmed;
static int   s_tx_retries;

void Vision_Init(void)
{
    UartVision_Init();
    s_seeded = false;
    s_period_base_ms = 0u;
    s_topic_pending = false;
    s_topic_confirmed = false;
    s_topic_main = 0u;
    s_topic_sub = 0u;
    s_retry_base_ms = 0u;
    s_retry_count = 0u;
    s_ack_seq_baseline = 0u;
    s_telemetry_on = false;     /* 与头注「遥测状态复位」承诺对称（StartTelemetry 显式重开） */
}

bool Vision_SelectTopic(uint8_t main_task, uint8_t sub_task)
{
    s_topic_main = main_task;
    s_topic_sub = sub_task;
    s_topic_confirmed = false;
    s_topic_pending = true;
    s_retry_count = 0u;
    s_ack_seq_baseline = UartVision_GetTopicAckSeq();
    s_retry_base_ms = s_period_base_ms;     /* 下个 500ms 窗从当前节拍起算 */
    return UartVision_SendTopic(main_task, sub_task);
}

bool Vision_IsTopicConfirmed(void)
{
    return s_topic_confirmed;
}

void Vision_Update(uint32_t now_ms)
{
    uint32_t elapsed_ms;

    if (!s_seeded) {
        s_seeded = true;
        s_period_base_ms = now_ms;
        s_retry_base_ms = now_ms;
        return;
    }
    elapsed_ms = now_ms - s_period_base_ms;
    if (elapsed_ms < VISION_UPDATE_PERIOD_MS) {
        return;
    }
    s_period_base_ms = now_ms;

    UartVision_Poll();

    /* 确认跟踪：只认发起之后的回显，且题号须一致（不一致继续重发）。 */
    if (s_topic_pending && (UartVision_GetTopicAckSeq() != s_ack_seq_baseline)) {
        uint8_t ack_main = 0u;
        uint8_t ack_sub = 0u;

        s_ack_seq_baseline = UartVision_GetTopicAckSeq();
        if (UartVision_GetTopicAck(&ack_main, &ack_sub)
            && (ack_main == s_topic_main) && (ack_sub == s_topic_sub)) {
            s_topic_confirmed = true;
            s_topic_pending = false;
        }
    }

    /* 500ms 重发节拍（本服务唯一节拍所有者；uart_vision 无时间轴不变）。 */
    if (s_topic_pending && ((now_ms - s_retry_base_ms) >= VISION_RETRY_MS)) {
        s_retry_base_ms = now_ms;
        (void)UartVision_SendTopic(s_topic_main, s_topic_sub);
        s_retry_count++;
    }

    if (s_telemetry_on) {
        UartVision_Coord_T coord;
        uint8_t st[2];

        if (UartVision_GetLatestCoord(&coord)) {
            s_tx_x = coord.x;
            s_tx_y = coord.y;
        }
        if (UartVision_GetLatestStatus(st)) {
            s_tx_st0 = (int)st[0];
            s_tx_st1 = (int)st[1];
        }
        s_tx_coord_seq = (int)UartVision_GetCoordSeq();
        s_tx_st_seq    = (int)UartVision_GetStatusSeq();
        s_tx_confirmed = s_topic_confirmed ? 1 : 0;
        s_tx_retries   = (int)s_retry_count;
        vofa_run();
    }
}

bool Vision_GetLatestCoord(float *x, float *y)
{
    UartVision_Coord_T coord;

    if ((x == (void *)0) || (y == (void *)0) || !UartVision_GetLatestCoord(&coord)) {
        return false;
    }
    *x = coord.x;
    *y = coord.y;
    return true;
}

uint32_t Vision_CoordSeq(void)
{
    return UartVision_GetCoordSeq();
}

bool Vision_GetLatestStatus(uint8_t out[2])
{
    return UartVision_GetLatestStatus(out);
}

uint32_t Vision_StatusSeq(void)
{
    return UartVision_GetStatusSeq();
}

void Vision_StartTelemetry(void)
{
    vofa_clear_profile();
    s_tx_x = 0.0f;
    s_tx_y = 0.0f;
    s_tx_coord_seq = 0;
    s_tx_st0 = 0;
    s_tx_st1 = 0;
    s_tx_st_seq = 0;
    s_tx_confirmed = 0;
    s_tx_retries = 0;
    (void)vofa_register_float(&s_tx_x);
    (void)vofa_register_float(&s_tx_y);
    (void)vofa_register_int(&s_tx_coord_seq);
    (void)vofa_register_int(&s_tx_st0);
    (void)vofa_register_int(&s_tx_st1);
    (void)vofa_register_int(&s_tx_st_seq);
    (void)vofa_register_int(&s_tx_confirmed);
    (void)vofa_register_int(&s_tx_retries);
    s_telemetry_on = true;
}

void Vision_StopTelemetry(void)
{
    s_telemetry_on = false;
    vofa_clear_profile();
}
