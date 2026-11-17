/******************************************************************************
 * 文件名   : tim.h
 * 芯片     : CH32V307VCT6
 * 说明     : 通用 PWM 输出（TIM2：PA0=CH1，PA2=CH3，PA3=CH4）
 *
 * 使用示例:
 *     tim2_pwm_init();                 // 默认 1kHz
 *     tim2_pwm_set(TIM2_CH1, 50);      // 通道 1 输出 50% 占空比
 *     tim2_pwm_freq_set(20000);        // 改成 20kHz
 *     tim2_pwm_stop();                 // 所有通道输出 0
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误）
 ******************************************************************************/

#ifndef __TIM_H
#define __TIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 定时器参数
 */
#define TIM2_PWM_TIM_CLOCK      1000000UL                                   /* 计数频率 1MHz */
#define TIM2_PWM_PRESCALER      ((SystemCoreClock / TIM2_PWM_TIM_CLOCK) - 1)
#define TIM2_PWM_DEFAULT_FREQ   1000UL                                      /* 默认 1kHz */
#define TIM2_PWM_PERIOD         ((TIM2_PWM_TIM_CLOCK / TIM2_PWM_DEFAULT_FREQ) - 1)

/*********************************************************************
 * 类型定义
 */
typedef enum
{
    TIM2_CH1 = 1,       /* PA0 */
    TIM2_CH3 = 3,       /* PA2 */
    TIM2_CH4 = 4        /* PA3 */
} tim2_ch_t;

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化 TIM2 的三个 PWM 通道（默认 1kHz，占空比 0）
 * @return 0 成功
 */
int tim2_pwm_init(void);

/**
 * @brief  修改 PWM 频率（三个通道同时生效）
 * @param  freq_hz  1~100000 Hz
 * @return 0 成功，-1 频率超出范围
 */
int tim2_pwm_freq_set(uint32_t freq_hz);

/**
 * @brief  设置某个通道的占空比
 * @param  ch       TIM2_CH1 / TIM2_CH3 / TIM2_CH4
 * @param  percent  0~100
 * @return 0 成功，-1 参数错误
 */
int tim2_pwm_set(tim2_ch_t ch, uint8_t percent);

/**
 * @brief  三个通道全部输出 0
 */
void tim2_pwm_stop(void);

/*********************************************************************
 * 旧接口兼容
 */
#define DEFAULT_CONTROL_TIM_CLOCK       TIM2_PWM_TIM_CLOCK
#define DEFAULT_CONTROL_TIM_PRESCALER   TIM2_PWM_PRESCALER
#define DEFAULT_CONTROL_TIM_PERIOD_1MS  TIM2_PWM_PERIOD

#ifdef __cplusplus
}
#endif

#endif
