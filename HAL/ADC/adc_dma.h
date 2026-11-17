/******************************************************************************
 * 文件名   : adc_dma.h
 * 芯片     : CH32V307VCT6
 * 说明     : ADC1 + DMA + TIM3 定时采样驱动（1024 点，CPU 全程不参与）
 *
 * 数据流：
 *     TIM3 更新事件 → ADC1 转换 → DMA 自动搬运到 s_buf
 *                                    ↓
 *                       采满 1024 点 → DMA 完成中断（只置一个标志）
 *
 * CPU 只在"采满 1024 点"时进一次中断，中间不采样中断、不搬数据。
 *
 * 引脚：ADC1 通道 9 = PB1
 *
 * 使用示例:
 *     adc_dma_init(4000);            // 4kHz 采样率
 *     adc_dma_start();               // 开始采一次
 *     if(adc_dma_wait(1000) == 0)    // 等采满（1024 点 @4kHz = 256ms）
 *     {
 *         const uint16_t *wave = adc_dma_buffer();   // 拿波形，只读
 *     }
 *
 * 返回值约定: 0 = 成功，负数 = 失败
 ******************************************************************************/

#ifndef __ADC_DMA_H
#define __ADC_DMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 参数
 *********************************************************************/
#define ADC_DMA_POINTS          512           /* 每次采集的点数 */
#define ADC_DMA_CHANNEL         ADC_Channel_9   /* PB1 */
#define ADC_DMA_GPIO            GPIOB
#define ADC_DMA_PIN             GPIO_Pin_1

/* ADC 采样时间：12MHz ADC 时钟下 28.5 周期 ≈ 2.4us，支持采样率到 200kHz 左右。
 * 信号很慢（<10kHz）又嫌噪声大，可以改成 ADC_SampleTime_239Cycles5 */
#define ADC_DMA_SAMPLE_TIME     ADC_SampleTime_28Cycles5

#define ADC_DMA_MAX_RATE_HZ     200000UL        /* 采样率上限 */

/* 返回值 */
#define ADC_DMA_OK              0
#define ADC_DMA_ERR_PARAM       (-1)
#define ADC_DMA_ERR_BUSY        (-2)
#define ADC_DMA_ERR_TIMEOUT     (-3)

/*********************************************************************
 * 类型定义
 *********************************************************************/
typedef void (*adc_dma_cb_t)(const uint16_t *buf, uint16_t len);

/*********************************************************************
 * 函数
 *********************************************************************/

/**
 * @brief  切换采集通道（示波器默认 PB1，麦克风在 PA1）
 * @param  adc_ch  ADC 通道号：0~7 = PA0~PA7，8 = PB0，9 = PB1
 * @return 0 成功，-1 通道不支持
 * @note   默认用宏 ADC_DMA_CHANNEL（PB1）。task_fft 想让频谱分析采麦克风，
 *         就在 fft_init() 之前调一次 adc_dma_channel_set(MIC_CH_1)。
 *         已经初始化过也没关系，会立刻改 GPIO 和规则组通道，下一次采集就生效。
 */
int adc_dma_channel_set(uint8_t adc_ch);

/**
 * @brief  当前采集通道
 */
uint8_t adc_dma_channel_get(void);

/**
 * @brief  初始化 ADC1 + DMA + TIM3
 * @param  sample_rate_hz  采样率（Hz），1 ~ 200000
 * @return 0 成功，-1 参数错误
 * @note   调用一次即可，之后每次 adc_dma_start() 采一次
 */
int adc_dma_init(uint32_t sample_rate_hz);

/**
 * @brief  开始一次采集（1024 点）
 * @return 0 成功，-2 上一次还没采完
 */
int adc_dma_start(void);

/**
 * @brief  只改采样率（重配 TIM3），不碰 ADC / DMA / 信号量
 * @note   运行中切换时基用它；不要重复调 adc_dma_init()，
 *         那会重复 rt_sem_init，破坏内核对象链表。
 */
int adc_dma_set_rate(uint32_t sample_rate_hz);

/**
 * @brief  停止采集
 */
void adc_dma_stop(void);

/**
 * @brief  是否已采满 1024 点（不阻塞，读标志）
 * @return 1 完成，0 未完成
 */
int adc_dma_is_done(void);

/**
 * @brief  阻塞等待采满
 * @param  timeout_ms  超时时间（毫秒）
 * @return 0 成功，-3 超时
 */
int adc_dma_wait(uint32_t timeout_ms);

/**
 * @brief  取波形数据（只读，1024 个 uint16）
 * @note   在 adc_dma_is_done() 为真之后调用；再次 adc_dma_start() 后内容会被覆盖
 */
const uint16_t *adc_dma_buffer(void);

/**
 * @brief  当前采样率
 */
uint32_t adc_dma_rate_hz(void);

/**
 * @brief  注册采集完成回调（在 DMA 中断里被调用，不要做耗时操作）
 * @param  cb  回调函数，NULL 表示不回调
 */
void adc_dma_callback_set(adc_dma_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif
