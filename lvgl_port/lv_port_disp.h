#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H
#include "ch32v30x.h"
/* ESAD2025 board P1: ST7796 320x480, landscape.
 * LCD_CS=PD11, DC=PD12(FSMC_A17), WR=PD5, RD=PD4,
 * LCD_RES=PD13, LCD_BLK=PC4. */
#define LCD_HOR_RES 480
#define LCD_VER_RES 320
#define LCD_BUF_LINES 10
#define LCD_CS_PORT GPIOD
#define LCD_CS_PIN GPIO_Pin_11
#define LCD_RST_PORT GPIOD
#define LCD_RST_PIN GPIO_Pin_13
#define LCD_BL_PORT GPIOC
#define LCD_BL_PIN GPIO_Pin_4
void lv_port_disp_init(void);
/* 由 LVGL 线程调用，完成本轮可能仍在传输的最后一块。 */
void lv_port_disp_wait_pending(void);
void lcd_fill_screen(uint16_t color);
int lcd_run_diagnostic(void);
int lcd_diagnostic_active(void);
#endif
