/******************************************************************************
 * 文件名   : menu_ui.h
 * 说明     : 选择页界面（逻辑层只需要调 menu_ui_render 这一个函数）
 ******************************************************************************/

#ifndef __MENU_UI_H
#define __MENU_UI_H

#include <stdint.h>

/**
 * @brief  注册触摸按钮回调（左箭头 / 右箭头 / 卡片）
 * @note   回调在 UI 线程里被调用，实现里只做"发消息"这种快操作
 */
void menu_ui_set_tap_cb(void (*on_prev)(void), void (*on_next)(void), void (*on_enter)(void));

/**
 * @brief  画出（或更新）选择页
 * @param  index  当前选中的任务序号（0 开始）
 * @param  total  任务总数
 * @param  name   当前任务名
 */
void menu_ui_render(uint8_t index, uint8_t total, const char *name);

#endif