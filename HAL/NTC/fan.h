/******************************************************************************
 * 文件名   : fan.h
 * 芯片     : CH32V307VCT6
 * 说明     : 风扇 PWM 调速（PA0 = TIM2_CH1）
 *
 * 使用示例:
 *     fan_init();
 *     fan_set_speed(40);      // 40% 转速
 *     fan_stop();
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误）
 ******************************************************************************/

#ifndef __FAN_H
#define __FAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 参数
 */
#define FAN_GPIO            GPIOA
#define FAN_PIN             GPIO_Pin_0          /* PA0 = TIM2_CH1 */
#define FAN_PWM_TIM_CLOCK   1000000UL
#define FAN_PWM_FREQ_HZ     1000UL              /* PWM 频率 1kHz */
#define FAN_PWM_PRESCALER   ((SystemCoreClock / FAN_PWM_TIM_CLOCK) - 1)
#define FAN_PWM_PERIOD      ((FAN_PWM_TIM_CLOCK / FAN_PWM_FREQ_HZ) - 1)

/*********************************************************************
 * 全局变量
 */
extern uint8_t fan_level;       /* 当前转速百分比 0~100 */

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化风扇 PWM（上电停转）
 * @return 0 成功
 */
int fan_init(void);

/**
 * @brief  设置风扇转速
 * @param  percent  0~100，0 表示停转
 * @return 0 成功，-1 参数错误
 */
int fan_set_speed(uint8_t percent);

/**
 * @brief  停止风扇
 */
void fan_stop(void);

/**
 * @brief  读取当前转速百分比
 * @return 0~100
 */
uint8_t fan_get_speed(void);

/*********************************************************************
 * 旧接口兼容
 */
#define FAN_CONTROL_TIM_CLOCK       FAN_PWM_TIM_CLOCK
#define FAN_CONTROL_TIM_PRESCALER   FAN_PWM_PRESCALER
#define FAN_CONTROL_TIM_PERIOD_1MS  FAN_PWM_PERIOD
#define fan_pwm_init(arr, psc, ccp) fan_init()
#define fan_pwm_control(period, per) fan_set_speed((uint8_t)(per))

#ifdef __cplusplus
}
#endif

#endif
