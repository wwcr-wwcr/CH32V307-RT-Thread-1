/******************************************************************************
 * 文件名   : beep.h
 * 芯片     : CH32V307VCT6
 * 说明     : 无源蜂鸣器驱动（PA2 = TIM2_CH3，PWM 驱动）
 *
 * 使用示例:
 *     beep_init();
 *     beep_freq_set(2000);   // 2kHz 长鸣
 *     beep_stop();
 *     beep_once(100);        // 响 100ms
 *     beep_tone_play(BEEP_TONE_DO);
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误）
 ******************************************************************************/

#ifndef __BEEP_H
#define __BEEP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 引脚与定时器
 */
#define BEEP_GPIO_CLOCK     RCC_APB2Periph_GPIOA
#define BEEP_GPIO           GPIOA
#define BEEP_PIN            GPIO_Pin_2          /* PA2 = TIM2_CH3 */

/* 定时器计数频率 1MHz（1us 一个计数），周期按发声频率动态计算 */
#define BEEP_TIM_CLOCK      1000000UL
#define BEEP_TIM_PRESCALER  ((SystemCoreClock / BEEP_TIM_CLOCK) - 1)
#define BEEP_MIN_FREQ_HZ    100U
#define BEEP_MAX_FREQ_HZ    20000U

/*********************************************************************
 * 类型定义
 */
typedef enum
{
    BEEP_TONE_DO = 0,   /* 262Hz */
    BEEP_TONE_RE,       /* 294Hz */
    BEEP_TONE_MI,       /* 330Hz */
    BEEP_TONE_FA,       /* 349Hz */
    BEEP_TONE_SOL,      /* 392Hz */
    BEEP_TONE_LA,       /* 440Hz */
    BEEP_TONE_SI        /* 494Hz */
} beep_tone_t;

/*********************************************************************
 * 全局变量
 */
extern uint8_t beep_level;                  /* 当前占空比 0~100 */
extern const uint16_t beepToneFreq[7];      /* 七个音阶对应的频率(Hz) */

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化蜂鸣器（初始化后处于静音状态）
 * @return 0 成功
 */
int beep_init(void);

/**
 * @brief  按频率发声（占空比 50%）
 * @param  freq_hz  发声频率，100~20000Hz
 * @return 0 成功，-1 频率超出范围
 */
int beep_freq_set(uint16_t freq_hz);

/**
 * @brief  设置占空比（影响音量，一般 50% 最响）
 * @param  percent  0~100
 * @return 0 成功，-1 参数错误
 */
int beep_duty_set(uint8_t percent);

/**
 * @brief  停止发声
 */
void beep_stop(void);

/**
 * @brief  播放指定音阶
 * @param  tone  音阶
 * @return 0 成功，-1 参数错误
 */
int beep_tone_play(beep_tone_t tone);

/**
 * @brief  响一声后自动停止（阻塞）
 * @param  ms  持续时间（毫秒）
 */
void beep_once(uint16_t ms);

/*********************************************************************
 * 旧接口兼容
 */
#define beep_pwm_init(arr, psc, ccp)    beep_init()
#define beep_pwm_control(period, per)   beep_duty_set((uint8_t)(per))

/*********************************************************************
 * 宝宝函数（一行就能响，不用管频率）
 *********************************************************************/

/**
 * @brief  直接响（默认 2kHz，一直响到调 beep_off）
 * @return 0 成功
 */
int beep_on(void);

/**
 * @brief  停止发声
 */
void beep_off(void);

/**
 * @brief  响几声
 * @param  times   次数
 * @param  on_ms   每声时长（毫秒）
 * @param  off_ms  间隔（毫秒）
 */
void beep_times(uint8_t times, uint16_t on_ms, uint16_t off_ms);

#ifdef __cplusplus
}
#endif

#endif
