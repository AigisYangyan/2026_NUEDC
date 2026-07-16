/**
 * @file    key.h
 * @brief   板载按键驱动模块对外接口定义
 *
 * @details
 * 模块职责：
 * - 读取 K1~K4 的当前电平状态
 * - 在周期扫描时产生“单次按下”事件
 * - 为上层菜单/参数调节逻辑提供最基础的输入接口
 *
 * 设计约定：
 * 1. K1~K4 已在 SysConfig 中配置为上拉输入，按下时为低电平
 * 2. 本模块使用最简单的软件防抖滤波，不做菜单状态机、长按或连发
 * 3. 若上层需要“单次按下事件”，需周期调用 Key_Scan()
 * 4. 引脚映射来自 SysConfig（GPIO_GRP_KEY_*），本模块不对外暴露端口细节
 *
 * 使用方式：
 * 1. 上电初始化后先调用 Key_Init()
 * 2. 在 1~10ms 周期任务中调用 Key_Scan()
 * 3. 上层通过 Key_IsPressed()/Key_GetPressEvent()/Key_PollPressEvent() 获取输入
 *
 * 注意：
 * - 本模块默认按下有效电平为低
 * - 当前防抖效果依赖 Key_Scan() 的调用周期；扫描越慢，等效防抖时间越长
 * - 本模块不直接参与菜单逻辑，只提供基础输入事件
 */

#ifndef __KEY_H__
#define __KEY_H__


#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ---- 类型定义 ----------------------------------------------------------- */

/**
 * @brief 按键逻辑编号
 * @note  顺序与板载 K1 ~ K4 一致
 */
typedef enum {
    KEY_ID_K1 = 0,  /**< 上 / 左 */
    KEY_ID_K2,      /**< 下 / 右 */
    KEY_ID_K3,      /**< 确认 / 进入 */
    KEY_ID_K4,      /**< 返回 / 退出 */
    KEY_ID_COUNT
} Key_Id_e;

/* ---- 公开 API ----------------------------------------------------------- */

/* 初始化按键驱动，清空事件并同步当前电平。 */
void Key_Init(void);

/* 周期扫描按键状态，建议在 1~10ms 周期任务中调用。 */
void Key_Scan(void);

/* 读取当前是否处于按下状态。返回 true 表示当前按下。 */
bool Key_IsPressed(Key_Id_e key);

/* 读取并清除单次按下事件。 */
bool Key_GetPressEvent(Key_Id_e key);

/* 按 K1 -> K4 顺序取出一个待处理的按下事件。 */
bool Key_PollPressEvent(Key_Id_e *p_key);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H__ */
