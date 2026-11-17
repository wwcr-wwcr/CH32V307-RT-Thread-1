/******************************************************************************
 * 文件名   : motor.h
 * 芯片     : CH32V307VCT6
 * 说明     : 直流电机驱动（TIM1：PA8=CH1，PA11=CH4，正反转 + 调速）
 *
 * 硬件说明: CH1 驱动一路、CH4 驱动另一路，两路互补实现正反转
 *           占空比越大转速越快，符号表示方向
 *
 * 使用示例:
 *     motor_init();
 *     motor_set_speed(60);     // 正转 60%
 *     motor_set_speed(-30);    // 反转 30%
 *     motor_stop();
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误）
 ******************************************************************************/

#ifndef __MOTOR_H
#define __MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 定时器参数
 */
#define MOTOR_PWM_TIM_CLOCK     1000000UL                           /* 计数频率 1MHz */
#define MOTOR_PWM_FREQ_HZ       10000UL                             /* PWM 频率 10kHz */
#define MOTOR_PWM_PRESCALER     ((SystemCoreClock / MOTOR_PWM_TIM_CLOCK) - 1)
#define MOTOR_PWM_PERIOD        ((MOTOR_PWM_TIM_CLOCK / MOTOR_PWM_FREQ_HZ) - 1)

#ifndef ABS
#define ABS(n)     (((n) < 0) ? -(n) : (n))
#endif

/*********************************************************************
 * 全局变量
 */
extern int8_t motor_level;      /* 当前转速百分比，正=正转，负=反转 */

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化电机 PWM（默认 10kHz，上电停转）
 * @return 0 成功
 */
int motor_init(void);

/**
 * @brief  设置转速与方向
 * @param  percent  -100~100，正数正转、负数反转、0 停止
 * @return 0 成功，-1 参数错误
 */
int motor_set_speed(int16_t percent);

/**
 * @brief  停止电机
 */
void motor_stop(void);

/**
 * @brief  读取当前转速百分比
 * @return -100~100
 */
int8_t motor_get_speed(void);

/**
 * @brief  由编码器计数换算转速（转/分）
 * @param  count     采样周期内的编码器计数
 * @param  time_ms   采样周期（毫秒）
 * @return 转速 rpm
 */
float motor_speed_calculate(int16_t count, uint32_t time_ms);

/*********************************************************************
 * 旧接口兼容
 */
#define MOTOR_CONTROL_TIM_CLOCK         MOTOR_PWM_TIM_CLOCK
#define MOTOR_CONTROL_TIM_PRESCALER     MOTOR_PWM_PRESCALER
#define MOTOR_CONTROL_TIM_PERIOD_10MS   MOTOR_PWM_PERIOD
#define motor_pwm_init(arr, psc, ccp)   motor_init()
#define motor_pwm_control(period, per)  motor_set_speed((motor_level < 0) ? -(int16_t)(per) : (int16_t)(per))

/*********************************************************************
 * 宝宝函数（不用管正负号）
 *********************************************************************/

/**
 * @brief  正转
 * @param  percent  0~100
 * @return 0 成功
 */
int motor_forward(uint8_t percent);

/**
 * @brief  反转
 * @param  percent  0~100
 * @return 0 成功
 */
int motor_reverse(uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif
