/******************************************************************************
 * 文件名   : boot_ui.h
 * 说明     : 启动动画（糖果风 splash）
 ******************************************************************************/

#ifndef __BOOT_UI_H
#define __BOOT_UI_H

#include <stdint.h>

/**
 * @brief  显示启动动画（会切到自己的 screen）
 * @note   调用后主循环要轮询 boot_ui_done()，播完了再画菜单
 */
void boot_ui_show(void);

/**
 * @brief  启动动画是否播完
 */
uint8_t boot_ui_done(void);

/**
 * @brief  销毁启动动画的 screen（画完首屏后调用）
 */
void boot_ui_close(void);

#endif