/******************************************************************************
 * 文件名   : adc.c
 * 芯片     : CH32V307VCT6
 * 说明     : ADC1 单次转换驱动
 ******************************************************************************/

#include "adc.h"
#include "rtthread.h"
#include "rthw.h"

/**************************************************************************************************
 *                                        全局变量
 **************************************************************************************************/
int16_t Calibrattion_Val = 0;       /* ADC1 校准值 */

static uint8_t s_adc_inited = 0;

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* 通道号 -> 引脚，返回 0 表示该通道本板没有引出 */
static uint8_t adc_pin_of_channel(uint8_t ch, GPIO_TypeDef **port, uint16_t *pin)
{
    if(ch <= ADC_Channel_7)                         /* PA0~PA7 */
    {
        *port = GPIOA;
        *pin  = (uint16_t)(GPIO_Pin_0 << ch);
        return 1;
    }

    if(ch == ADC_Channel_8)                         /* PB0 */
    {
        *port = GPIOB;
        *pin  = GPIO_Pin_0;
        return 1;
    }

    if(ch == ADC_Channel_9)                         /* PB1 */
    {
        *port = GPIOB;
        *pin  = GPIO_Pin_1;
        return 1;
    }

    return 0;
}

/* 启动一次转换并等待结果 */
/* 等 EOC 的最长循环次数。ADC 一次转换在 9MHz ADCCLK 下约 4.5us，
 * 这里给到 ~5ms，正常情况连零头都用不到，超时只可能是 ADC1 被别的模块改过配置。 */
#define ADC_EOC_TIMEOUT     200000u

/*
 * 做一次转换并等待结束。
 *
 * 为什么要加超时 + 自愈：
 *     adc_dma.c（示波器 / FFT）用的是同一个 ADC1，它把 ADC1 配成 T3_TRGO 外部触发
 *     + ADC_DMACmd 之后不还原。这时候本函数里的 ADC_SoftwareStartConvCmd 根本发不出
 *     转换，原来那个无限等 EOC 的 while 会把整个线程永久卡死（K4 都退不出来）。
 *     所以超时就认为 ADC1 被抢过配置：关掉外部触发和 DMA，重新初始化再试一次。
 */
static uint16_t adc_convert_once(uint8_t ch)
{
    static uint16_t s_last = 0;
    uint8_t retry;

    for(retry = 0; retry < 2u; retry++)
    {
        uint32_t guard = ADC_EOC_TIMEOUT;

        ADC_RegularChannelConfig(ADC1, ch, 1, ADC_DEFAULT_SAMPLE_TIME);
        ADC_ClearFlag(ADC1, ADC_FLAG_EOC);          /* 清掉上一轮没取走的标志 */
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);

        while(ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
        {
            if(--guard == 0u)
            {
                break;
            }
        }

        if(ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) != RESET)
        {
            s_last = ADC_GetConversionValue(ADC1);
            return s_last;
        }

        /* 超时：把外部触发和 DMA 关掉，重新配回单次软件触发模式，再试一次 */
        ADC_DMACmd(ADC1, DISABLE);
        ADC_ExternalTrigConvCmd(ADC1, DISABLE);
        (void)adc_init();
    }

    return s_last;                                  /* 两次都没成就返回上一次的值 */
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化 ADC1
 */
int adc_init(void)
{
    ADC_InitTypeDef ADC_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    /* PCLK2 = 72MHz（APB2 二分频），ADC /8 = 9MHz，低于 14MHz 上限 */
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    ADC_DeInit(ADC1);
    ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode       = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;     /* 单次转换 */
    ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_Cmd(ADC1, ENABLE);

    ADC_BufferCmd(ADC1, DISABLE);                   /* 校准前必须关闭 Buffer */
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
    Calibrattion_Val = Get_CalibrationValue(ADC1);

    s_adc_inited = 1;

    return 0;
}

/**
 * @brief  配置通道对应的引脚
 */
int adc_channel_init(uint8_t ch)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_TypeDef *port = NULL;
    uint16_t pin = 0;

    if(!adc_pin_of_channel(ch, &port, &pin))
    {
        return -1;
    }

    if(port == GPIOA)
    {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    }
    else
    {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    }

    GPIO_InitStructure.GPIO_Pin  = pin;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(port, &GPIO_InitStructure);

    return 0;
}

/**
 * @brief  单次采样
 */
uint16_t adc_read(uint8_t ch)
{
    uint16_t val;

    if(!s_adc_inited)
    {
        adc_init();
    }

    /* 通道 0~7 之外第一次使用时自动配置引脚，避免忘记初始化 */
    (void)adc_channel_init(ch);

    val = adc_convert_once(ch);

    return adc_value_calibrate((int16_t)val);
}

/**
 * @brief  多次采样取平均
 */
uint16_t adc_read_avg(uint8_t ch, uint8_t times)
{
    uint32_t sum = 0;

    if(times == 0)
    {
        times = 1;
    }

    if(!s_adc_inited)
    {
        adc_init();
    }

    (void)adc_channel_init(ch);

    (void)adc_convert_once(ch);             /* 丢掉第一次，等内部采样电容稳定 */
    rt_hw_us_delay(50);

    for(uint8_t i = 0; i < times; i++)
    {
        sum += adc_convert_once(ch);
        rt_hw_us_delay(50);
    }

    return adc_value_calibrate((int16_t)((sum + times / 2) / times));
}

/**
 * @brief  ADC 值换算电压
 */
uint32_t adc_to_mv(uint16_t val)
{
    return ((uint32_t)val * ADC_VREF_MV + ADC_MAX_VALUE / 2) / ADC_MAX_VALUE;
}

/**
 * @brief  单次采样并换算电压
 */
uint32_t adc_read_mv(uint8_t ch)
{
    return adc_to_mv(adc_read(ch));
}

/**
 * @brief  多次采样平均后换算电压
 */
uint32_t adc_read_avg_mv(uint8_t ch, uint8_t times)
{
    return adc_to_mv(adc_read_avg(ch, times));
}

/**
 * @brief  采样值校准
 */
uint16_t adc_value_calibrate(int16_t val)
{
    int16_t calibrated = (int16_t)(val + Calibrattion_Val);

    if(calibrated < 0)
    {
        return 0;
    }
    if(calibrated > (int16_t)ADC_MAX_VALUE)
    {
        return ADC_MAX_VALUE;
    }

    return (uint16_t)calibrated;
}

/**
 * @brief  采样并换算成百分比
 */
uint8_t adc_percent(uint8_t ch)
{
    uint32_t mv = adc_read_avg_mv(ch, ADC_DEFAULT_AVG_TIMES);

    return (uint8_t)((mv * 100UL + ADC_VREF_MV / 2) / ADC_VREF_MV);
}
