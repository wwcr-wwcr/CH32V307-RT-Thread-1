/******************************************************************************
 * 文件名   : mic.h
 * 芯片     : CH32V307VCT6
 * 说明     : 模拟麦克风（MIC）采集封装
 *
 * 硬件接法：MIC 模块的模拟输出 -> ADC 某个通道（默认 CH1，按实际改）
 *           模块一般自带偏置（约 VCC/2），所以静止时读数在 2048 左右，
 *           有声音时上下摆动。
 *
 * 用法（最省事）:
 *     mic_init(MIC_CH_1);
 *     uint8_t lv = mic_level(0, 64);      // 采 64 点算 0~100 的相对电平
 *     uint16_t rms = mic_rms(64);         // 有效值（原始 0~4095）
 *     uint16_t pk  = mic_peak(32);        // 峰值（拍手/敲击检测用）
 *
 * 注意：
 *   1. 采 64 点大约几百微秒（软件触发逐点转换），不要放在 1ms 级的循环里；
 *   2. 要真实声压级(dB)必须标定，这里给的是"相对电平"，够课程演示；
 *   3. 和 fft 模块不要同时初始化同一个 ADC 通道。
 *
 * 返回值约定: 0 = 成功，负数 = 失败
 ******************************************************************************/

#ifndef __MIC_H
#define __MIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 常用通道（按实际接线改，这里只是给个可读的名字）
 */
#define MIC_CH_0            0
#define MIC_CH_1            1
#define MIC_CH_2            2
#define MIC_CH_3            3
#define MIC_DEFAULT_CH      MIC_CH_1

/* 交流有效值到这个码值算 100%（电平曲线是开方压缩的，不是线性）。
 *
 * 实际手感（MIC_AC_FULL_SCALE = 1000）：
 *     安静 rms≈4   -> 6      轻声 rms≈76  -> 27
 *     说话 rms≈256 -> 50     大喊 rms≈894 -> 94
 *
 * 嫌不够灵敏就改小：500 = 双倍灵敏度（同样的声音显示翻倍），
 * 太容易顶满就改大（1500~2000）。 */
#define MIC_AC_FULL_SCALE   1000u

/*********************************************************************
 * 初始化
 */

/**
 * @brief  初始化 MIC 所接的 ADC 通道
 * @param  adc_ch  ADC 通道号
 * @return 0 成功；-1 ADC 外设初始化失败；-2 通道初始化失败
 */
int mic_init(uint8_t adc_ch);

/**
 * @brief  换一个通道（不用重新 init）
 */
void mic_channel_set(uint8_t adc_ch);

/**
 * @brief  当前用的是哪个通道
 */
uint8_t mic_channel_get(void);

/*********************************************************************
 * 采集
 */

/**
 * @brief  单次采样
 * @return 原始值 0~4095（12 位）
 */
uint16_t mic_sample(void);

/**
 * @brief  采一批到自己的数组里
 * @param  buf  目标数组
 * @param  n    采几个点
 * @return 这批的峰值
 * @note   采完之后可以用这批数据算 RMS / 找过零点 / 送 FFT
 */
uint16_t mic_capture(uint16_t *buf, uint16_t n);

/**
 * @brief  采 n 点，只返回峰值
 * @note   拍手、敲击这类"冲击检测"用它最省事
 */
uint16_t mic_peak(uint16_t n);

/**
 * @brief  采 n 点，返回峰峰值（max-min，已消掉直流偏置）
 * @note   判断"有没有声音/有没有拍手"用这个，比 mic_peak 靠谱得多：
 *         安静底噪 20~60，说话 200~600，拍手 800~2000
 */
uint16_t mic_amp(uint16_t n);

/**
 * @brief  采 n 点，返回【交流有效值】（已扣掉直流偏置）
 * @note   内部用 E[v^2]-E[v]^2 算方差再开方，二分法开方不依赖 math.h。
 *         返回值范围 0 ~ 2000 左右，静音时接近 0，不是 2048。
 */
uint16_t mic_rms(uint16_t n);

uint16_t mic_rms_buf(const uint16_t *buf, uint16_t n);
/**
 * @brief  采 n 点，返回直流分量（那个 1.65V 偏置对应的码值）
 * @note   接线自检用：正常在 1500~2600 之间
 */
uint16_t mic_mean(uint16_t n);

/**
 * @brief  采 n 点，同时给出均值/最小/最大（最短的接线自检）
 * @param  vmin  返回最小值，可传 NULL
 * @param  vmax  返回最大值，可传 NULL
 * @return 均值
 * @note   max-min > 100 且围绕 2048 摆动 -> 正常；
 *         max-min < 20 -> 没信号，查通道；
 *         贴 0 或 4095 -> 引脚被别的外设占了
 */
uint16_t mic_probe(uint16_t n, uint16_t *vmin, uint16_t *vmax);

/**
 * @brief  采 n 点，返回 0~100 的相对电平
 * @param  base  底噪基准（先调 mic_level(0, n) 读一次当前环境值传进来）
 */
uint8_t mic_level(uint8_t base, uint16_t n);

/**
 * @brief  用已有的 RMS 值算 0~100 电平（不再采样）
 * @param  rms   之前 mic_rms() 的返回值
 * @param  base  底噪基准
 * @note   想同时显示 RMS 和电平的时候用它，避免采两次
 */
uint8_t mic_level_calc(uint16_t rms, uint8_t base);

/**
 * @brief  采 n 点，判断有没有超过阈值的冲击
 * @param  thr  峰峰值阈值（安静 20~60，说话 200~600，拍手 800~2000）
 * @return 1 有冲击；0 没有
 */
uint8_t mic_detect(uint16_t thr, uint16_t n);


#ifdef __cplusplus
}
#endif

#endif
