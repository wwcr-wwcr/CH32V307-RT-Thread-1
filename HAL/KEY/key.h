/******************************************************************************
 * 文件名   : key.h
 * 芯片     : CH32V307VCT6
 * 说明     : 4 个板载按键驱动（PE2~PE5，上拉输入，按下为低）
 *
 * 本驱动自己管一个线程，和原来的轮询版本完全不同：
 *
 *     GPIO 电平变化 ──EXTI 中断──> 释放信号量 ──> key 线程
 *                                                    | 15ms 消抖
 *                                                    | 与上次稳定值比较
 *                                                    v
 *                                            回调 key_event_cb_t
 *
 * 上层用法只有三步：
 *     key_init();                          // 初始化（中断 + 建线程，不启动）
 *     key_event_register(my_key_cb);       // 注册回调
 *     key_start();                         // 启动按键线程
 *
 * 回调在 key 线程上下文执行，里面不要做耗时操作。
 ******************************************************************************/

#ifndef __KEY_H
#define __KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 引脚定义
 */
#define KEY1_GPIO           GPIOE
#define KEY2_GPIO           GPIOE
#define KEY3_GPIO           GPIOE
#define KEY4_GPIO           GPIOE
#define KEY1_PIN            GPIO_Pin_2     /* PE2 */
#define KEY2_PIN            GPIO_Pin_3     /* PE3 */
#define KEY3_PIN            GPIO_Pin_4     /* PE4 */
#define KEY4_PIN            GPIO_Pin_5     /* PE5 */
#define KEY_GPIO            GPIOE
#define KEY_PIN_ALL         (KEY1_PIN | KEY2_PIN | KEY3_PIN | KEY4_PIN)

/* RT-Thread 引脚编号（对应 drivers/drv_gpio.c 的 pins[] 表：1=PE2 2=PE3 3=PE4 4=PE5） */
#define KEY_RT_PIN_1        1
#define KEY_RT_PIN_2        2
#define KEY_RT_PIN_3        3
#define KEY_RT_PIN_4        4

/*********************************************************************
 * 类型定义
 */
typedef enum
{
    KEY_ID_1 = 0,       /* KEY1 / PE2 */
    KEY_ID_2,           /* KEY2 / PE3 */
    KEY_ID_3,           /* KEY3 / PE4 */
    KEY_ID_4,           /* KEY4 / PE5 */
    KEY_ID_NONE,        /* 没有按键 */
    KEY_ID_MAX = KEY_ID_NONE
} key_id_t;

/* 位掩码，便于一次判断多个按键 */
#define KEY_BIT(id)         ((uint8_t)(1 << (id)))
#define KEY_BUTTON_1        KEY_BIT(KEY_ID_1)
#define KEY_BUTTON_2        KEY_BIT(KEY_ID_2)
#define KEY_BUTTON_3        KEY_BIT(KEY_ID_3)
#define KEY_BUTTON_4        KEY_BIT(KEY_ID_4)
#define KEY_BUTTON_ALL      (KEY_BUTTON_1 | KEY_BUTTON_2 | KEY_BUTTON_3 | KEY_BUTTON_4)

/* 单个按键是否按下（原始电平，未消抖） */
#define KEY_PUSH_BUTTON_1() (GPIO_ReadInputDataBit(KEY1_GPIO, KEY1_PIN) == Bit_RESET)
#define KEY_PUSH_BUTTON_2() (GPIO_ReadInputDataBit(KEY2_GPIO, KEY2_PIN) == Bit_RESET)
#define KEY_PUSH_BUTTON_3() (GPIO_ReadInputDataBit(KEY3_GPIO, KEY3_PIN) == Bit_RESET)
#define KEY_PUSH_BUTTON_4() (GPIO_ReadInputDataBit(KEY4_GPIO, KEY4_PIN) == Bit_RESET)

/**
 * @brief 按键按下事件回调（在 key 线程里被调用）
 * @param key 被按下的按键
 */
typedef void (*key_event_cb_t)(key_id_t key);

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化按键：上拉输入 + 双边沿 EXTI + 信号量 + 创建按键线程
 * @return 0 成功，-1 失败
 * @note   只创建线程不启动，注册回调后再调 key_start()
 */
int key_init(void);

/**
 * @brief  启动按键线程
 * @return 0 成功，-1 未初始化
 */
int key_start(void);

/**
 * @brief  注册按键事件回调
 * @param  cb  回调函数，传 NULL 表示不处理
 */
void key_event_register(key_event_cb_t cb);

/**
 * @brief  读取当前按键电平（未消抖，位掩码）
 * @return KEY_BUTTON_x 组合
 */
uint8_t key_state_get(void);

/**
 * @brief  某个按键当前是否按下（已消抖）
 * @return 1 按下，0 未按下
 */
uint8_t key_is_pressed(key_id_t id);

#ifdef __cplusplus
}
#endif

#endif
