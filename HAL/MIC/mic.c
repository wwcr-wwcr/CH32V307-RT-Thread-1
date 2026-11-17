/******************************************************************************
 * 文件名   : mic.c
 * 说明     : 模拟麦克风采集封装（基于 ADC）
 *
 * 只做"采样 + 算特征"，不做滤波、不做 FFT —— 那些交给调用方。
 * 这样 mic 模块和 fft / noise / clap 三个示例都能复用。
 ******************************************************************************/

#include "mic.h"
#include "adc.h"

static uint8_t s_ch = MIC_DEFAULT_CH;

/**************************************************************************************************
 *                                       初始化
 **************************************************************************************************/
int mic_init(uint8_t adc_ch)
{
    s_ch = adc_ch;

    if(adc_init() != 0)
    {
        return -1;
    }

    if(adc_channel_init(s_ch) != 0)
    {
        return -2;
    }

    return 0;
}

void mic_channel_set(uint8_t adc_ch)
{
    s_ch = adc_ch;
    (void)adc_channel_init(s_ch);
}

uint8_t mic_channel_get(void)
{
    return s_ch;
}

/**************************************************************************************************
 *                                       采集
 **************************************************************************************************/
uint16_t mic_sample(void)
{
    return (uint16_t)(adc_read(s_ch) & 0x0FFFu);
}

uint16_t mic_capture(uint16_t *buf, uint16_t n)
{
    uint16_t i;
    uint16_t pk = 0;
    uint16_t v;

    if(buf == 0)
    {
        return 0;
    }

    for(i = 0; i < n; i++)
    {
        v = mic_sample();
        buf[i] = v;
        if(v > pk)
        {
            pk = v;
        }
    }

    return pk;
}

uint16_t mic_peak(uint16_t n)
{
    uint16_t i;
    uint16_t pk = 0;
    uint16_t v;

    for(i = 0; i < n; i++)
    {
        v = mic_sample();
        if(v > pk)
        {
            pk = v;
        }
    }

    return pk;
}

/* 整数开方（二分法，不引 math.h） */
static uint32_t isqrt_u32(uint32_t x)
{
    uint32_t lo = 0;
    uint32_t hi = 4095u;            /* 12 位 ADC 的方差最大 4095^2 */
    uint32_t mid;

    if(x >= 0xFFFFFFFFUL) { hi = 65535u; }

    while(lo < hi)
    {
        mid = (lo + hi + 1u) / 2u;
        if((mid * mid) <= x)
        {
            lo = mid;
        }
        else
        {
            hi = mid - 1u;
        }
    }

    return lo;
}

/**
 * @brief  从已有的采样缓冲区算交流有效值（不再自己逐个采样）
 * @param  buf  12 位 ADC 值数组，比如 adc_dma_buffer()
 * @param  n    点数
 * @return 交流 RMS，静音时接近 0，说话时几百
 * @note   两遍算法：先求均值，再算方差，避免 E[v^2] 在 512 点时溢出 uint32
 */
uint16_t mic_rms_buf(const uint16_t *buf, uint16_t n)
{
    uint32_t sum = 0;
    uint32_t sq  = 0;
    int32_t  mean, d;
    uint16_t i;

    if((buf == 0) || (n == 0u))
    {
        return 0;
    }

    for(i = 0; i < n; i++)
    {
        sum += buf[i];
    }
    mean = (int32_t)(sum / n);

    for(i = 0; i < n; i++)
    {
        d   = (int32_t)buf[i] - mean;       /* |d| ≤ 2048，d*d 不会溢出 */
        sq += (uint32_t)(d * d);
    }

    return (uint16_t)isqrt_u32(sq / n);     /* AC_RMS = sqrt(E[(v-mean)^2]) */
}


/**
 * @brief 采 n 点，返回【交流有效值】（已经扣掉直流偏置）
 *
 * 为什么要扣偏置：
 *     麦克风模块自带 VCC/2 偏置，静止时读数一直在 2048 左右。
 *     如果直接对原始值求 RMS，结果永远是 2048 上下 —— 说话时也只是
 *     从 2048 涨到 2100，再套 rms*100/4095 就被压成 50 和 51 两档，
 *     根本看不出声音。所以这里用方差算：
 *
 *         AC_RMS = sqrt( E[v^2] - (E[v])^2 )
 *
 *     一次采样就能算完，不用存数组，也不用知道偏置是多少。
 */
/**
 * @brief 采 n 点，返回峰峰值（max - min，单位是 ADC 码值）
 *
 * 为什么用峰峰值而不是最大值：
 *     信号带 2048 的直流偏置，用"最大值"当判据的话，安静的底噪也有
 *     2100，阈值只能往上抬，抬到自己都分不清是在测声音还是在测偏置。
 *     峰峰值天然把偏置消掉了，安静时 20~40，拍一下手 800 以上，好判断。
 */
uint16_t mic_amp(uint16_t n)
{
    uint16_t i;
    uint16_t v;
    uint16_t lo = 4095u;
    uint16_t hi = 0u;

    if(n == 0u)
    {
        return 0;
    }

    for(i = 0; i < n; i++)
    {
        v = mic_sample();
        if(v < lo) { lo = v; }
        if(v > hi) { hi = v; }
    }

    return (uint16_t)(hi - lo);
}

uint16_t mic_rms(uint16_t n)
{
    uint32_t s1 = 0;                /* 和 */
    uint32_t s2 = 0;                /* 平方和 */
    uint32_t mean;
    uint32_t var;
    uint16_t i;
    uint16_t v;

    if(n == 0u)
    {
        return 0;
    }

    for(i = 0; i < n; i++)
    {
        v = mic_sample();
        s1 += v;
        s2 += (uint32_t)v * v;
    }

    mean = s1 / n;
    var  = s2 / n;                  /* E[v^2] */

    if(var <= (mean * mean))        /* 全静音时方差可能是 0，防下溢 */
    {
        return 0;
    }

    return (uint16_t)isqrt_u32(var - mean * mean);
}

/**
 * @brief 采 n 点，返回直流分量（就是那个 1.65V 偏置对应的码值）
 * @note  接线排查用：正常应该在 1500~2600 之间；接近 0 或 4095 说明脚接错了
 */
uint16_t mic_mean(uint16_t n)
{
    uint32_t s = 0;
    uint16_t i;

    if(n == 0u)
    {
        return 0;
    }

    for(i = 0; i < n; i++)
    {
        s += mic_sample();
    }

    return (uint16_t)(s / n);
}

/**
 * @brief 采 n 点，一次性给出均值 + 最小 + 最大（最短的接线自检）
 * @return 均值；*vmin / *vmax 可以传 NULL
 * @note  判断标准：
 *           max - min > 100 且在 2048 附近摆动 -> 接线正常、有声音
 *           max - min < 20                      -> 没有信号进来，检查通道/接线
 *           读数贴 0 或 4095                    -> 引脚被别的外设占了
 */
uint16_t mic_probe(uint16_t n, uint16_t *vmin, uint16_t *vmax)
{
    uint32_t s = 0;
    uint16_t i;
    uint16_t v;
    uint16_t lo = 4095u;
    uint16_t hi = 0u;

    if(n == 0u)
    {
        if(vmin != 0) { *vmin = 0; }
        if(vmax != 0) { *vmax = 0; }
        return 0;
    }

    for(i = 0; i < n; i++)
    {
        v = mic_sample();
        s += v;
        if(v < lo) { lo = v; }
        if(v > hi) { hi = v; }
    }

    if(vmin != 0) { *vmin = lo; }
    if(vmax != 0) { *vmax = hi; }

    return (uint16_t)(s / n);
}

uint8_t mic_level_calc(uint16_t rms, uint8_t base)
{
    uint32_t lv;

    /* 开方式压缩：lv = 100 * sqrt(rms / MIC_AC_FULL_SCALE)
     *
     * 为什么不用线性：
     *     人耳对响度的感觉本来就是接近对数的，麦克风输出的动态范围又大
     *     （安静 5、说话 300、喊 900）。线性映射的结果是"小声全挤在
     *     0~3、大声一动就顶满"，中间那段几乎没有分辨率 —— 也就是
     *     "明明很吵了，电平才显示 30"。
     *     开方是最省事的那种压缩（不用 math.h、不用查表）：
     *
     *         rms=4    -> 6        安静，能看见一点
     *         rms=76   -> 27       轻声说话
     *         rms=256  -> 50       正常说话
     *         rms=894  -> 94       大喊，接近满格
     *
     * 想更灵敏就把 mic.h 里的 MIC_AC_FULL_SCALE 调小（500 就是双倍灵敏度）。
     */
    lv = isqrt_u32(((uint32_t)rms * 10000u) / MIC_AC_FULL_SCALE);

    if(lv > 100u)
    {
        lv = 100u;
    }

    if(lv > base)
    {
        return (uint8_t)(lv - base);
    }

    return 0;
}

uint8_t mic_level(uint8_t base, uint16_t n)
{
    return mic_level_calc(mic_rms(n), base);
}

uint8_t mic_detect(uint16_t thr, uint16_t n)
{
    /* 改用峰峰值判据：thr 是"摆幅"，不是"绝对电平"。
     * 典型值：安静底噪 20~60，说话 200~600，拍手 800~2000。 */
    return (mic_amp(n) >= thr) ? 1u : 0u;
}

