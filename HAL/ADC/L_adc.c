/******************************************************************************
 * 文件名   : L_adc.c
 * 芯片     : CH32V307VCT6
 * 说明     : ADC1 定时器触发采样（TIM3 TRGO -> ADC 外部触发，更新中断取数）
 ******************************************************************************/

#include "L_adc.h"
#include "rtthread.h"
#include "rthw.h"

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
static volatile uint16_t s_adc_value = 0;       /* 最近一次采样值 */
static adc_timer_cb_t    s_adc_cb    = NULL;    /* 用户回调 */

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  注册采样回调
 */
void adc_timer_callback_set(adc_timer_cb_t cb)
{
    s_adc_cb = cb;
}

/**
 * @brief  启动定时器触发采样
 */
int adc_timer_start(uint8_t channel, uint32_t sample_rate_hz)
{
    ADC_InitTypeDef ADC_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};
    uint16_t arr;

    if(sample_rate_hz == 0 || sample_rate_hz > ADC_TIMER_MAX_RATE_HZ)
    {
        return -1;
    }

    arr = (uint16_t)(ADC_TIMER_CLOCK / sample_rate_hz - 1);

    /* 1. 先按定时器触发模式重新配置 ADC1 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    /* PCLK2 = 72MHz（APB2 二分频），ADC /8 = 9MHz，低于 14MHz 上限 */
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    ADC_DeInit(ADC1);
    ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode       = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;                     /* 单次转换：一次触发一次转换 */
    ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_T3_TRGO;
    ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    (void)adc_channel_init(channel);
    ADC_RegularChannelConfig(ADC1, channel, 1, ADC_DEFAULT_SAMPLE_TIME);

    ADC_Cmd(ADC1, ENABLE);

    ADC_BufferCmd(ADC1, DISABLE);
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1))
    {
        /* 等待复位校准完成 */
    }
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1))
    {
        /* 等待校准完成 */
    }

    ADC_ExternalTrigConvCmd(ADC1, ENABLE);

    /* 2. 配置 TIM3：1MHz 计数，更新事件作为 ADC 触发源 */
    TIM_TimeBaseInitStructure.TIM_Period        = arr;
    TIM_TimeBaseInitStructure.TIM_Prescaler     = ADC_TIMER_PRESCALER;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);

    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);

    NVIC_InitStructure.NVIC_IRQChannel                   = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM3, ENABLE);

    return 0;
}

/**
 * @brief  停止采样
 */
void adc_timer_stop(void)
{
    TIM_Cmd(TIM3, DISABLE);
    TIM_ITConfig(TIM3, TIM_IT_Update, DISABLE);
    ADC_ExternalTrigConvCmd(ADC1, DISABLE);
    ADC_Cmd(ADC1, DISABLE);

    TIM_DeInit(TIM3);
    ADC_DeInit(ADC1);
}

/**
 * @brief  读取最近一次采样值
 */
uint16_t adc_timer_value_get(void)
{
    return s_adc_value;
}

/**************************************************************************************************
 *                                        中断服务
 **************************************************************************************************/
/* TIM3 更新中断：每次触发一次 ADC 转换，把结果交给上层 */
void TIM3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM3_IRQHandler(void)
{
    GET_INT_SP();
    rt_interrupt_enter();

    if(TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        if(ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) != RESET)
        {
            s_adc_value = adc_value_calibrate((int16_t)ADC_GetConversionValue(ADC1));
            ADC_ClearFlag(ADC1, ADC_FLAG_EOC);

            if(s_adc_cb != NULL)
            {
                s_adc_cb(s_adc_value);
            }
        }
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    }

    rt_interrupt_leave();
    FREE_INT_SP();
}
