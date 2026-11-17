/******************************************************************************
 * 文件名   : menu_ui.h
 * 说明     : 选择页（列表页）界面。菜单核心只认这三个函数，不知道列表长什么样。
 *
 * 「一屏显示几个任务、怎么排版、怎么选」全部在本文件里决定，menu.c 不参与。
 * 换成翻页版、列表版、滑动版，都只改本文件。
 *
 * 三个函数都只能在 UI 线程（ui_entry）里调用，内部会碰 lv_。
 ******************************************************************************/

#ifndef __MENU_UI_H
#define __MENU_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  建立（或重新激活）选择页
 * @note   必须幂等：页面已存在时只切回去，不重建
 */
void menu_ui_list_draw(void);

/**
 * @brief  本页自己的命令处理（选择页上收到的命令都走这里）
 * @param  cmd   MENU_CMD_xxx
 * @param  id    含义随 cmd 变
 * @param  value 目前选择页用不到
 * @note   可以是空实现 —— 网格版点击卡片就进任务，没有"光标"这回事
 */
void menu_ui_list_cmd(uint8_t cmd, uint8_t id, int32_t value);

/**
 * @brief  释放选择页占用的 LVGL 对象
 * @note   可以空实现。当前是网格版，进任务后整页回收，省 LVGL 堆
 */
void menu_ui_list_free(void);

#ifdef __cplusplus
}
#endif

#endif
