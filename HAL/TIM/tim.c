/******************************************************************************
 * 文件名   : tim.c
 * 芯片     : CH32V307VCT6
 * 说明     : 通用 PWM 输出（TIM2 多通道）
 ******************************************************************************/

#include "tim.h"

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
static uint16_t s_tim2_period = TIM2_PWM_PERIOD;    /* 当前自动重装值 */

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化 TIM2 的 PWM 通道
 */
int tim2_pwm_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_2 | GPIO_Pin_3;   /* PA0:CH1 PA2:CH3 PA3:CH4 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    s_tim2_period = TIM2_PWM_PERIOD;

    TIM_TimeBaseInitStructure.TIM_Period        = s_tim2_period;
    TIM_TimeBaseInitStructure.TIM_Prescaler     = TIM2_PWM_PRESCALER;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_Pulse       = 0;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(TIM2, &TIM_OCInitStructure);
    TIM_OC3Init(TIM2, &TIM_OCInitStructure);
    TIM_OC4Init(TIM2, &TIM_OCInitStructure);

    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_OC4PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);

    return 0;
}

/**
 * @brief  修改 PWM 频率
 */
int tim2_pwm_freq_set(uint32_t freq_hz)
{
    if(freq_hz < 1 || freq_hz > 100000)
    {
        return -1;
    }

    s_tim2_period = (uint16_t)(TIM2_PWM_TIM_CLOCK / freq_hz - 1);

    TIM_SetAutoreload(TIM2, s_tim2_period);
    TIM_SetCompare1(TIM2, 0);
    TIM_SetCompare3(TIM2, 0);
    TIM_SetCompare4(TIM2, 0);

    return 0;
}

/**
 * @brief  设置通道占空比
 */
int tim2_pwm_set(tim2_ch_t ch, uint8_t percent)
{
    uint16_t duty;

    if(percent > 100)
    {
        return -1;
    }

    duty = (uint16_t)(((uint32_t)(s_tim2_period + 1) * percent) / 100);

    switch(ch)
    {
    case TIM2_CH1:  TIM_SetCompare1(TIM2, duty); break;
    case TIM2_CH3:  TIM_SetCompare3(TIM2, duty); break;
    case TIM2_CH4:  TIM_SetCompare4(TIM2, duty); break;
    default:        return -1;
    }

    return 0;
}

/**
 * @brief  停止所有通道输出
 */
void tim2_pwm_stop(void)
{
    TIM_SetCompare1(TIM2, 0);
    TIM_SetCompare3(TIM2, 0);
    TIM_SetCompare4(TIM2, 0);
}
