/**
 * @file lvgl_app.h
 * LVGL 应用层接口
 */

#ifndef LVGL_APP_H
#define LVGL_APP_H

#include <rtthread.h>

/**
 * 初始化 LVGL 并创建 LVGL 主循环线程
 * 注意：本函数已通过 INIT_APP_EXPORT 自动调用，一般不需要手动调
 */
int lvgl_app_init(void);

/** 创建示例界面（可换成你自己的界面） */
void lvgl_demo_create(void);

#endif /* LVGL_APP_H */
