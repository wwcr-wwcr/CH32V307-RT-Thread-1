/******************************************************************************
 * 文件名   : audio.c
 * 说明     : 扬声器/音频输出
 *
 * 双后端：改 AUDIO_BACKEND_DAC 一个宏就能在"DAC 推功放"和"PWM 推无源喇叭"之间切换。
 ******************************************************************************/

#include "audio.h"

/**************************************************************************************************
 *                                       后端选择
 **************************************************************************************************/
/* 1 = 用 DAC 输出波形（接功放推喇叭，音质好）
 * 0 = 用 BEEP 的 PWM 引脚（无源喇叭直接推，和 beep 一样）*/
#define AUDIO_BACKEND_DAC       1

#if AUDIO_BACKEND_DAC
#include "DAC/dac.h"

/* 扬声器接 PA4，只能由 DAC1（通道 1）驱动；不能改成 2，否则 PA4 没波形 */
#define AUDIO_DAC_CH        1
#else
#include "beep.h"
#endif

static uint8_t  s_volume = 70;
static uint8_t  s_on;
static uint32_t s_freq = 1000;

/* 音乐流状态：实现放在文件末尾，但 audio_volume() 也要改幅度，所以先声明 */
static uint8_t  s_mrun;
static uint16_t s_amp;

/* 满幅 3300mV，按音量缩放 */
static uint16_t amp_mv(void)
{
    return (uint16_t)((3300u * s_volume) / 100u);
}

/**************************************************************************************************
 *                                       对外函数
 **************************************************************************************************/
int audio_init(void)
{
#if AUDIO_BACKEND_DAC
    if(dac_init() != 0)
    {
        return -1;
    }

    /* DAC 模块可能被别的任务切到通道 2，这里强制拉回扬声器通道 */
    (void)dac_channel_set(AUDIO_DAC_CH);
#else
    if(beep_init() != 0)
    {
        return -1;
    }
    beep_duty_set(50);
    beep_off();
#endif

    s_on   = 0;
    s_freq = 1000;

    return 0;
}

int audio_tone(uint32_t freq_hz)
{
    if(freq_hz < AUDIO_FREQ_MIN) { freq_hz = AUDIO_FREQ_MIN; }
    if(freq_hz > AUDIO_FREQ_MAX) { freq_hz = AUDIO_FREQ_MAX; }

    s_freq = freq_hz;
    s_on   = 1;

#if AUDIO_BACKEND_DAC
    audio_music_stop();             /* 音乐流和单音互斥，都占 TIM4 + DMA */
    /* 上一个用 DAC 的任务可能把通道改成了 2，发声前先确认一次 */
    if(dac_channel_get() != AUDIO_DAC_CH)
    {
        (void)dac_channel_set(AUDIO_DAC_CH);
    }

    /* 方波：低电平 0，高电平按音量缩放 -> 峰峰值就是音量 */
    return dac_wave_start(DAC_WAVE_SQUARE, freq_hz, amp_mv(), 0);
#else
    if(s_volume == 0u)
    {
        beep_off();
        return 0;
    }

    (void)beep_duty_set((uint8_t)((s_volume * 50u) / 100u + 10u));
    return beep_freq_set((uint16_t)freq_hz);
#endif
}

void audio_off(void)
{
    dac_deinit();
    s_on = 0;

#if AUDIO_BACKEND_DAC
    dac_stop();
#else
    beep_off();
#endif
}

void audio_volume(uint8_t percent)
{
    if(percent > 100u) { percent = 100; }

    s_volume = percent;

    /* 音乐在放：只改合成幅度，绝对不要走 audio_tone()
     * （那会把音乐停掉、换成方波单音） */
    if(s_mrun != 0u)
    {
        s_amp = (uint16_t)((uint32_t)s_volume * 2047u / 100u);
        return;
    }

    /* 单音在响：按新音量重新发声 */
    if(s_on != 0u)
    {
        (void)audio_tone(s_freq);
    }
}

uint8_t audio_volume_get(void)
{
    return s_volume;
}

uint8_t audio_is_on(void)
{
    return s_on;
}


/**************************************************************************************************
 *                            音乐播放：DAC + DMA 实时合成
 *
 * 为什么之前每个音符"哒"一下：
 *     旧版给每个音都做 5ms 起音 + 10ms 收尾，而且收尾的最后一个样本幅度是 1/80
 *     （约 -38dB）而不是 0，紧跟着下个音又从 0 起音。
 *     于是每个音符边界都有一次"幅度塌陷 + 突变"，边界率 = 音符率，
 *     听起来就是一串跟着节拍走的脉冲（2~4 次/秒就是这么来的）。
 *
 * 现在的做法（关键）：**音符之间不做幅度调制，只换频率**。
 *     相位累加器一路连续走，换音只是改 s_step，波形在数学上完全连续，
 *     连台阶都没有，自然不会有任何"哒"声。
 *     只有在两种情况下才碰幅度：
 *        1. 同音重复（连续两个音频率一样）—— 插一个 12ms 的 V 形凹口，
 *           否则两个同音会连成一个长音，听不出节奏；
 *        2. 整首的开头（20ms 淡入）和最后一个音（25ms 淡出），
 *           避免开机/结束时"啪"一声。
 *
 * 数据流（出声完全不占 CPU）：
 *     任务线程 10ms ──合成样本──> s_buf[512] ──DMA2_Ch3 循环──> DAC1(PA4) ──> 功放 ──> 喇叭
 *                        ^                      |
 *                        └──── 播放头 = 512 - DMA_GetCurrDataCounter() ────┘
 **************************************************************************************************/
#define AUD_SRATE           8000u           /* 采样率 */
#define AUD_MARGIN          96u             /* 写指针和播放头之间的安全距离（12ms） */
#define AUD_PHASE_INC       536870uL        /* 2^32 / 8000 ≈ 536870.9，1Hz 的相位步进 */
#define AUD_DIP_SAMP        96u             /* 同音重复时的凹口长度（12ms） */
#define AUD_FADE_IN_SAMP    160u            /* 全曲开头淡入（20ms） */
#define AUD_FADE_OUT_SAMP   200u            /* 最后一个音淡出（25ms） */

/* 256 点 Q15 正弦表（512 字节 Flash） */
static const int16_t s_sine[256] =
{
         0,    804,   1608,   2410,   3212,   4011,   4808,   5602,
      6393,   7179,   7962,   8739,   9512,  10278,  11039,  11793,
     12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
     18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,
     23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
     27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
     30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,
     32137,  32285,  32412,  32521,  32609,  32678,  32728,  32757,
     32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
     32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,
     30273,  29956,  29621,  29268,  28898,  28510,  28105,  27683,
     27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
     23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,
     18204,  17530,  16846,  16151,  15446,  14732,  14010,  13279,
     12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
      6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,
         0,   -804,  -1608,  -2410,  -3212,  -4011,  -4808,  -5602,
     -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
    -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
    -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
    -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
    -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,
     -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,   -804,
};

static const audio_note_t *s_song;
static uint16_t s_num;              /* 一共几个音 */
static uint16_t s_idx;              /* 当前放到第几个（给 UI 显示） */
static uint8_t  s_mplay;            /* 还有音符没走完 */
static uint32_t s_phase;            /* 相位累加器（跨音符连续，绝不重置） */
static uint32_t s_step;             /* 每个样本相位增量，0 = 休止符 */
static uint32_t s_len;              /* 当前音的样本数 */
static uint32_t s_pos;              /* 当前音已输出样本数 */
static uint16_t s_dip;              /* 当前音开头的凹口长度（0 = 不做） */
static uint16_t s_fade_in;          /* 当前音开头的淡入长度 */
static uint8_t  s_need_fade;        /* 下一个音要不要淡入（只在整曲开头/休止符之后） */
static uint16_t s_fade_out;         /* 当前音结尾的淡出长度 */
static uint16_t s_prev_hz;          /* 上一个音的频率，用来判同音重复 */
static uint16_t s_aw;               /* 缓冲写指针 */

/**
 * @brief  查表 + 线性插值取正弦值
 * @param  phase  32 位相位，0xFFFFFFFF 对应 2π
 */
static int16_t audio_sine(uint32_t phase)
{
    uint32_t idx  = phase >> 24;                    /* 高 8 位选表项 */
    uint32_t frac = (phase >> 10) & 0x3FFFu;        /* 中 14 位做插值 */
    int32_t  a    = s_sine[idx & 0xFFu];
    int32_t  b    = s_sine[(idx + 1u) & 0xFFu];

    return (int16_t)(a + (((b - a) * (int32_t)frac) >> 14));
}

/**
 * @brief  切到下一个音符
 */
static void audio_note_next(void)
{
    while(s_idx < s_num)
    {
        const audio_note_t *n = &s_song[s_idx++];
        uint32_t len = ((uint32_t)n->ms * AUD_SRATE) / 1000u;

        if(len == 0u)
        {
            continue;
        }

        s_len  = len;
        s_pos  = 0;
        s_dip  = 0;

        /* 淡入/淡出是"逐个音"的属性，每个音都要先清掉，
         * 否则会像打补丁一样一直挂着，变成每个音都淡入 -> 每个音都塌一次幅度。 */
        s_fade_in  = 0;
        s_fade_out = 0;

        if(n->hz == 0u)
        {
            s_step    = 0;                          /* 休止符 */
            s_prev_hz = 0;
            s_need_fade = 1;                        /* 休止后面的音要淡入 */
            return;
        }

        if(n->hz > 4000u)                           /* 防止 hz*INC 溢出 uint32 */
        {
            s_step = 4000u * AUD_PHASE_INC;
        }
        else
        {
            s_step = (uint32_t)n->hz * AUD_PHASE_INC;
        }

        /* 同音重复：不加凹口的话两个音会连成一个长音，听不出节奏 */
        if((n->hz == s_prev_hz) && (len > (AUD_DIP_SAMP * 2u)))
        {
            s_dip = AUD_DIP_SAMP;
        }
        s_prev_hz = n->hz;

        /* 整曲第一个音、以及休止符之后的第一个音：淡入，避免从静音突然起 */
        if(s_need_fade != 0u)
        {
            s_fade_in   = AUD_FADE_IN_SAMP;
            s_need_fade = 0;
        }

        /* 下一个音是休止或已经没有下一个了 -> 这个音要淡出 */
        {
            uint16_t nxt = (s_idx < s_num) ? s_song[s_idx].hz : 0u;

            if(nxt == 0u)
            {
                s_fade_out = AUD_FADE_OUT_SAMP;
            }
        }

        return;
    }

    s_mplay = 0;                                    /* 整首放完，之后只喂静音 */
}

/**
 * @brief  合成下一个样本
 * @return DAC 12 位码值（静音 = 2048）
 */
static uint16_t audio_sample_next(void)
{
    int32_t  v;
    int32_t  amp;
    uint32_t pos;

    if(s_mplay == 0u)
    {
        return 2048u;
    }

    if(s_pos >= s_len)
    {
        audio_note_next();
        if(s_mplay == 0u)
        {
            return 2048u;
        }
    }

    pos = s_pos++;

    /* 休止符：直接给中点，不查表 */
    if(s_step == 0u)
    {
        return 2048u;
    }

    amp = (int32_t)s_amp;

    /* 1. 全曲开头淡入 */
    if((s_fade_in != 0u) && (pos < s_fade_in))
    {
        amp = amp * (int32_t)pos / (int32_t)s_fade_in;
    }

    /* 2. 同音重复的 V 形凹口：满 -> 0 -> 满，两端都接得上 */
    if((s_dip != 0u) && (pos < s_dip))
    {
        uint32_t h = (uint32_t)s_dip / 2u;

        if(pos < h)
        {
            amp = amp * (int32_t)(h - pos) / (int32_t)h;
        }
        else
        {
            amp = amp * (int32_t)(pos - h) / (int32_t)h;
        }
    }

    /* 3. 最后一个音淡出 */
    if((s_fade_out != 0u) && ((s_len - pos) <= s_fade_out))
    {
        amp = amp * (int32_t)(s_len - pos) / (int32_t)s_fade_out;
    }

    if(amp < 0)
    {
        amp = 0;
    }

    v = audio_sine(s_phase);
    s_phase += s_step;

    return (uint16_t)(2048 + ((v * amp) >> 15));
}

/**************************************************************************************************
 *                                       对外接口
 **************************************************************************************************/
/**
 * @brief  开始播放一首曲子
 * @param  song  音符表（必须是常驻内存，一般用 const 数组）
 * @param  num   音符个数
 * @return 0 成功，负数失败
 */
int audio_music_start(const audio_note_t *song, uint16_t num)
{
    uint16_t *buf;
    uint16_t  len;
    uint16_t  i;

    if((song == 0) || (num == 0u))
    {
        return -1;
    }

    if(dac_init() != 0)
    {
        return -1;
    }
    (void)dac_channel_set(1);                   /* 扬声器固定走 PA4 / DAC1 */

    buf = dac_stream_buf(&len);
    if((buf == 0) || (len < 128u))
    {
        return -1;
    }

    for(i = 0; i < len; i++)
    {
        buf[i] = 2048u;                         /* 先填中点，避免上电"砰"一声 */
    }

    s_song   = song;
    s_num    = num;
    s_idx    = 0;
    s_aw     = 0;
    s_phase  = 0;
    s_amp    = (uint16_t)((uint32_t)s_volume * 2047u / 100u);
    s_mplay  = 1;
    s_mrun   = 1;
    s_prev_hz   = 0;
    s_fade_in   = 0;
    s_fade_out  = 0;
    s_need_fade = 1;                            /* 整曲第一个音淡入 */

    audio_note_next();
    if(s_mplay == 0u)
    {
        s_mrun = 0;
        return -1;
    }

    if(dac_stream_start(AUD_SRATE, buf, len) != DAC_OK)
    {
        s_mplay = 0;
        s_mrun  = 0;
        return -1;
    }

    audio_music_pump();
    s_on = 1;

    return 0;
}

/**
 * @brief  续命：每 10ms 调一次，往播放头前面补样本
 * @note   曲子放完之后仍然喂中点（静音），这样 DMA 不会循环播放最后一段。
 */
void audio_music_pump(void)
{
    uint16_t *buf;
    uint16_t  len;
    uint16_t  remain;
    uint16_t  pos;
    uint16_t  pending;
    uint16_t  space;

    if(s_mrun == 0u)
    {
        return;
    }

    buf = dac_stream_buf(&len);
    if((buf == 0) || (len == 0u) || ((len & (len - 1u)) != 0u))
    {
        return;                                 /* len 必须是 2 的幂 */
    }

    remain  = DMA_GetCurrDataCounter(DMA2_Channel3);
    pos     = (uint16_t)((len - remain) & (len - 1u));
    pending = (uint16_t)((s_aw - pos) & (len - 1u));

    if((uint32_t)pending + AUD_MARGIN >= len)
    {
        return;
    }
    space = (uint16_t)(len - AUD_MARGIN - pending);

    while(space-- > 0u)
    {
        buf[s_aw] = audio_sample_next();
        s_aw = (uint16_t)((s_aw + 1u) & (len - 1u));
    }
}

/**
 * @brief  停止音乐（立刻停 TIM/DMA，输出回到中点）
 */
void audio_music_stop(void)
{
    if(s_mrun == 0u)
    {
        s_mplay = 0;
        return;
    }

    s_mrun  = 0;
    s_mplay = 0;
    dac_stream_stop();
}

uint8_t audio_music_playing(void)
{
    return s_mplay;
}

uint16_t audio_music_index(void)
{
    return (s_idx == 0u) ? 0u : (uint16_t)(s_idx - 1u);
}
