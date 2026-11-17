/******************************************************************************
 * 文件名   : adc.h
 * 芯片     : CH32V307VCT6
 * 说明     : ADC1 单次转换驱动（轮询方式，适合低速采样场景）
 *
 * 设计说明:
 *   - 通道号与引脚的对应关系在 adc_channel_init() 内部自动处理，不用手写 GPIO 配置；
 *   - 常用场景直接调用 adc_read_avg() / adc_read_avg_mv()，一行拿到稳定值；
 *   - 需要等间隔高速采样请看 L_adc.h（定时器触发方式）。
 *
 * 使用示例:
 *     adc_init();
 *     uint16_t raw = adc_read_avg(ADC_Channel_8, 16);   // PB0 采 16 次取平均
 *     uint32_t mv  = adc_read_avg_mv(ADC_Channel_8, 16);
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误）
 ******************************************************************************/

#ifndef __ADC_H
#define __ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 常量
 */
#define ADC_MAX_VALUE           4095U       /* 12 位 ADC 满量程 */
#define ADC_VREF_MV             3300U       /* 参考电压（mV） */
#define ADC_DEFAULT_SAMPLE_TIME ADC_SampleTime_239Cycles5
#define ADC_DEFAULT_AVG_TIMES   16

/*********************************************************************
 * 全局变量
 */
extern int16_t Calibrattion_Val;            /* ADC1 校准值 */

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化 ADC1（独立模式、单次转换、右对齐、软件触发）
 * @return 0 成功
 */
int adc_init(void);

/**
 * @brief  按通道号配置对应引脚为模拟输入
 * @param  ch  ADC 通道号（ADC_Channel_0 ~ ADC_Channel_15）
 * @return 0 成功，-1 该通道不在本板支持范围内
 * @note   本板引出的通道：PA0~PA7 -> 0~7，PB0 -> 8，PB1 -> 9
 */
int adc_channel_init(uint8_t ch);

/**
 * @brief  单次采样
 * @param  ch  ADC 通道号
 * @return 采样值 0~4095，参数错误返回 0
 */
uint16_t adc_read(uint8_t ch);

/**
 * @brief  多次采样取平均
 * @param  ch     ADC 通道号
 * @param  times  采样次数（1~255）
 * @return 平均值 0~4095
 */
uint16_t adc_read_avg(uint8_t ch, uint8_t times);

/**
 * @brief  ADC 原始值换算成电压
 * @param  val  0~4095
 * @return 电压（mV）
 */
uint32_t adc_to_mv(uint16_t val);

/**
 * @brief  单次采样并换算电压（mV）
 */
uint32_t adc_read_mv(uint8_t ch);

/**
 * @brief  多次采样平均后换算电压（mV）
 */
uint32_t adc_read_avg_mv(uint8_t ch, uint8_t times);

/**
 * @brief  对采样值做校准（加校准偏移并限幅到 0~4095）
 * @param  val  原始采样值
 * @return 校准后的值
 */
uint16_t adc_value_calibrate(int16_t val);

/*********************************************************************
 * 旧接口兼容
 */
#define adc1_single_convert_init()                          adc_init()
#define adc1_single_convert_value_get(ch, st)               adc_read(ch)
#define adc1_single_convert_average_value_get(ch, t, st)    adc_read_avg((ch), (t))
#define adc1_voltage_get(val)                               adc_to_mv(val)
#define adc1_value_calibration(val)                         adc_value_calibrate(val)

/**
 * @brief  直接把采样值换算成百分比（最省事）
 * @param  ch  ADC 通道号
 * @return 0~100
 * @note   例：uint8_t p = adc_percent(ADC_Channel_8);
 */
uint8_t adc_percent(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif
