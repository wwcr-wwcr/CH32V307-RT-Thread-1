/******************************************************************************
 * 文件名   : beep.c
 * 芯片     : CH32V307VCT6
 * 说明     : 无源蜂鸣器驱动，按频率发声（TIM2_CH3 / PA2）
 ******************************************************************************/

#include "beep.h"
#include "rtthread.h"

/**************************************************************************************************
 *                                        全局变量
 **************************************************************************************************/
uint8_t beep_level;                                     /* 当前占空比 0~100 */
const uint16_t beepToneFreq[7] = {262, 294, 330, 349, 392, 440, 494};

static uint16_t s_beep_period = 999;                    /* 当前周期值（上电默认 1kHz） */

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化蜂鸣器
 */
int beep_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};

    RCC_APB2PeriphClockCmd(BEEP_GPIO_CLOCK, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = BEEP_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(BEEP_GPIO, &GPIO_InitStructure);

    s_beep_period = (uint16_t)(BEEP_TIM_CLOCK / 1000 - 1);           /* 默认 1kHz */

    TIM_TimeBaseInitStructure.TIM_Period        = s_beep_period;
    TIM_TimeBaseInitStructure.TIM_Prescaler     = BEEP_TIM_PRESCALER;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_Pulse       = 0;                         /* 上电静音 */
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC3Init(TIM2, &TIM_OCInitStructure);

    TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);                                          /* 计数运行，占空比 0 即静音 */

    beep_level = 0;

    return 0;
}

/**
 * @brief  按频率发声
 */
int beep_freq_set(uint16_t freq_hz)
{
    uint16_t period;

    if(freq_hz < BEEP_MIN_FREQ_HZ || freq_hz > BEEP_MAX_FREQ_HZ)
    {
        return -1;
    }

    period = (uint16_t)(BEEP_TIM_CLOCK / freq_hz - 1);

    TIM_SetAutoreload(TIM2, period);
    TIM_SetCompare3(TIM2, (uint16_t)((period + 1) / 2));            /* 50% 占空比 */

    s_beep_period = period;
    beep_level    = 50;

    return 0;
}

/**
 * @brief  设置占空比
 */
int beep_duty_set(uint8_t percent)
{
    if(percent > 100)
    {
        return -1;
    }

    TIM_SetCompare3(TIM2, (uint16_t)(((uint32_t)(s_beep_period + 1) * percent) / 100));
    beep_level = percent;

    return 0;
}

/**
 * @brief  停止发声
 */
void beep_stop(void)
{
    TIM_SetCompare3(TIM2, 0);
    beep_level = 0;
}

/**
 * @brief  播放指定音阶
 */
int beep_tone_play(beep_tone_t tone)
{
    if(tone > BEEP_TONE_SI)
    {
        return -1;
    }

    return beep_freq_set(beepToneFreq[tone]);
}

/**
 * @brief  响一声后自动停止
 */
void beep_once(uint16_t ms)
{
    beep_freq_set(2000);
    rt_thread_mdelay(ms);
    beep_stop();
}

/**************************************************************************************************
 *                                        宝宝函数
 **************************************************************************************************/
/**
 * @brief  直接响
 */
int beep_on(void)
{
    return beep_freq_set(2000);
}

/**
 * @brief  停止发声
 */
void beep_off(void)
{
    beep_stop();
}

/**
 * @brief  响几声
 */
void beep_times(uint8_t times, uint16_t on_ms, uint16_t off_ms)
{
    for(uint8_t i = 0; i < times; i++)
    {
        (void)beep_freq_set(2000);
        rt_thread_mdelay(on_ms);
        beep_stop();
        rt_thread_mdelay(off_ms);
    }
}
