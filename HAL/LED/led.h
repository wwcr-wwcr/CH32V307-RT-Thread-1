/******************************************************************************
 * 文件名   : led.h
 * 芯片     : CH32V307VCT6
 * 说明     : 板载 RGB LED 驱动（PB3=红 PB4=绿 PB5=蓝，低电平点亮）
 *
 * 使用示例:
 *     led_init();                        // 初始化，默认全灭
 *     led_color_set(LED_COLOR_RED);      // 亮红灯
 *     led_brightness_set(LED_ALL, 30);   // 全部 30% 亮度（需先调用 led_pwm_init）
 *     led_blink(LED_COLOR_BLUE, 3, 200, 200);
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误，-2 超时/无响应）
 ******************************************************************************/

#ifndef __LED_H
#define __LED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 引脚定义
 */
#define LED_RED_GPIO        GPIOB
#define LED_GREEN_GPIO      GPIOB
#define LED_BLUE_GPIO       GPIOB
#define LED_RED_PIN         GPIO_Pin_3
#define LED_GREEN_PIN       GPIO_Pin_4
#define LED_BLUE_PIN        GPIO_Pin_5

/* 底层电平操作（低电平点亮，如需直接操作引脚可用） */
#define LED_RED_ON()        (GPIO_ResetBits(LED_RED_GPIO, LED_RED_PIN))
#define LED_RED_OFF()       (GPIO_SetBits(LED_RED_GPIO, LED_RED_PIN))
#define LED_GREEN_ON()      (GPIO_ResetBits(LED_GREEN_GPIO, LED_GREEN_PIN))
#define LED_GREEN_OFF()     (GPIO_SetBits(LED_GREEN_GPIO, LED_GREEN_PIN))
#define LED_BLUE_ON()       (GPIO_ResetBits(LED_BLUE_GPIO, LED_BLUE_PIN))
#define LED_BLUE_OFF()      (GPIO_SetBits(LED_BLUE_GPIO, LED_BLUE_PIN))
#define LED_ALL_OFF()       (GPIO_SetBits(GPIOB, LED_RED_PIN | LED_GREEN_PIN | LED_BLUE_PIN))

/*********************************************************************
 * PWM 参数（不需要手动计算，led_pwm_init() 内部使用下面的值）
 * 定时器计数频率 1MHz，PWM 频率 1kHz，亮度分辨率 1000 级
 */
#define LED_PWM_TIM_CLOCK       1000000UL
#define LED_PWM_FREQ_HZ         1000UL
#define LED_PWM_PRESCALER       ((SystemCoreClock / LED_PWM_TIM_CLOCK) - 1)
#define LED_PWM_PERIOD          ((LED_PWM_TIM_CLOCK / LED_PWM_FREQ_HZ) - 1)

/*********************************************************************
 * 类型定义
 */
typedef enum
{
    LED_COLOR_BLACK = 0,    /* 全灭 */
    LED_COLOR_WHITE,        /* 白 */
    LED_COLOR_RED,          /* 红 */
    LED_COLOR_GREEN,        /* 绿 */
    LED_COLOR_BLUE,         /* 蓝 */
    LED_COLOR_YELLOW,       /* 黄 */
    LED_COLOR_MAGENTA,      /* 品红 */
    LED_COLOR_CYAN          /* 青 */
} led_color_t;

typedef enum
{
    LED_ALL = 0,            /* 三色一起操作 */
    LED_RED = 1,            /* PB3 */
    LED_GREEN = 2,          /* PB4 */
    LED_BLUE = 3            /* PB5 */
} led_id_t;

/*********************************************************************
 * 全局变量
 */
extern uint8_t led_level;   /* 当前亮度百分比 0~100 */

/*********************************************************************
 * 函数
 */

/**
 * @brief  GPIO 方式初始化 LED（上电默认全灭）
 * @return 0 成功
 */
int led_init(void);

/**
 * @brief  点亮指定 LED（保持其它 LED 状态不变）
 * @param  id  LED_ALL / LED_RED / LED_GREEN / LED_BLUE
 */
void led_on(led_id_t id);

/**
 * @brief  熄灭指定 LED
 * @param  id  LED_ALL / LED_RED / LED_GREEN / LED_BLUE
 */
void led_off(led_id_t id);

/**
 * @brief  翻转指定 LED 状态
 * @param  id  LED_ALL / LED_RED / LED_GREEN / LED_BLUE
 */
void led_toggle(led_id_t id);

/**
 * @brief  按颜色显示（常用组合色）
 * @param  color  颜色枚举
 */
void led_color_set(led_color_t color);

/**
 * @brief  PWM 方式初始化（亮度可调），默认 1kHz / 100 级
 * @return 0 成功
 * @note   调用后 GPIO 由定时器 10 的 CH1~CH3 接管，不能再使用 led_on/led_off
 */
int led_pwm_init(void);

/**
 * @brief  设置亮度
 * @param  id       LED_ALL / LED_RED / LED_GREEN / LED_BLUE
 * @param  percent  0~100，大于 100 按 100 处理
 * @return 0 成功，-1 参数错误，-2 未调用 led_pwm_init
 */
int led_brightness_set(led_id_t id, uint8_t percent);

/**
 * @brief  按颜色闪烁
 * @param  color    颜色
 * @param  times    闪烁次数
 * @param  on_ms    亮的时间（毫秒）
 * @param  off_ms   灭的时间（毫秒）
 * @note   阻塞函数，结束以后 LED 保持熄灭
 */
void led_blink(led_color_t color, uint8_t times, uint16_t on_ms, uint16_t off_ms);

/*********************************************************************
 * 旧接口兼容（新代码请使用上面的函数）
 */
#define RED_ON()                LED_RED_ON()
#define RED_OFF()               LED_RED_OFF()
#define GREEN_ON()              LED_GREEN_ON()
#define GREEN_OFF()             LED_GREEN_OFF()
#define BLUE_ON()               LED_BLUE_ON()
#define BLUE_OFF()              LED_BLUE_OFF()
#define ALL_OFF()               LED_ALL_OFF()
#define led_color_change(c)     led_color_set(c)
#define led_pwm_control(id, period, per)   led_brightness_set((led_id_t)(id), (uint8_t)(per))
#define kBlack      LED_COLOR_BLACK
#define kWhite      LED_COLOR_WHITE
#define kRed        LED_COLOR_RED
#define kGreen      LED_COLOR_GREEN
#define kBlue       LED_COLOR_BLUE
#define kYellow     LED_COLOR_YELLOW
#define kMagenta    LED_COLOR_MAGENTA
#define kCyan       LED_COLOR_CYAN
#define kALL_LED    LED_ALL
#define kRED_LED    LED_RED
#define kGREEN_LED  LED_GREEN
#define kBLUE_LED   LED_BLUE

#ifdef __cplusplus
}
#endif

#endif
