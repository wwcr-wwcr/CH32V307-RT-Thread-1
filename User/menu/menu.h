/******************************************************************************
 * 文件名   : menu.h
 * 说明     : 菜单对外接口，只有「命令号 + 三个函数」。
 *
 * 命令统一成 (cmd, id, value) 三元组，任何线程都可以 menu_post() 入队，
 * 由 UI 线程单线程消费。menu_post() 不碰 lv_、不阻塞。
 ******************************************************************************/

#ifndef __MENU_H
#define __MENU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rtthread.h"
#include "key.h"

/* 命令号。menu_ui.c 也直接用这些名字，不要出现裸数字 */
#define MENU_CMD_KEY        0       /* id = key_id_t（短按 1~4 / 长按 5~8 / 双击 9~12） */
#define MENU_CMD_PREV       1       /* 上一个 */
#define MENU_CMD_NEXT       2       /* 下一个 */
#define MENU_CMD_ENTER      3       /* id = 任务序号 */
#define MENU_CMD_BACK       4       /* 退出当前任务 */
#define MENU_CMD_SET        5       /* id = 控件号, value = 控件值 */
#define MENU_CMD_NUM        6

/**
 * @brief  启动菜单：建任务运行器、命令队列、按键线程、UI 线程
 * @note   main 里调一次，调用前必须先 key_init()
 */
void menu_start(void);

/**
 * @brief  投递一条命令（线程安全，只入队）
 * @param  cmd   MENU_CMD_xxx
 * @param  id    含义随 cmd 变，见上面的注释
 * @param  value 只有 MENU_CMD_SET 用
 * @return RT_EOK；-RT_EINVAL 命令号非法；-RT_EFULL 队列满；-RT_ERROR 菜单未启动
 */
rt_err_t menu_post(uint8_t cmd, uint8_t id, int32_t value);

/**
 * @brief  打印菜单状态 + 任务清单 + 堆占用，MSH 命令 `menu` 用
 */
void menu_dump(void);

#ifdef __cplusplus
}
#endif

#endif
