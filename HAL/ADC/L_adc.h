/******************************************************************************
 * 文件名   : L_adc.h
 * 芯片     : CH32V307VCT6
 * 说明     : ADC1 定时器触发采样（TIM3 TRGO 触发，固定采样率）
 *
 * 与 adc.h 的区别:
 *   adc.h      —— 软件触发，适合慢慢测电压/温度
 *   L_adc.h    —— 定时器触发 + 中断取数，适合采集波形做 FFT、做音频分析
 *
 * 使用示例:
 *     static void on_sample(uint16_t v) { ... }      // 在中断里调用，尽量短
 *     adc_timer_callback_set(on_sample);
 *     adc_timer_start(ADC_Channel_9, 4000);          // PB1，4kHz 采样
 *     ...
 *     adc_timer_stop();
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误）
 ******************************************************************************/

#ifndef __L_ADC_H
#define __L_ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "adc.h"

/*********************************************************************
 * 参数
 */
#define ADC_TIMER_CLOCK         1000000UL       /* 定时器计数频率 1MHz */
#define ADC_TIMER_PRESCALER     ((SystemCoreClock / ADC_TIMER_CLOCK) - 1)
#define ADC_TIMER_MAX_RATE_HZ   500000UL

/*********************************************************************
 * 类型定义
 */
typedef void (*adc_timer_cb_t)(uint16_t value);

/*********************************************************************
 * 函数
 */

/**
 * @brief  注册采样回调（每采到一个点调用一次，在中断上下文中执行）
 * @param  cb  回调函数，传 NULL 表示不回调
 */
void adc_timer_callback_set(adc_timer_cb_t cb);

/**
 * @brief  启动定时器触发采样
 * @param  channel          ADC 通道号
 * @param  sample_rate_hz   采样率（Hz）
 * @return 0 成功，-1 参数错误
 */
int adc_timer_start(uint8_t channel, uint32_t sample_rate_hz);

/**
 * @brief  停止采样
 */
void adc_timer_stop(void);

/**
 * @brief  读取最近一次的采样值
 * @return 0~4095
 */
uint16_t adc_timer_value_get(void);

/*********************************************************************
 * 旧接口兼容
 */
#define L_Time_Adc_init(ch, arr, psc)   adc_timer_start((ch), 1000000UL / ((arr) + 1))
#define L_Time_Adc_DeInit()             adc_timer_stop()
#define ADC_getValue(ch, st)            adc_read(ch)
#define ADC_voltage_get(val)            adc_to_mv(val)
#define value_calibration(val)          adc_value_calibrate(val)

#ifdef __cplusplus
}
#endif

#endif
