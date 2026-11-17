/******************************************************************************
 * 文件名   : encoder.c
 * 芯片     : CH32V307VCT6
 * 说明     : 增量式编码器输入，TIM3 编码器模式，软件累加计数
 ******************************************************************************/

#include "encoder.h"
#include "rtthread.h"

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
static int16_t  s_last_count = 0;       /* 上一次读取的硬件计数 */
static int32_t  s_total      = 0;       /* 软件累计计数 */
static uint8_t  s_inited     = 0;

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化编码器接口
 */
int encoder_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    TIM_ICInitTypeDef TIM_ICInitStructure = {0};

    RCC_APB2PeriphClockCmd(ENCODER_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = ENCODER_PIN_A | ENCODER_PIN_B;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ENCODER_PORT, &GPIO_InitStructure);

    TIM_TimeBaseStructure.TIM_Prescaler     = 0;
    TIM_TimeBaseStructure.TIM_Period        = 0xFFFF;          /* 16 位满量程计数 */
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    TIM_ICInitStructure.TIM_ICFilter    = 0x00;

    TIM_ICInitStructure.TIM_Channel     = TIM_Channel_1;
    TIM_ICInitStructure.TIM_ICPolarity  = TIM_ICPolarity_Rising;
    TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI;
    TIM_ICInit(TIM3, &TIM_ICInitStructure);

    TIM_ICInitStructure.TIM_Channel     = TIM_Channel_2;
    TIM_ICInitStructure.TIM_ICPolarity  = TIM_ICPolarity_Rising;
    TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI;
    TIM_ICInit(TIM3, &TIM_ICInitStructure);

    /* TI1 + TI2 双边沿计数，一圈 4 个计数 */
    TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);

    TIM_SetCounter(TIM3, 0);
    TIM_Cmd(TIM3, ENABLE);

    s_last_count = 0;
    s_total      = 0;
    s_inited     = 1;

    return 0;
}

/**
 * @brief  读取增量计数
 */
int16_t encoder_count_get(void)
{
    int16_t now;
    int16_t delta;

    if(!s_inited)
    {
        return 0;
    }

    now   = (int16_t)TIM_GetCounter(TIM3);
    delta = (int16_t)(now - s_last_count);       /* 16 位回绕也能得到正确增量 */
    s_last_count = now;
    s_total     += delta;

    return delta;
}

/**
 * @brief  读取累计计数
 */
int32_t encoder_total_get(void)
{
    (void)encoder_count_get();                   /* 先把硬件计数并入累计值 */
    return s_total;
}

/**
 * @brief  清零累计计数
 */
void encoder_reset(void)
{
    TIM_SetCounter(TIM3, 0);
    s_last_count = 0;
    s_total      = 0;
}

/**
 * @brief  测量转速（阻塞采样）
 */
float encoder_speed_rpm(uint32_t period_ms)
{
    int16_t count;

    if(period_ms == 0 || !s_inited)
    {
        return 0.0f;
    }

    (void)encoder_count_get();                   /* 丢掉上次残留的增量 */
    rt_thread_mdelay(period_ms);
    count = encoder_count_get();

    return (count * 60000.0f) / (period_ms * (float)ENCODER_COUNTS_PER_REV);
}

/**
 * @brief  直接测转速
 */
float encoder_rpm(void)
{
    return encoder_speed_rpm(100);
}

/**
 * @brief  转向
 */
int8_t encoder_direction(void)
{
    int16_t delta = encoder_count_get();

    if(delta > 0) { return 1;  }
    if(delta < 0) { return -1; }

    return 0;
}
