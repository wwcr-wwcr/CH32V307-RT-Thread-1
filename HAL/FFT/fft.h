/******************************************************************************
 * 文件名   : fft.h
 * 芯片     : CH32V307VCT6
 * 说明     : ADC 波形采集 + FFT 频谱分析（1024 点）
 *
 * 分工：
 *     adc_dma.c  负责采集（硬件完成，CPU 不参与）
 *     本文件     负责 FFT 计算和波形特征提取
 *
 * 使用示例:
 *     fft_init(4000);                 // 4kHz 采样率
 *     if(fft_capture(1000) == 0)      // 采 1024 点并算完 FFT
 *     {
 *         uint16_t hz  = fft_peak_hz();     // 主频
 *         uint16_t amp = fft_peak_amp();    // 主频幅度
 *         uint16_t pp  = fft_wave_peak();   // 峰峰值
 *     }
 *
 * 内存占用（全部静态，不进 heap）：
 *     采样缓冲 2KB（adc_dma.c）+ FFT 工作区约 12KB
 ******************************************************************************/

#ifndef _FFT_H_
#define _FFT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"
#include "adc_dma.h"

/*********************************************************************
 * 参数
 *********************************************************************/
#define FFT_POINTS              ADC_DMA_POINTS       /* 1024 */
#define FFT_BINS                (FFT_POINTS / 2 + 1) /* 输出频谱点数 513 */

/* 返回值 */
#define FFT_OK                  0
#define FFT_ERR_PARAM           (-1)
#define FFT_ERR_TIMEOUT         (-2)
#define FFT_ERR_NOALLOC         (-3)

/* 频谱柱（音乐播放器那种跳动条）用 */
#define FFT_BAND_MAX            16      /* 最多分成几段 */
#define FFT_BAND_FULL           10u    /* 某段幅度到这个码值算满格 */

/*********************************************************************
 * 函数
 *********************************************************************/

/**
 * @brief  初始化（内部调用 adc_dma_init）
 * @param  sample_rate_hz  采样率（Hz）
 * @return 0 成功，-1 参数错误，-3 FFT 内存申请失败
 */
int fft_init(uint32_t sample_rate_hz);

void fft_deinit(void);
/**
 * @brief  采一次 1024 点并做 FFT（阻塞，带超时）
 * @param  timeout_ms  超时时间（毫秒）。1024 点 @4kHz 需要 256ms
 * @return 0 成功，-2 超时
 * @note   本函数会阻塞调用者，请放在任务线程里，不要放 UI 线程
 */
int fft_capture(uint32_t timeout_ms);

/**
 * @brief  对一段外部采样数据做 FFT（不走 ADC DMA）
 * @param  buf  采样数组，0~4095 的 12 位码值
 * @param  n    有效点数，1 ~ FFT_POINTS
 * @return FFT_OK / FFT_ERR_PARAM / FFT_ERR_NOALLOC
 */
int fft_process(const uint16_t *buf, uint16_t n);
/**
 * @brief  主频（Hz），没信号时接近 0
 */
uint16_t fft_peak_hz(void);

/**
 * @brief  主频幅度（0~4095，已按采样点数归一化）
 */
uint16_t fft_peak_amp(void);

/**
 * @brief  时域峰峰值（max - min，0~4095）
 */
uint16_t fft_wave_peak(void);

/**
 * @brief  时域有效值 RMS（0~4095）
 */
uint16_t fft_wave_rms(void);

/**
 * @brief  取原始波形（只读，1024 个 uint16）
 */
const uint16_t *fft_wave(void);

/**
 * @brief  当前采样率
 */
uint32_t fft_rate_hz(void);

/**
 * @brief  把频谱归并成 bands 段，每段取最大值并归一化到 0~100
 * @param  dst    输出数组，长度 >= bands
 * @param  bands  段数（1 ~ FFT_BAND_MAX）
 * @return 实际写入的段数，失败返回 0
 * @note   段宽按 1,4,7,10... 递增（低频窄、高频宽），接近人耳的分辨习惯；
 *         必须在 fft_capture() 之后调用，内部直接用上一次的 s_out。
 *         想让柱子更活跃就把 FFT_BAND_FULL 改小（400 就是双倍灵敏度）。
 */
uint8_t fft_band_get(uint8_t *dst, uint8_t bands);

#ifdef __cplusplus
}
#endif

#endif
