/**
 * @file    sys_init.h
 * @brief   系统一次性初始化入口声明（App System 装配层）。
 *
 * SysInit 完成 Board/Clock/Driver/Service 初始化并开中断；实现在 sys_init.c。
 * 历史上其声明寄居 World-1 冻结头 task_scheduler.h（:127）；W2 起由本头承载
 * （main.c 去 task_scheduler.h 后的层合规声明 site，见计划表 §22.2）。
 */
#ifndef APP_SYSTEM_SYS_INIT_H
#define APP_SYSTEM_SYS_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

/** 系统初始化：Board→Clock→Driver→Service，末尾开中断。上电一次性调用。 */
void SysInit(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SYSTEM_SYS_INIT_H */
