/******************************************************************************
 * 文件名   : menu.h
 * 说明     : 菜单调度（UI 线程）
 *
 * 线程划分：
 *     key 线程     中断驱动，消抖后回调
 *     ui  线程     本文件，唯一碰 lcd 的线程
 *     sensor 线程  采集传感器，见 sensor.h
 ******************************************************************************/

#ifndef __MENU_H
#define __MENU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/**
 * @brief  启动菜单：建队列、注册按键回调、启动按键线程和菜单线程
 * @note   main 里调一次即可，调用前必须先 key_init()
 *
 * 界面分两级：
 *     列表页  显示所有任务名，K1/K2 选择，K3 进入
 *     任务页  进入后动态创建任务线程，K4 强制退出
 */
void menu_start(void);
void menu_request_exit(void);
#ifdef __cplusplus
}
#endif

#endif
