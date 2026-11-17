/******************************************************************************
 * 文件名   : motor.c
 * 芯片     : CH32V307VCT6
 * 说明     : 直流电机驱动（TIM1 双通道互补输出，正反转 + 调速）
 ******************************************************************************/

#include "motor.h"
#include "rtthread.h"

/**************************************************************************************************
 *                                        全局变量
 **************************************************************************************************/
int8_t motor_level;     /* 当前转速百分比，正=正转，负=反转 */

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化电机 PWM
 */
int motor_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_11;       /* PA8:CH1  PA11:CH4 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    TIM_TimeBaseInitStructure.TIM_Period        = MOTOR_PWM_PERIOD;
    TIM_TimeBaseInitStructure.TIM_Prescaler     = MOTOR_PWM_PRESCALER;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseInitStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_Pulse       = 0;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Reset;
    TIM_OC1Init(TIM1, &TIM_OCInitStructure);
    TIM_OC4Init(TIM1, &TIM_OCInitStructure);

    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_OC4PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_Cmd(TIM1, ENABLE);

    motor_level = 0;

    return 0;
}

/**
 * @brief  设置转速与方向
 */
int motor_set_speed(int16_t percent)
{
    uint16_t duty;
    int16_t  speed = percent;

    if(speed > 100)  { speed = 100;  }
    if(speed < -100) { speed = -100; }

    duty = (uint16_t)(((uint32_t)(MOTOR_PWM_PERIOD + 1) * ABS(speed)) / 100);

    if(speed > 0)               /* 正转：CH4 输出，CH1 关断 */
    {
        TIM_SetCompare1(TIM1, 0);
        TIM_SetCompare4(TIM1, duty);
    }
    else if(speed < 0)          /* 反转：CH1 输出，CH4 关断 */
    {
        TIM_SetCompare4(TIM1, 0);
        TIM_SetCompare1(TIM1, duty);
    }
    else                        /* 停止：两路都关断 */
    {
        TIM_SetCompare1(TIM1, 0);
        TIM_SetCompare4(TIM1, 0);
    }

    motor_level = (int8_t)speed;

    return 0;
}

/**
 * @brief  停止电机
 */
void motor_stop(void)
{
    TIM_SetCompare1(TIM1, 0);
    TIM_SetCompare4(TIM1, 0);
    motor_level = 0;
}

/**
 * @brief  读取当前转速百分比
 */
int8_t motor_get_speed(void)
{
    return motor_level;
}

/**
 * @brief  由编码器计数换算转速
 * @note   默认编码器 4 倍频，电机转一圈 4 个计数（与硬件相关的系数可按实际修改）
 */
float motor_speed_calculate(int16_t count, uint32_t time_ms)
{
    if(time_ms == 0)
    {
        return 0.0f;
    }

    return (count * 60000.0f) / (time_ms * 4.0f);
}

/**************************************************************************************************
 *                                        宝宝函数
 **************************************************************************************************/
/**
 * @brief  正转
 */
int motor_forward(uint8_t percent)
{
    return motor_set_speed((int16_t)percent);
}

/**
 * @brief  反转
 */
int motor_reverse(uint8_t percent)
{
    return motor_set_speed(-(int16_t)percent);
}
