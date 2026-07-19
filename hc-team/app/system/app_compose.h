/**
 * @file    app_compose.h
 * @brief   World-2 运行栈装配（App System 装配层）——运行条目表 + 菜单分组表的持有点。
 *
 * 抽象：把「运行条目（名字 + 生命周期钩子）」与「菜单一级分类」两张编译期静态表
 * 登记进 scheduler 与 menu。换/加 debug/test 项 = 只改本模块的表，
 * scheduler/menu/各 Service 零改动。
 *
 * 前置条件（由 sys_init 保证）：Hmi_Init / Chassis_Init / Tuning_Init 已完成。
 * 分层：本文件属 app/system 装配层，可 include 任何非 DL HAL 头（scheduler/menu/service）。
 */
#ifndef APP_SYSTEM_APP_COMPOSE_H
#define APP_SYSTEM_APP_COMPOSE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 登记运行条目表 + 菜单分组表。
 * @note  = Scheduler_Init(条目表, 条目数, Menu_Tick 背景钩子) + Menu_Setup(分组表, 分组数)。
 *        调用后：空转态 scheduler 每拍只泵 Menu_Tick（HMI）；菜单进入某条目才泵其作用域服务。
 */
void AppCompose_Install(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SYSTEM_APP_COMPOSE_H */
