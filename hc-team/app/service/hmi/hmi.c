/**
 * @file    hmi.c
 * @brief   人机输入/显示服务实现（S04 契约 §10）。
 *
 * 包装 Key/OLED 两个 Driver：K1..K4 → 语义动作的唯一映射点从旧 menu_core
 * 下沉至此；泵送节奏沿用旧 5ms UI 任务；行式显示把「防残影」从旧的整屏
 * Clear 改为行级整行覆写保证。
 */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "app/service/hmi/hmi.h"
#include "driver/key/key.h"
#include "driver/oled/oled_hardware_i2c.h"
#include "driver/clock/clock.h"

/* 沿用旧 Task_UiService5ms 节奏：按键去抖 4 拍 × 5ms ≈ 20ms 等效不变。 */
#define HMI_UPDATE_PERIOD_MS 5u

/* 16px 字模占 2 页：行号 → 页地址。 */
#define HMI_PAGES_PER_ROW 2u

static uint32_t s_last_update_ms;

void Hmi_Init(void)
{
    s_last_update_ms = Clock_NowMs();
}

void Hmi_Update(void)
{
    uint32_t now_ms = Clock_NowMs();

    if ((uint32_t)(now_ms - s_last_update_ms) < HMI_UPDATE_PERIOD_MS) {
        return;
    }

    s_last_update_ms = now_ms;

    if (OLED_IsReady() == false) {
        (void)OLED_Process();
    }

    Key_Scan();
}

Hmi_Input Hmi_PollInput(void)
{
    Key_Id_e key = KEY_ID_K1;

    if (Key_PollPressEvent(&key) == false) {
        return HMI_INPUT_NONE;
    }

    switch (key) {
    case KEY_ID_K1:
        return HMI_INPUT_UP;
    case KEY_ID_K2:
        return HMI_INPUT_DOWN;
    case KEY_ID_K3:
        return HMI_INPUT_ENTER;
    case KEY_ID_K4:
        return HMI_INPUT_BACK;
    default:
        return HMI_INPUT_NONE;
    }
}

bool Hmi_IsDisplayReady(void)
{
    return OLED_IsReady();
}

bool Hmi_PrintLine(uint8_t row, const char *text)
{
    char line[HMI_DISPLAY_COLS + 1u];
    size_t len = 0u;

    if ((text == NULL) || (row >= HMI_DISPLAY_ROWS) ||
        (OLED_IsReady() == false)) {
        return false;
    }

    len = strlen(text);
    if (len > HMI_DISPLAY_COLS) {
        len = HMI_DISPLAY_COLS;
    }

    memcpy(line, text, len);
    memset(&line[len], ' ', (size_t)HMI_DISPLAY_COLS - len);
    line[HMI_DISPLAY_COLS] = '\0';

    return OLED_ShowString(0u, (uint8_t)(row * HMI_PAGES_PER_ROW), line,
                           16u) == OLED_OK;
}

bool Hmi_ClearDisplay(void)
{
    if (OLED_IsReady() == false) {
        return false;
    }

    return OLED_Clear() == OLED_OK;
}
