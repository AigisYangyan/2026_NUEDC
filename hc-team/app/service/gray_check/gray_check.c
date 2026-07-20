/**
 * @file    gray_check.c
 * @brief   12 路灰度标定助手服务实现：VOFA tx×12 遥测 + OLED 现场标定面板（W7 §29）。
 *
 * 数据链（§8.2）：
 *   GPIO 电平 → gray_hw 一次 DL_GPIO_readPins → gray.c 位散射 → Gray_ReadDarkBitmap
 *   [bit i = 通道 i 深色，器件比较器电平（阈值=板上电位器，固件零阈值），无滤波/去抖/反相]
 *   → 本服务同一次读取分两路单向消费：
 *     ① 逐位镜像 12 个 tx（零第二处理）→ vofa_run [uart_vofa：JustFloat 组帧发送]；
 *     ② 标定统计（sticky OR / 逐路跳变计数——只累计不回写，不构成对位图的第二处理）
 *        → 100ms 门控 + 行差分 → Hmi_PrintLine 四行面板。
 *
 * 所有权：12 路原子读唯一在 gray driver；黑白判定唯一在硬件电位器；OLED 行写语义归 hmi；
 * 本服务唯一拥有：tx 组存储 + 发帧节奏 + 标定统计 + 面板格式化/重绘节奏。
 * 只用 hmi 显示面（Hmi_PrintLine）；不碰 Hmi_PollInput——语义输入唯一消费者是 menu，
 * 统计清零手势 = 重进条目（BACK→ENTER 即 Start 归零），不扩输入面。
 *
 * 面板 4 行（16 列 ASCII，'#'=深色/1、'.'=浅色/0，左=bit0=G1，仅通道序不声明车上左右）：
 *   row0 L:············  实时位图
 *   row1 S:············  粘滞深色（进条目以来 OR，白底扫察哪路曾误判深色）
 *   row2 T:············  逐路跳变计数（'.'=0、'1'..'9'、'*'≥10）——手册 p.23「上下微抖
 *                         灯仍不变」的量化读数，压线静置时该行应保持全 '.'
 *   row3 X:%03X N:%02u   位图十六进制（bit0=LSB，位序实测直读）+ 当前深色路数
 */
#include "app/service/gray_check/gray_check.h"

#include <string.h>

#include "app/service/hmi/hmi.h"
#include "driver/gray/gray.h"
#include "driver/uart_vofa/uart_vofa.h"

#define GRAY_CHECK_STREAM_PERIOD_MS 10u
#define GRAY_CHECK_PANEL_PERIOD_MS  100u
#define GRAY_CHECK_TOGGLE_DIGIT_MAX 9u   /* 显示上限：超过画 '*'（计数器本体饱和 0xFFFF） */

/* tx 遥测镜像（注册序 = 通道序 = 显示序）：
 * ch_i（上位机 G(i+1)）= Gray bit i 的深色数字量，1=深色（线上）、0=浅色。 */
static int      s_tx_channel[GRAY_CHANNEL_COUNT];
static uint32_t s_period_base_ms;

/* 标定统计（Start 清零；清零手势=重进条目） */
static uint16_t s_live_bitmap;
static uint16_t s_sticky_dark;
static uint16_t s_prev_bitmap;
static bool     s_prev_seeded;   /* 首拍只播种 prev 不计跳变（否则进条目时压线路误计） */
static uint16_t s_toggle_count[GRAY_CHANNEL_COUNT];

/* OLED 面板：100ms 自门控 + 行差分缓存（仅重绘变化行，避免冗余 I2C 事务；
 * PrintLine 失败＝未就绪/总线错 → 缓存不更新，下个周期重试整行）。 */
static uint32_t s_panel_base_ms;
static char     s_row_cache[HMI_DISPLAY_ROWS][HMI_DISPLAY_COLS + 1u];

/* 同一次 Gray_ReadDarkBitmap 喂 tx 镜像与标定统计（不加第二读点，§8.2）。 */
static void gray_check_sample(void)
{
    const uint16_t bitmap = Gray_ReadDarkBitmap();
    uint32_t i;

    for (i = 0u; i < GRAY_CHANNEL_COUNT; i++) {
        /* 单向复制：逐位提取，零反相/去抖/滤波/左右重排（§8.2 单一所有者）。 */
        s_tx_channel[i] = (((bitmap >> i) & 1u) != 0u) ? 1 : 0;
    }

    if (s_prev_seeded) {
        const uint16_t changed = (uint16_t)(bitmap ^ s_prev_bitmap);

        for (i = 0u; i < GRAY_CHANNEL_COUNT; i++) {
            if ((((changed >> i) & 1u) != 0u) && (s_toggle_count[i] != 0xFFFFu)) {
                s_toggle_count[i]++;
            }
        }
    } else {
        s_prev_seeded = true;
    }
    s_prev_bitmap = bitmap;
    s_live_bitmap = bitmap;
    s_sticky_dark |= bitmap;
}

/* 由当前统计组装 4 行面板文本（纯格式化，无采样副作用）。 */
static void gray_check_format_rows(char rows[HMI_DISPLAY_ROWS][HMI_DISPLAY_COLS + 1u])
{
    static const char k_hex[] = "0123456789ABCDEF";
    uint32_t dark_count = 0u;
    uint32_t i;
    char *hex_row;

    rows[0][0] = 'L';
    rows[1][0] = 'S';
    rows[2][0] = 'T';
    rows[0][1] = rows[1][1] = rows[2][1] = ':';
    for (i = 0u; i < GRAY_CHANNEL_COUNT; i++) {
        const bool live_dark = ((s_live_bitmap >> i) & 1u) != 0u;

        rows[0][2u + i] = live_dark ? '#' : '.';
        rows[1][2u + i] = (((s_sticky_dark >> i) & 1u) != 0u) ? '#' : '.';
        rows[2][2u + i] = (s_toggle_count[i] == 0u) ? '.'
                        : (s_toggle_count[i] <= GRAY_CHECK_TOGGLE_DIGIT_MAX)
                              ? (char)('0' + s_toggle_count[i])
                              : '*';
        if (live_dark) {
            dark_count++;
        }
    }
    rows[0][2u + GRAY_CHANNEL_COUNT] = '\0';
    rows[1][2u + GRAY_CHANNEL_COUNT] = '\0';
    rows[2][2u + GRAY_CHANNEL_COUNT] = '\0';

    hex_row = rows[3];
    hex_row[0] = 'X';
    hex_row[1] = ':';
    hex_row[2] = k_hex[(s_live_bitmap >> 8) & 0xFu];
    hex_row[3] = k_hex[(s_live_bitmap >> 4) & 0xFu];
    hex_row[4] = k_hex[s_live_bitmap & 0xFu];
    hex_row[5] = ' ';
    hex_row[6] = 'N';
    hex_row[7] = ':';
    hex_row[8] = (char)('0' + (dark_count / 10u));
    hex_row[9] = (char)('0' + (dark_count % 10u));
    hex_row[10] = '\0';
}

static void gray_check_panel_render(void)
{
    char rows[HMI_DISPLAY_ROWS][HMI_DISPLAY_COLS + 1u];
    uint32_t row;

    gray_check_format_rows(rows);
    for (row = 0u; row < HMI_DISPLAY_ROWS; row++) {
        if (strcmp(rows[row], s_row_cache[row]) != 0) {
            if (Hmi_PrintLine((uint8_t)row, rows[row])) {
                strcpy(s_row_cache[row], rows[row]);
            }
        }
    }
}

void GrayCheck_Start(void)
{
    uint32_t i;

    vofa_clear_profile();

    for (i = 0u; i < GRAY_CHANNEL_COUNT; i++) {
        s_tx_channel[i] = 0;
        s_toggle_count[i] = 0u;
    }

    /* 注册序 = 通道序 ch0..ch11（G1..G12）；零 bind_cmd（只读诊断，不接收上位机命令）。 */
    for (i = 0u; i < GRAY_CHANNEL_COUNT; i++) {
        (void)vofa_register_int(&s_tx_channel[i]);
    }

    /* base=0：gray 无 elapsed 消费者，无需播种拍——进页首拍即 now-0≥10 发一帧+首绘面板。 */
    s_period_base_ms = 0u;
    s_panel_base_ms = 0u;

    /* 标定统计清零 + 行缓存失效（空串≠任何面板行 → 首绘覆盖全部 4 行，
     * 盖掉菜单残留内容——self-draw 条目的显示权交接义务，menu.h §29）。 */
    s_live_bitmap = 0u;
    s_sticky_dark = 0u;
    s_prev_bitmap = 0u;
    s_prev_seeded = false;
    for (i = 0u; i < HMI_DISPLAY_ROWS; i++) {
        s_row_cache[i][0] = '\0';
    }
}

void GrayCheck_Update(uint32_t now_ms)
{
    uint32_t elapsed_ms = now_ms - s_period_base_ms; /* 无符号减法天然处理回绕 */

    if (elapsed_ms < GRAY_CHECK_STREAM_PERIOD_MS) {
        return;
    }
    s_period_base_ms = now_ms;

    gray_check_sample();
    vofa_run(); /* RX 排空（零绑定 = 无副作用）+ 发本拍刚镜像的 12 路数字量帧（刷新在发送前，无一帧延迟） */

    if ((now_ms - s_panel_base_ms) >= GRAY_CHECK_PANEL_PERIOD_MS) {
        s_panel_base_ms = now_ms;
        gray_check_panel_render();
    }
}

void GrayCheck_Stop(void)
{
    vofa_clear_profile();
    /* 不清 OLED：BACK 后 menu 立即重绘子列表（4 行整行覆写），无残影窗口。 */
}
