/******************************************************************************
 * 文件名   : heat.c
 * 芯片     : CH32V307VCT6
 * 说明     : 加热片 PWM 功率控制（TIM2_CH4 / PA3）
 ******************************************************************************/

#include "heat.h"

/**************************************************************************************************
 *                                        全局变量
 **************************************************************************************************/
uint8_t heat_level;     /* 当前功率百分比 0~100 */

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化加热片 PWM
 */
int heat_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = HEAT_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(HEAT_GPIO, &GPIO_InitStructure);

    TIM_TimeBaseInitStructure.TIM_Period        = HEAT_PWM_PERIOD;
    TIM_TimeBaseInitStructure.TIM_Prescaler     = HEAT_PWM_PRESCALER;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_Pulse       = 0;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC4Init(TIM2, &TIM_OCInitStructure);

    TIM_OC4PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);

    heat_level = 0;

    return 0;
}

/**
 * @brief  设置加热功率
 */
int heat_set_power(uint8_t percent)
{
    uint16_t duty;

    if(percent > 100)
    {
        return -1;
    }

    duty = (uint16_t)(((uint32_t)(HEAT_PWM_PERIOD + 1) * percent) / 100);
    TIM_SetCompare4(TIM2, duty);
    heat_level = percent;

    return 0;
}

/**
 * @brief  关闭加热
 */
void heat_off(void)
{
    TIM_SetCompare4(TIM2, 0);
    heat_level = 0;
}

/**
 * @brief  读取当前功率百分比
 */
uint8_t heat_get_power(void)
{
    return heat_level;
}
