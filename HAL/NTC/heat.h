/******************************************************************************
 * 文件名   : heat.h
 * 芯片     : CH32V307VCT6
 * 说明     : 加热片 PWM 功率控制（PA3 = TIM2_CH4）
 *
 * 使用示例:
 *     heat_init();
 *     heat_set_power(50);     // 50% 功率
 *     heat_off();
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误）
 ******************************************************************************/

#ifndef __HEAT_H
#define __HEAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 参数
 */
#define HEAT_GPIO           GPIOA
#define HEAT_PIN            GPIO_Pin_3          /* PA3 = TIM2_CH4 */
#define HEAT_PWM_TIM_CLOCK  1000000UL
#define HEAT_PWM_FREQ_HZ    1000UL              /* PWM 频率 1kHz */
#define HEAT_PWM_PRESCALER  ((SystemCoreClock / HEAT_PWM_TIM_CLOCK) - 1)
#define HEAT_PWM_PERIOD     ((HEAT_PWM_TIM_CLOCK / HEAT_PWM_FREQ_HZ) - 1)

/*********************************************************************
 * 全局变量
 */
extern uint8_t heat_level;      /* 当前功率百分比 0~100 */

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化加热片 PWM（上电关闭）
 * @return 0 成功
 */
int heat_init(void);

/**
 * @brief  设置加热功率
 * @param  percent  0~100，0 表示关闭
 * @return 0 成功，-1 参数错误
 */
int heat_set_power(uint8_t percent);

/**
 * @brief  关闭加热
 */
void heat_off(void);

/**
 * @brief  读取当前功率百分比
 * @return 0~100
 */
uint8_t heat_get_power(void);

/*********************************************************************
 * 旧接口兼容
 */
#define HEAT_CONTROL_TIM_CLOCK      HEAT_PWM_TIM_CLOCK
#define HEAT_CONTROL_TIM_PRESCALER  HEAT_PWM_PRESCALER
#define HEAT_CONTROL_TIM_PERIOD_1MS HEAT_PWM_PERIOD
#define heat_pwm_init(arr, psc, ccp)  heat_init()
#define heat_pwm_control(period, per) heat_set_power((uint8_t)(per))

#ifdef __cplusplus
}
#endif

#endif
