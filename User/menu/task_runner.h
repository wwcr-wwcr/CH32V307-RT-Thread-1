/******************************************************************************
 * 文件名   : task_runner.h
 * 说明     : 任务线程的运行器。只管「建线程 / 停线程 / 转后台 / 投事件」，
 *            一行 lv_ 都没有，也不认识菜单页面。菜单通过它间接操作任务线程。
 *
 * 一个任务 = 一个动态线程：
 *     进入界面时 rt_thread_create()，退出界面时线程自己结束并被回收，
 *     所以整机内存占用 = key + ui + 当前这一个任务，不随任务数量增长。
 *
 * 事件队列：
 *     菜单线程（UI 线程）往 s_cmd_q 里投事件，任务线程取出来分发到
 *     on_key / on_set，这也是唯一的跨线程通道。
 ******************************************************************************/

#ifndef __TASK_RUNNER_H
#define __TASK_RUNNER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rtthread.h"
#include "task/task.h"

/*********************************************************************
 * 初始化与查询
 *********************************************************************/

/**
 * @brief  建事件队列和退出信号量，必须在任何任务启动前调用一次
 * @return 0 成功，-1 失败
 */
int task_runner_init(void);

/** 任务总数（由 task_runner.c 里的 s_task_list[] 决定） */
uint8_t task_runner_count(void);

/** 取第 index 个任务的描述，越界返回 NULL */
const app_task_t *task_runner_at(uint8_t index);

/** 当前前台任务，任务页之外返回 NULL */
const app_task_t *task_runner_current(void);

/** 当前是否有任务线程在跑 */
rt_bool_t task_runner_running(void);

/*********************************************************************
 * 生命周期（只有菜单线程能调）
 *********************************************************************/

/**
 * @brief  启动第 index 个任务线程
 * @return 0 成功，-1 失败（越界、重复启动、线程创建失败都会返回 -1）
 */
int task_runner_start(uint8_t index);

/**
 * @brief  停掉当前任务线程，阻塞等它退出（最多 3 秒）
 */
void task_runner_stop(void);

/**
 * @brief  离开当前任务：登记在「常驻表」里的转后台继续跑，其余直接停掉
 * @note   调用后 task_runner_current() 返回 NULL
 */
void task_runner_leave(void);

/*********************************************************************
 * 事件投递（线程安全）
 *********************************************************************/

/** 投递按键事件，command 里已经过滤掉「K4 长按」 */
rt_err_t task_runner_post_key(key_id_t key);

/** 投递触摸控件事件（控件 id + 数值） */
rt_err_t task_runner_post_set(uint8_t id, int32_t value);

#ifdef __cplusplus
}
#endif

#endif
