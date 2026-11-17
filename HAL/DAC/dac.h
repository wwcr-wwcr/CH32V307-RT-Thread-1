/******************************************************************************
 * 文件名   : dac.h
 * 芯片     : CH32V307VCT6
 * 说明     : DAC（CH1/CH2 运行时可切）+ DMA2 + TIM4 波形发生器（正弦 / 方波 / 三角波）
 *
 * 数据流（启动后 CPU 完全不参与）：
 *
 *     TIM4 更新事件(TRGO) ──> DAC1/DAC2 触发一次转换 ──> DMA2_Channel3/4
 *                                                    │
 *                              从 s_buf 取下一个码 ─┘
 *                              （循环模式，一个周期 8~512 点）
 *
 * 波形来源：
 *     正弦：Flash 里一张 256 点 const 码表（512 字节），不用 sinf、不引 libm；
 *     方波 / 三角：纯整数阶梯。
 *     启动时用整型运算把归一化码映射到 [low_mv, high_mv] 电压区间，
 *     之后 DMA 自己循环搬，CPU 一次都不进。
 *
 * 频率关系：  f_out = f_sample / points
 *             f_sample 由 TIM4 产生，最高 1MHz；f_out 范围 1Hz ~ 10kHz
 *
 * 引脚：PA4 = DAC1 输出（扬声器），PA5 = DAC2 输出（DAC 测量）
 *       默认 CH1/PA4；要走 PA5 调 dac_channel_set(2)，两路不能同时出波形
 *
 * 使用示例:
 *     dac_init();                                        // 一次即可（start 里也会自动调）
 *     dac_wave_start(DAC_WAVE_SINE, 10000, 3000, 300);   // 10kHz，0.3V~3.0V 正弦
 *     dac_set_freq(1000);                                // 运行中改频率（沿用上次电压）
 *     dac_set_wave(DAC_WAVE_TRIANGLE);                   // 运行中改波形
 *     dac_stop();                                        // 停止，输出回到电压中点
 *
 * 资源占用：
 *     TIM4 + DMA2_Channel3/4 + PA4&PA5 + 1KB 搬运表（s_buf）+ 512B 正弦码表（Flash）
 *     ADC 用的是 TIM3 + DMA1_Channel1 + PB1，两边完全不冲突。
 *     CH32V307 的两路 DAC 都挂在 DMA2 上（DAC1 -> DMA2_Channel3，DAC2 -> DMA2_Channel4），
 *     配成 DMA1_Channel4 不会报错，但 DMA 永远不应答，现象就是输出停在直流。
 *
 * 注意：
 *     1. PA5 必须配成模拟输入（GPIO_Mode_AIN），不要在外面再把它配成别的功能；
 *     2. PA5 同时也是 ADC 通道 5，如果哪天要用 adc_read(ADC_Channel_5) 读 PA5，
 *        会和 DAC 输出打架，两者只能选一个；
 *     3. DAC 开输出缓冲后输出范围约 0.2V ~ (VDDA-0.2V)，传 3300/0 也不会真到轨；
 *        要满摆幅请把 DAC_OUTPUT_BUFFER 改成 0（这时输出阻抗变高，后级要加跟随器）。
 ******************************************************************************/

#ifndef __DAC_H
#define __DAC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 参数
 *********************************************************************/
#define DAC_WAVE_MAX_POINTS     512         /* 一个周期的最大点数（1KB 搬运表） */
#define DAC_MAX_SAMPLE_HZ       1000000UL   /* TIM4 能给出的最高采样率（1MHz） */
#define DAC_MIN_FREQ_HZ         1UL         /* 最低输出频率 */
#define DAC_MAX_FREQ_HZ         10000UL     /* 最高输出频率 */

#define DAC_VREF_MV             3300U       /* 参考电压 3.3V */
#define DAC_DEFAULT_HIGH_MV     3100U       /* 默认高电平（开输出缓冲时的可用上限） */
#define DAC_DEFAULT_LOW_MV      200U        /* 默认低电平（开输出缓冲时的可用下限） */

/* 1 = 打开 DAC 内部输出缓冲（推荐，带负载能力强）；0 = 满摆幅输出 */
#define DAC_OUTPUT_BUFFER       1

/* 返回值约定：0 = 成功，负数 = 失败 */
#define DAC_OK                  0
#define DAC_ERR_PARAM           (-1)
#define DAC_ERR_NOT_INIT        (-2)

/*********************************************************************
 * 类型
 *********************************************************************/
typedef enum
{
    DAC_WAVE_SINE = 0,          /* 正弦波 */
    DAC_WAVE_SQUARE,            /* 方波，占空比 50% */
    DAC_WAVE_TRIANGLE,          /* 三角波，low -> high -> low */
} dac_wave_t;

/*********************************************************************
 * 函数
 *********************************************************************/

/**
 * @brief  初始化：PA5 模拟、TIM4（只出 TRGO）、DMA2_Channel4、DAC2
 * @return 0 成功
 * @note   可以重复调用；输出默认停在电压中点
 */
int dac_init(void);

/**
 * @brief  生成波形并开始输出
 * @param  type     波形类型
 * @param  freq_hz  目标频率（1 ~ 10000 Hz）
 * @param  high_mv  波形最高电平（mV，0 ~ 3300）
 * @param  low_mv   波形最低电平（mV，0 ~ 3300，必须小于 high_mv）
 * @return 0 成功，-1 参数错误
 * @note   分辨率 = 3300mV/4096 ≈ 0.8mV/LSB，高低电平差越小台阶越明显；
 *         波形/电压/点数都没变时不会重填码表，只改 TIM4。
 */
int dac_wave_start(dac_wave_t type, uint32_t freq_hz,
                   uint16_t high_mv, uint16_t low_mv);

/**
 * @brief  停止输出（DMA 和定时器都关掉，输出停在电压中点）
 */
void dac_stop(void);

/**
 * @brief  运行中改频率，沿用上次设定的电压范围
 * @param  freq_hz  1 ~ 10000 Hz
 * @return 0 成功，-1 参数错误
 */
int dac_set_freq(uint32_t freq_hz);

/**
 * @brief  运行中改波形，沿用上次设定的频率和电压范围
 * @param  type  波形类型
 * @return 0 成功，-1 参数错误
 */
int dac_set_wave(dac_wave_t type);

/**
 * @brief  实际输出频率（Hz）= 实际采样率 / 每周期点数
 */
uint32_t dac_freq_get(void);

/**
 * @brief  当前采样率（Hz），也就是 TIM4 的更新频率
 */
uint32_t dac_rate_get(void);

/**
 * @brief  当前一个周期的点数
 */
uint16_t dac_points_get(void);

/**
 * @brief  取当前设定的电压范围（mV），任一指针可为 NULL
 */
void dac_level_get(uint16_t *high_mv, uint16_t *low_mv);

/**
 * @brief  是否正在输出
 */
int dac_is_running(void);

/**
 * @brief  切换输出通道（运行时）
 * @param  ch  1 = CH1/PA4（扬声器），2 = CH2/PA5（DAC 测量）
 * @return 0 成功，-1 参数错误
 * @note   1. 切换会先 dac_stop() 停止当前输出，再把新通道拉到中点电压
 *         2. 两路引脚（PA4/PA5）在 dac_init() 里已经同时配好，这里只换 DMA/寄存器目标
 *         3. CH1 -> DMA2_Channel3，CH2 -> DMA2_Channel4（CH32V307 两路 DAC 都挂 DMA2）
 */
int dac_channel_set(uint8_t ch);

/**
 * @brief  取当前输出通道（1 或 2）
 */
uint8_t dac_channel_get(void);

/**
 * @brief  取流式播放用的内部缓冲（可写，512 个 12 位码值）
 * @param  len  返回样本数，可为 NULL
 * @return 指向内部 s_buf 的指针
 * @note   给 audio.c 的"实时合成"用：先把 dac_stream_start() 指到这块缓冲，
 *         之后每 10ms 往播放头前面填新样本即可。和 dac_wave_start() 互斥。
 */
uint16_t *dac_stream_buf(uint16_t *len);

/**
 * @brief  流式播放（音频）：把 buf 循环播出去
 * @param  sample_hz  采样率，典型 8000
 * @param  buf        样本缓存，必须是常驻内存
 * @param  len        缓存有多少个码值（0~4095）
 * @return DAC_OK / DAC_ERR_PARAM / DAC_ERR_NOT_INIT
 * @note   和 dac_wave_start() 互斥（都占 TIM4 + DMA）；
 *         函数只负责“播”，往里面填数据是调用方的事。
 */
int dac_stream_start(uint32_t sample_hz, const uint16_t *buf, uint16_t len);

/**
 * @brief  停止流式播放
 */
void dac_stream_stop(void);

/**
 * @brief  当前是否在流式播放
 */
uint8_t dac_is_streaming(void);

/**
 * @brief  取出当前正在输出的码表（给 UI 画波形预览用）
 * @param  len  返回表长（点数），可为 NULL
 * @return 指向 s_buf 的只读指针；下次 dac_wave_start() 后内容会被覆盖
 * @note   表里存的是 DAC 原始码（0~4095），已经映射到设定电压；
 *         指针在驱动生命周期内不变，只有内容会被重写。
 */
const uint16_t *dac_wave_table(uint16_t *len);

/**
 * @brief  码表版本号：每次重填表 +1
 * @note   UI 记住上次画的版本号，一样就不用重画，省 LVGL 重绘
 */
uint32_t dac_wave_table_seq(void);

void dac_deinit(void);
#ifdef __cplusplus
}
#endif

#endif /* __DAC_H */