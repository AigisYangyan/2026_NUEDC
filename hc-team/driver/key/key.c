/**
 * @file    key.c
 * @brief   板载按键基础驱动
 *
 * 本文件实现 K1 ~ K4 的最小按键输入逻辑，作为菜单与交互层的基础输入模块。
 *
 * 功能范围：
 * - 读取按键当前电平状态
 * - 在周期扫描中锁存“未按下 -> 按下”的单次事件
 * - 按固定顺序对外输出待处理的按键事件
 *
 * 不负责的内容：
 * - 长按、连发、双击等高级交互语义
 * - 复杂时间窗口消抖
 * - 菜单状态切换与业务动作分发
 *
 * 实现说明：
 * - BoardGpio 负责提供原始下降沿位图与当前按下位图
 * - GPIO 下降沿中断只负责唤醒扫描；真正事件必须由周期采样确认
 * - 一次有效按下后，必须等待稳定释放，才允许产生下一次按下事件
 * - 所有对外接口都围绕“当前状态”和“单次事件”两类需求展开
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "driver/key/key.h"
#include "driver/board_gpio/board_gpio.h"

 /* ---- 静态配置与运行时状态 ---------------------------------------------- */

/*
 * 更严格的软件消抖：
 * 1. 按下需要连续 4 次采样为低电平才确认，5ms 扫描下约 20ms。
 * 2. 释放同样要求连续 4 次采样为高电平，避免一次按压产生两个事件。
 */
#define KEY_PRESS_DEBOUNCE_TICKS    4u
#define KEY_RELEASE_DEBOUNCE_TICKS  4u

static bool s_key_stable_pressed[KEY_ID_COUNT];
static bool s_key_press_event[KEY_ID_COUNT];
static uint8_t s_key_press_debounce_count[KEY_ID_COUNT];
static uint8_t s_key_release_debounce_count[KEY_ID_COUNT];
static bool s_key_edge_pending[KEY_ID_COUNT];

/* ---- 静态辅助函数 ------------------------------------------------------- */

static bool key_is_valid(Key_Id_e key)
{
    return ((int)key >= 0) && ((int)key < (int)KEY_ID_COUNT);
}

/**
 * @brief  读取按键原始电平对应的按下状态
 * @param  key       按键编号
 * @param  p_pressed 输出：true 表示按下
 * @return true 表示读取成功，false 表示参数非法
 * @note   由于按键为上拉输入，因此低电平表示按下
 */
static uint8_t key_bit(Key_Id_e key)
{
    return (uint8_t)(1u << (uint8_t)key);
}

/* ---- 公开 API ----------------------------------------------------------- */

/**
 * @brief 初始化按键模块
 * @note  清空事件标志与中断待处理标志
 */
void Key_Init(void)
{
    uint8_t raw_levels = BoardGpio_GetKeyRawLevels();

    (void)BoardGpio_ConsumeKeyIrqEdges();

    for (int i = 0; i < (int)KEY_ID_COUNT; ++i) {
        bool pressed = ((raw_levels & key_bit((Key_Id_e)i)) != 0u);

        s_key_stable_pressed[i] = pressed;
        s_key_press_event[i] = false;
        s_key_press_debounce_count[i] = 0u;
        s_key_release_debounce_count[i] = 0u;
        s_key_edge_pending[i] = false;
    }
}

/**
 * @brief 周期消抖确认
 * @note  下降沿中断只作为“开始观察”的提示。
 *        真正按下事件需满足：
 *        1. 当前稳定状态为释放
 *        2. 连续 KEY_PRESS_DEBOUNCE_TICKS 次读到真实按下
 *        3. 产生事件后，必须连续 KEY_RELEASE_DEBOUNCE_TICKS 次读到释放，才重新解锁
 */
void Key_Scan(void)
{
    uint8_t edge_bits = BoardGpio_ConsumeKeyIrqEdges();
    uint8_t raw_levels = 0u;
    bool needs_levels = false;

    for (int i = 0; i < (int)KEY_ID_COUNT; ++i) {
        uint8_t bit = key_bit((Key_Id_e)i);

        if (((edge_bits & bit) != 0u) &&
            (s_key_stable_pressed[i] == false) &&
            (s_key_edge_pending[i] == false)) {
            s_key_edge_pending[i] = true;
            s_key_press_debounce_count[i] = 0u;
        }

        if ((s_key_edge_pending[i] == true) ||
            (s_key_stable_pressed[i] == true)) {
            needs_levels = true;
        }
    }

    if (needs_levels == false) {
        return;
    }

    raw_levels = BoardGpio_GetKeyRawLevels();

    for (int i = 0; i < (int)KEY_ID_COUNT; ++i) {
        bool raw_pressed;

        /*
         * 释放态下只在收到下降沿后开始采样，保留中断唤醒的低开销特性；
         * 已确认按下后则持续观察释放，防止同一次按压重复出事件。
         */
        if ((s_key_edge_pending[i] == false) &&
            (s_key_stable_pressed[i] == false)) {
            continue;
        }

        raw_pressed = ((raw_levels & key_bit((Key_Id_e)i)) != 0u);

        if (s_key_stable_pressed[i] == false) {
            if (raw_pressed == true) {
                if (s_key_press_debounce_count[i] < KEY_PRESS_DEBOUNCE_TICKS) {
                    s_key_press_debounce_count[i]++;
                }

                if (s_key_press_debounce_count[i] >= KEY_PRESS_DEBOUNCE_TICKS) {
                    s_key_stable_pressed[i] = true;
                    s_key_press_event[i] = true;
                    s_key_press_debounce_count[i] = 0u;
                    s_key_release_debounce_count[i] = 0u;
                    s_key_edge_pending[i] = false;
                }
            }
            else {
                /* 抖动或误触发：看到高电平就撤销本轮候选按下。 */
                s_key_press_debounce_count[i] = 0u;
                s_key_edge_pending[i] = false;
            }
        }
        else {
            if (raw_pressed == false) {
                if (s_key_release_debounce_count[i] < KEY_RELEASE_DEBOUNCE_TICKS) {
                    s_key_release_debounce_count[i]++;
                }

                if (s_key_release_debounce_count[i] >= KEY_RELEASE_DEBOUNCE_TICKS) {
                    s_key_stable_pressed[i] = false;
                    s_key_release_debounce_count[i] = 0u;
                    s_key_press_debounce_count[i] = 0u;
                    s_key_edge_pending[i] = false;
                }
            }
            else {
                /* 按住期间任何额外下降沿都不应再次触发事件。 */
                s_key_release_debounce_count[i] = 0u;
                s_key_edge_pending[i] = false;
            }
        }
    }
}

/**
 * @brief 读取按键当前稳定状态
 * @param key 按键编号
 * @return true 表示当前按下
 */
bool Key_IsPressed(Key_Id_e key)
{
    if (key_is_valid(key) == false) {
        return false;
    }

    return s_key_stable_pressed[(int)key];
}

/**
 * @brief 读取并清除指定按键的单次按下事件(事件读取安全检验)
 * @param key 按键编号
 * @return true 表示本次取到了待处理事件
 */
bool Key_GetPressEvent(Key_Id_e key)
{
    if (key_is_valid(key) == false) {
        return false;
    }

    if (s_key_press_event[(int)key] == false) {
        return false;
    }

    s_key_press_event[(int)key] = false;
    return true;
}

/**
 * @brief  按 K1 -> K4 顺序取出一个待处理事件
 * @param  p_key 输出：被取出的按键编号
 * @return true 表示成功取到一个事件
 */
bool Key_PollPressEvent(Key_Id_e* p_key)
{
    if (p_key == NULL) {
        return false;
    }

    for (int i = 0; i < (int)KEY_ID_COUNT; ++i) {
        if (s_key_press_event[i] == true) {
            s_key_press_event[i] = false;
            *p_key = (Key_Id_e)i;
            return true;
        }
    }

    return false;
}
