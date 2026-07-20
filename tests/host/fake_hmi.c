/**
 * @file    fake_hmi.c
 * @brief   hmi 显示面 fake（W7 契约 §29）——捕获 Hmi_PrintLine 的行文本与逐行绘制计数。
 *
 * 只顶替 gray_check 消费的显示面符号 `Hmi_PrintLine`（gray_check 不碰 PollInput/Update/
 * IsDisplayReady，故不提供——缺符号即链接期证明消费面没有偷偷扩大）。
 * 与真实 hmi 的语义差异：不做 16 列空格填满（padding 是显示硬件语义，消费者契约只到
 * 「整行覆写+截断」）；行文本按 16 列截断后原样存储，供用例做精确字符串断言。
 *
 * 仅 test_gray_check 链接本文件；test_menu 链接真实 hmi.c（互不冲突，各自独立二进制）。
 */
#include "app/service/hmi/hmi.h"

#include <string.h>

static char     s_rows[HMI_DISPLAY_ROWS][HMI_DISPLAY_COLS + 1u];
static uint32_t s_print_count[HMI_DISPLAY_ROWS];
static bool     s_ready = true;

void FakeHmi_Reset(void)
{
    memset(s_rows, 0, sizeof(s_rows));
    memset(s_print_count, 0, sizeof(s_print_count));
    s_ready = true;
}

void FakeHmi_SetReady(bool ready)
{
    s_ready = ready;
}

const char *FakeHmi_GetRow(uint8_t row)
{
    return (row < HMI_DISPLAY_ROWS) ? s_rows[row] : "";
}

uint32_t FakeHmi_GetRowPrintCount(uint8_t row)
{
    return (row < HMI_DISPLAY_ROWS) ? s_print_count[row] : 0u;
}

/* 契约同真实 hmi.h：未就绪/越界/NULL → false 零绘制；成功 → 截断存储 + 计数。 */
bool Hmi_PrintLine(uint8_t row, const char *text)
{
    uint32_t i;

    if (!s_ready || (row >= HMI_DISPLAY_ROWS) || (text == NULL)) {
        return false;
    }
    for (i = 0u; (i < HMI_DISPLAY_COLS) && (text[i] != '\0'); i++) {
        s_rows[row][i] = text[i];
    }
    s_rows[row][i] = '\0';
    s_print_count[row]++;
    return true;
}
