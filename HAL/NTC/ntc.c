/******************************************************************************
 * 文件名   : ntc.c
 * 芯片     : CH32V307VCT6
 * 说明     : NTC 热敏电阻测温（B 值法）
 ******************************************************************************/

#include "ntc.h"
#include "adc.h"
#include "math.h"

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化 NTC 测温
 */
int ntc_init(void)
{
    return adc_init();
}

/**
 * @brief  读取 NTC 当前阻值
 */
uint32_t ntc_resistance_get(void)
{
    uint16_t adc;
    uint32_t res;

    adc = adc_read_avg(NTC_ADC_CHANNEL, NTC_ADC_SAMPLE_TIMES);

    if(adc == 0)
    {
        return 0;
    }
    if(adc >= 4095)
    {
        return 0xFFFFFFFF;      /* 开路或短路，返回极大值 */
    }

    res = (uint32_t)(((uint64_t)adc * NTC_SERIES_RESISTOR) / (4095 - adc));

    return res;
}

/**
 * @brief  由 ADC 采样值换算温度
 */
float ntc_temperature_convert(uint16_t adc)
{
    float res;
    float temp_k;

    if(adc == 0 || adc >= 4095)
    {
        return 0.0f;            /* 采样异常，直接返回 0 */
    }

    res = (float)adc * (float)NTC_SERIES_RESISTOR / (float)(4095 - adc);

    /* T = 1 / (1/T0 + ln(R/R0) / B) */
    temp_k = 1.0f / ((1.0f / NTC_NOMINAL_TEMP_K) + (logf(res / (float)NTC_NOMINAL_RESISTOR) / NTC_B_VALUE));

    return temp_k - 273.15f;
}

/**
 * @brief  读取温度（摄氏度）
 */
float ntc_temperature_get(void)
{
    return ntc_temperature_convert(adc_read_avg(NTC_ADC_CHANNEL, NTC_ADC_SAMPLE_TIMES));
}

/**
 * @brief  读取温度（0.1℃）
 */
int16_t ntc_temperature_x10_get(void)
{
    float t = ntc_temperature_get();

    if(t < 0)
    {
        return (int16_t)(t * 10.0f - 0.5f);
    }

    return (int16_t)(t * 10.0f + 0.5f);
}
