/******************************************************************************
 * 文件名   : ntc.h
 * 芯片     : CH32V307VCT6
 * 说明     : NTC 热敏电阻测温（ADC1 通道 8，PB0）
 *
 * 电路说明: NTC 与 10kΩ 电阻串联分压，NTC 接地，B 值 3380K
 *
 * 使用示例:
 *     ntc_init();
 *     float t = ntc_temperature_get();        // 直接拿到摄氏度
 *     int16_t t10 = ntc_temperature_x10_get();// 0.1℃ 精度，方便显示
 *
 * 返回值约定: 0 = 成功，负数 = 失败
 ******************************************************************************/

#ifndef __NTC_H
#define __NTC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 参数
 */
#define NTC_ADC_CHANNEL         ADC_Channel_8   /* PB0 */
#define NTC_ADC_SAMPLE_TIMES    16              /* 多次采样取平均 */
#define NTC_SERIES_RESISTOR     10000UL         /* 分压电阻 10kΩ */
#define NTC_NOMINAL_RESISTOR    10000UL         /* 25℃ 时的阻值 10kΩ */
#define NTC_NOMINAL_TEMP_K      298.15f         /* 25℃ 对应的开尔文温度 */
#define NTC_B_VALUE             3380.0f         /* B 常数 */

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化 NTC 测温（初始化 ADC1 单次转换）
 * @return 0 成功
 */
int ntc_init(void);

/**
 * @brief  读取温度（摄氏度）
 * @return 温度值（℃），读数异常时返回 0
 */
float ntc_temperature_get(void);

/**
 * @brief  读取温度（0.1℃ 为单位，方便直接显示）
 * @return 例如 256 表示 25.6℃
 */
int16_t ntc_temperature_x10_get(void);

/**
 * @brief  读取 NTC 当前阻值
 * @return 阻值（Ω）
 */
uint32_t ntc_resistance_get(void);

/**
 * @brief  由 ADC 采样值换算温度
 * @param  adc  ADC 原始值（0~4095）
 * @return 温度（℃）
 */
float ntc_temperature_convert(uint16_t adc);

/*********************************************************************
 * 旧接口兼容
 */
#define ntc_get_temperature()   ((uint32_t)ntc_temperature_x10_get())

#ifdef __cplusplus
}
#endif

#endif
