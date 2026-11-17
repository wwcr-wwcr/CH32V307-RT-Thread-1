/******************************************************************************
 * 文件名   : task.h
 * 说明     : 菜单任务接口（每个任务 = 一个动态线程）
 *
 * 线程模型：
 *     key 线程    常驻，中断驱动，只产生按键事件
 *     menu 线程   常驻，管菜单：收按键 -> 停掉旧任务 -> 建新任务
 *     task 线程   动态：进界面 rt_thread_create()，退界面 rt_thread_delete()
 *
 * 内存：
 *     任务线程的栈和控制块都从 heap 分配，任务退出时立刻回收，
 *     所以整机内存占用 = key + menu + 当前这一个任务，不随任务数量增长。
 *
 * 新增界面只需要两步：
 *     1. 照着 task_example.c 写一个 task_xxx.c，填好下面那些回调；
 *     2. 在 task.h 末尾加一行 extern，再在 task_runner.c 的 s_task_list[] 加一行。
 ******************************************************************************/

#ifndef __TASK_H
#define __TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "key.h"

/** 任务线程与菜单 UI 线程共用的时间节拍（ms）。改这里两端一起变 */
#define APP_TICK_MS     10

/**
 * @brief 一个任务（一个界面 + 一个线程）的描述
 *
 * name       标题栏显示的名字，同时也是线程名（建议 <= 7 个字符）
 * stack_size 线程栈大小（字节）。只画字符 1024 够；有浮点/大字符串给 2048
 * priority   线程优先级，数字越大优先级越低（key=8、menu=9，任务建议 12 左右）
 * setup      线程启动后调用：初始化硬件、画内容区（y >= UI_CONTENT_Y）
 * update     周期刷新（20ms 一次），传 NULL 表示静态界面，线程只等按键不空转
 * on_key     本界面的按键，会收到除「K4 长按」以外的全部事件：
 *               短按 K1~K4 = 1~4，长按 K1~K3 = 5~7，双击 K1~K4 = 9~12
 *            唯一被菜单截走的是 K4 长按（8），它固定用于返回菜单，永远不会下发。
 *            也就是说任务里短按 K4（4）是有效的，可以自己用。
 * render     渲染回调，在 UI 线程里被周期调用，里面只调一个 xxx_ui_render 即可。
 *            所有 lv_ 调用只能出现在 render 链路里，逻辑线程绝对不许碰 lv_。
 * teardown   线程退出前调用，释放本任务占用的资源
 */
typedef struct
{
    const char *name;
    uint16_t    stack_size;
    uint8_t     priority;

    void (*setup)(void);            /* 逻辑线程：初始化硬件 */
    void (*update)(void);           /* 逻辑线程：周期处理（可 NULL） */
    void (*on_key)(key_id_t key);   /* 逻辑线程：按键（可 NULL） */
    void (*on_set)(uint8_t id, int32_t value);  /* 逻辑线程：触摸控件事件（可 NULL） */
    void (*render)(void);           /* UI  线程：渲染（可 NULL），内部只调一个 xxx_ui_render */
    void (*teardown)(void);         /* 逻辑线程：清理（可 NULL） */
    void (*ui_destroy)(void);       /* UI 线程：销毁本任务的页面，归还 LVGL 堆 */
} app_task_t;

/* 各任务实例（在各自的 task_x.c 里定义）。
 * 加新任务：这里加一行 extern，再到 task_runner.c 的 s_task_list[] 里加一行地址。 */
extern const app_task_t task_example;

#ifdef __cplusplus
}
#endif

#endif