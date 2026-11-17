/******************************************************************************
 * 文件名   : menu_ui.h
 * 说明     : 选择页界面（逻辑层只需要调 menu_ui_render 这一个函数）
 *
 * 触摸：界面上的箭头和卡片直接发菜单命令（1 = 上一个，2 = 下一个，
 *       3 = 进入当前任务），和 MSH 的 menu prev/next/open 同一条路。
 ******************************************************************************/

#ifndef __MENU_UI_H
#define __MENU_UI_H

#include <stdint.h>

/**
 * @brief  画出（或更新）选择页
 * @param  index  当前选中的任务序号（0 开始）
 * @param  total  任务总数
 * @param  name   当前任务名
 */
void menu_ui_render(uint8_t index, uint8_t total, const char *name);

#endif
