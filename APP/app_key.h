#ifndef APP_KEY_H
#define APP_KEY_H

#include <stdint.h>

/* 引脚：KEY1=PE2、KEY2=PE3、KEY3=PE4、KEY4=PE5，低电平按下。 */
#define APP_KEY_NONE          0x00U
#define APP_KEY1              0x01U
#define APP_KEY2              0x02U
#define APP_KEY3              0x04U
#define APP_KEY4              0x08U

/* 毫秒单位；扫描周期应明显短于消抖时间。 */
#define APP_KEY_SCAN_MS       10U
#define APP_KEY_DEBOUNCE_MS   20U
#define APP_KEY_DOUBLE_MS     300U
#define APP_KEY_LONG_MS       800U

/*
 * 16位事件：bit0～3单击，bit4～7双击，bit8～11长按。
 * key传APP_KEY1～APP_KEY4，也可以按位或组合多个按键。
 * 示例：events & APP_KEY_DOUBLE_CLICK(APP_KEY1)。
 * 兼容旧写法：events & APP_KEY1，含义现在是KEY1单击事件。
 */
#define APP_KEY_CLICK(key)         ((uint16_t)(key))
#define APP_KEY_DOUBLE_CLICK(key)  ((uint16_t)((uint16_t)(key) << 4U))
#define APP_KEY_LONG_PRESS(key)    ((uint16_t)((uint16_t)(key) << 8U))

/* 初始化一次：上拉输入并复位状态；开机按住的键必须松开后再操作。 */
void app_key_init(void);

/*
 * 每隔约10ms由一个固定线程调用，不延时、不等待松手。
 * 返回本轮所有按键事件的按位或组合；没有事件或未初始化时返回0。
 * 单击：短按松开后，等待300ms没有确认第二次按下才报告。
 * 双击：第一次松开后300ms内确认第二次按下，并短按松开才报告。
 * 长按：确认按下后保持800ms，立即报告一次；松开不追加单击。
 * 第二次按下若变成长按，报告长按，取消尚未报告的第一次单击。
 * 时间判断基于扫描后的消抖状态；边界有扫描周期与tick量化误差。
 * 必须用uint16_t保存返回值，uint8_t会丢掉长按事件。
 * 事件由调用方处理，不要让多个线程分别调用本扫描函数消费事件。
 */
uint16_t app_key_scan(void);

#endif
