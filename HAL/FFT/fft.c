/******************************************************************************
 * 文件名   : fft.c
 * 芯片     : CH32V307VCT6
 * 说明     : ADC 波形采集 + kissfft 频谱分析（1024 点）
 *
 * 内存说明（重要）：
 *     FFT 的输入、输出、旋转因子全部用静态数组，编译期就定死。
 *     不用 rt_malloc，原因：大小固定、不能容忍运行时分不到，
 *     而且省下来的 heap 留给任务线程和消息队列。
 ******************************************************************************/

#include "fft.h"
#include "adc_dma.h"
#include "adc.h"
#include "kiss_fftr.h"
#include "rtthread.h"
#include "math.h"

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
static kiss_fftr_cfg s_cfg = RT_NULL;          /* FFT 配置，内含旋转因子表 */

/* FFT 输入：1024 个实数 */
static float s_in[FFT_POINTS];                 /* 4.0 KB */
static uint16_t s_wave[FFT_POINTS];
/* FFT 输出：实数 FFT 只输出前 513 个频点 */
static kiss_fft_cpx s_out[FFT_BINS];           /* 4.1 KB */

/* kiss_fftr_alloc 需要的内存（twiddle 表 + 状态）：
 *   kiss_fft_state(512 点) ≈ 4.4KB + fftr 状态 ≈ 16B + 768 个复数 ≈ 6.0KB
 *   合计 ≈ 10.5KB，给到 12KB 留一点余量。 */
static uint8_t s_mem[12288];                   /* 12 KB */
/* 波形特征，供上层直接读 */
static uint16_t s_peak_hz   = 0;
static uint16_t s_peak_amp  = 0;
static uint16_t s_wave_peak = 0;
static uint16_t s_wave_rms  = 0;
static uint32_t s_fs        = 4000;

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化
 */
int fft_init(uint32_t sample_rate_hz)
{
    size_t mem_len = sizeof(s_mem);
    int    ret;

    if(sample_rate_hz == 0)
    {
        return FFT_ERR_PARAM;
    }

    ret = adc_dma_init(sample_rate_hz);
    if(ret != ADC_DMA_OK)
    {
        return FFT_ERR_PARAM;
    }

    s_fs = sample_rate_hz;

    /* 分配 FFT 配置，失败说明 s_mem 给小了 */
    s_cfg = kiss_fftr_alloc(FFT_POINTS, 0, s_mem, &mem_len);
    if(s_cfg == RT_NULL)
    {
        rt_kprintf("fft: alloc need %u bytes, but s_mem is %u\r\n",
                   (unsigned)mem_len, (unsigned)sizeof(s_mem));
        return FFT_ERR_NOALLOC;
    }

    return FFT_OK;
}

/**
 * @brief  采一次并做 FFT
 */
int fft_process(const uint16_t *buf, uint16_t n)
{
    float    mean     = 0.0f;
    float    max_v    = -1e9f;
    float    min_v    =  1e9f;
    float    sum_sq   = 0.0f;
    uint16_t best_bin = 0;
    float    best_mag = 0.0f;
    uint16_t i;

    if(s_cfg == RT_NULL)
    {
        return FFT_ERR_NOALLOC;
    }
    if((buf == (const uint16_t *)0) || (n == 0u))
    {
        return FFT_ERR_PARAM;
    }
    if(n > FFT_POINTS)
    {
        n = FFT_POINTS;                 /* 超长截断，绝不越界 */
    }

    /* 1. 去直流：均值用实际点数 n，不能用 FFT_POINTS，
     *    否则 n < FFT_POINTS 时补零会把均值拉低，波形整体上移、低频虚高 */
    for(i = 0; i < n; i++)
    {
        mean += (float)buf[i];
    }
    mean /= (float)n;

    /* 2. 转 float，顺便统计时序特征（在去直流之后做，峰值/RMS 才有意义） */
    for(i = 0; i < n; i++)
    {
        float v = (float)buf[i] - mean;

        s_in[i] = v;

        if(v > max_v) { max_v = v; }
        if(v < min_v) { min_v = v; }
        sum_sq += v * v;
    }

    /* 3. 补零到 FFT_POINTS，保证 kiss_fftr 拿到固定长度 */
    for(; i < FFT_POINTS; i++)
    {
        s_in[i] = 0.0f;
    }

    s_wave_peak = (uint16_t)(max_v - min_v + 0.5f);
    s_wave_rms  = (uint16_t)(sqrtf(sum_sq / (float)n) + 0.5f);

    /* 4. 存一份原始波形（n 之后清零，避免 UI 画到上一批的残留） */
    for(i = 0; i < FFT_POINTS; i++)
    {
        s_wave[i] = (i < n) ? buf[i] : 0u;
    }

    /* 5. FFT */
    kiss_fftr(s_cfg, s_in, s_out);

    /* 6. 找主频（跳过前 2 个 bin，避开直流和极低频） */
    for(uint16_t k = 2; k < FFT_BINS; k++)
    {
        float re  = s_out[k].r;
        float im  = s_out[k].i;
        float mag = sqrtf(re * re + im * im);

        if(mag > best_mag)
        {
            best_mag = mag;
            best_bin = k;
        }
    }

    /* 7. 换算成 Hz 和幅度
     *    幅度除以 n：补零不改变谱峰高度，只有 n 个点贡献能量。
     *    n == FFT_POINTS 时这就是原来的 mag * 2 / N，完全等价。
     *    频率仍用 FFT_POINTS：FFT 长度是补零后的 512。 */
    if(best_bin == 0)
    {
        s_peak_hz  = 0;
        s_peak_amp = 0;
    }
    else
    {
        s_peak_hz  = (uint16_t)(best_bin * (float)s_fs / (float)FFT_POINTS + 0.5f);
        s_peak_amp = (uint16_t)(best_mag * 2.0f / (float)n + 0.5f);

        if(s_peak_amp > ADC_MAX_VALUE)
        {
            s_peak_amp = ADC_MAX_VALUE;
        }
    }

    return FFT_OK;
}

/**
 * @brief  采一批 ADC 数据并做 FFT（老接口，行为完全不变）
 */
int fft_capture(uint32_t timeout_ms)
{
    if(s_cfg == RT_NULL)
    {
        return FFT_ERR_NOALLOC;
    }

    /* 1. 启动 DMA 采集，等 1024 点（现在是 512 点）采满 */
    if(adc_dma_start() != ADC_DMA_OK)
    {
        return FFT_ERR_TIMEOUT;
    }
    if(adc_dma_wait(timeout_ms) != ADC_DMA_OK)
    {
        return FFT_ERR_TIMEOUT;
    }

    /* 2. 算（和 fft_process 走同一条路） */
    return fft_process(adc_dma_buffer(), FFT_POINTS);
}

void fft_deinit(void)
{
    adc_dma_stop();
    s_cfg = RT_NULL;        /* 下次 fft_init 重新 alloc */
}
/**
 * @brief  把频谱归并成 bars 段，每段取最大值并归一化到 0~100
 */
uint8_t fft_band_get(uint8_t *dst, uint8_t bands)
{
    uint16_t k = 1;                 /* 跳过 bin0（直流）*/
    uint8_t  i;

    if((dst == 0) || (bands == 0u))
    {
        return 0;
    }
    if(bands > FFT_BAND_MAX)
    {
        bands = FFT_BAND_MAX;
    }
    if(s_cfg == RT_NULL)
    {
        return 0;
    }

    for(i = 0; i < bands; i++)
    {
        /* 段宽 1,4,7,10... 递增：低频细、高频粗，跟人耳的感觉接近 */
        uint16_t width = (uint16_t)(1u + (uint16_t)i * 3u);
        uint16_t hi    = (uint16_t)(k + width);
        uint16_t j;
        float    best = 0.0f;
        uint32_t v;

        if(hi > FFT_BINS)
        {
            hi = FFT_BINS;
        }

        for(j = k; j < hi; j++)
        {
            float re  = s_out[j].r;
            float im  = s_out[j].i;
            float mag = sqrtf(re * re + im * im);

            if(mag > best)
            {
                best = mag;
            }
        }

        k = hi;

        /* 和 fft_peak_amp() 用同一个换算：幅度 = mag * 2 / N（ADC 码值）*/
        {
            float amp = best * 2.0f / (float)FFT_POINTS;

            v = (uint32_t)(amp * 100.0f / (float)FFT_BAND_FULL + 0.5f);
        }

        dst[i] = (v > 100u) ? 100u : (uint8_t)v;
    }

    return bands;
}

/**
 * @brief  主频
 */
uint16_t fft_peak_hz(void)
{
    return s_peak_hz;
}

/**
 * @brief  主频幅度
 */
uint16_t fft_peak_amp(void)
{
    return s_peak_amp;
}

/**
 * @brief  峰峰值
 */
uint16_t fft_wave_peak(void)
{
    return s_wave_peak;
}

/**
 * @brief  有效值
 */
uint16_t fft_wave_rms(void)
{
    return s_wave_rms;
}

/**
 * @brief  原始波形
 */
const uint16_t *fft_wave(void)
{
    return s_wave;              /* 不再返回 adc_dma_buffer() */
}
/**
 * @brief  采样率
 */
uint32_t fft_rate_hz(void)
{
    return s_fs;
}
