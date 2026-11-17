/******************************************************************************
 * 文件名   : audio.h
 * 芯片     : CH32V307VCT6
 * 说明     : 扬声器/音频输出（比蜂鸣器音质好，可放旋律）
 *
 * 板子上同时有"蜂鸣器"和"扬声器"，两者驱动方式不同：
 *   蜂鸣器：PWM 方波直接推，就是 beep.c 那套，声音尖、音量小
 *   扬声器：需要经功放，信号来源有两种常见接法，本驱动都支持：
 *
 *     AUDIO_BACKEND_DAC = 1   DAC 输出波形 -> 功放 -> 喇叭
 *                             （音质最好，能放不同波形）
 *     AUDIO_BACKEND_DAC = 0   用 BEEP 那个 PWM 引脚推无源喇叭
 *                             （本质上和 beep 一样，只是封装统一了）
 *
 * 改后端只改 audio.c 顶部的宏，上层代码不用动。
 *
 * 用法：
 *     audio_init();
 *     audio_volume(60);          // 音量 60%
 *     audio_tone(1000);          // 发 1kHz
 *     ...
 *     audio_off();               // 停
 *
 * 注意：
 *   1. 和 dac 模块的波形发生器**互斥**（都占用 DAC），不能同时用；
 *   2. 频率范围 100~20000Hz，超出会被夹住。
 ******************************************************************************/

#ifndef __AUDIO_H
#define __AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

#define AUDIO_FREQ_MIN      100U
#define AUDIO_FREQ_MAX      20000U

/**
 * @brief  初始化音频输出
 * @return 0 成功；负数失败
 */
int audio_init(void);

/**
 * @brief  发一个频率（持续响，直到 audio_off 或下一个 audio_tone）
 * @param  freq_hz  频率，100~20000
 * @return 0 成功
 */
int audio_tone(uint32_t freq_hz);

/**
 * @brief  停止发声
 */
void audio_off(void);

/**
 * @brief  设置音量
 * @param  percent  0~100（0 = 静音，改的是输出幅度不是关断）
 */
void audio_volume(uint8_t percent);

/**
 * @brief  当前音量
 */
uint8_t audio_volume_get(void);

/**
 * @brief  是否正在发声
 */
uint8_t audio_is_on(void);

/*==================== 音乐播放（DAC + DMA 实时合成） ====================*/

/**
 * 一个音符：
 *     hz = 0   表示休止符
 *     ms       这个音持续多少毫秒（含起音/收尾）
 */
typedef struct
{
    uint16_t hz;
    uint16_t ms;
} audio_note_t;

/**
 * @brief  开始播放一首曲子
 * @param  song  音符表，必须是常驻内存（一般用 const 数组放 Flash）
 * @param  num   音符个数
 * @return 0 成功，-1 参数错 / DAC 初始化失败
 * @note   和 audio_tone() 互斥；开始后要每 10ms 调一次 audio_music_pump()
 */
int audio_music_start(const audio_note_t *song, uint16_t num);

/**
 * @brief  续命：往 DAC 缓冲里补样本，每 10ms 调一次
 * @note   曲子放完会自动喂静音（不会循环播放最后一段），不占 CPU
 */
void audio_music_pump(void);

/**
 * @brief  停止音乐，输出回到中点
 */
void audio_music_stop(void);

/**
 * @brief  还在放吗（1 = 音符没走完）
 */
uint8_t audio_music_playing(void);

/**
 * @brief  当前放到第几个音符（给 UI 显示进度）
 */
uint16_t audio_music_index(void);

#ifdef __cplusplus
}
#endif

#endif
