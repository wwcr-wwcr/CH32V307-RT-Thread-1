/******************************************************************************
 * 文件名   : fan.c
 * 芯片     : CH32V307VCT6
 * 说明     : 风扇 PWM 调速（TIM2_CH1 / PA0）
 ******************************************************************************/

#include "fan.h"

/**************************************************************************************************
 *                                        全局变量
 **************************************************************************************************/
uint8_t fan_level;      /* 当前转速百分比 0~100 */

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化风扇 PWM
 */
int fan_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = FAN_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(FAN_GPIO, &GPIO_InitStructure);

    TIM_TimeBaseInitStructure.TIM_Period        = FAN_PWM_PERIOD;
    TIM_TimeBaseInitStructure.TIM_Prescaler     = FAN_PWM_PRESCALER;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_Pulse       = 0;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(TIM2, &TIM_OCInitStructure);

    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);

    fan_level = 0;

    return 0;
}

/**
 * @brief  设置风扇转速
 */
int fan_set_speed(uint8_t percent)
{
    uint16_t duty;

    if(percent > 100)
    {
        return -1;
    }

    duty = (uint16_t)(((uint32_t)(FAN_PWM_PERIOD + 1) * percent) / 100);
    TIM_SetCompare1(TIM2, duty);
    fan_level = percent;

    return 0;
}

/**
 * @brief  停止风扇
 */
void fan_stop(void)
{
    TIM_SetCompare1(TIM2, 0);
    fan_level = 0;
}

/**
 * @brief  读取当前转速百分比
 */
uint8_t fan_get_speed(void)
{
    return fan_level;
}
