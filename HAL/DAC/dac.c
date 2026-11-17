/******************************************************************************
 * 文件名   : dac.c
 * 芯片     : CH32V307VCT6
 * 说明     : DAC（CH1/CH2 运行时可切，默认 PA4）+ DMA2 + TIM4 波形发生器
 *
 * 为什么用 DMA：
 *     10kHz 输出、每周期 100 点，意味着 DAC 要每 1us 换一个码。
 *     靠中断或软件写，CPU 光喂数据就忙死了；DMA 循环搬运，
 *     CPU 只在启动时填一次表，之后一次都不进。
 *
 * 为什么用码表：
 *     正弦如果每次启动都调 sinf 现算 100~512 个点，既慢，又会把 libm 的
 *     三角函数整套拖进 Flash。这里改成 Flash 里一张 256 点 const 码表，
 *     启动时只做整型映射，全程没有浮点、也不依赖 math.h。
 *
 * 为什么用 TIM4：
 *     TIM2 被蜂鸣器(CH3/PA2)、风扇(CH1/PA0)、加热片(CH4/PA3) 和通用 PWM 占着，
 *     TIM1 给了电机，TIM3 给了 ADC。TIM4 在 encoder.c 改成软件计数后就空出来了。
 *
 * 为什么是 DMA2：
 *     CH32V307 的 DAC 挂在 DMA2 上（DAC1 -> DMA2_Channel3，DAC2 -> DMA2_Channel4）。
 *     写成 DMA1_Channel4 编译链接都正常，DAC 也在正常触发，但没有通道应答 DMA 请求，
 *     现象就是输出停在直流电平、示波器上看不到波形。
 *
 ******************************************************************************/

#include "dac.h"


/**************************************************************************************************
 *                                    输出通道选择
 *
 * CH32V307 有两路 DAC，引脚和资源都不同：
 *
 *   通道 1  ->  PA4 / DAC_Channel_1 / DMA2_Channel3 / 数据寄存器 R12BDHR1  （扬声器）
 *   通道 2  ->  PA5 / DAC_Channel_2 / DMA2_Channel4 / 数据寄存器 R12BDHR2  （DAC 测量）
 *
 * 两路 DAC 都挂在 DMA2 上，这点和 STM32 不一样（DAC1 也是 DMA2_Channel3）。
 * 引脚在 dac_init() 里一次配好两路，运行时用 dac_channel_set(ch)
 * 只换 DMA 目标和数据寄存器，不需要重新初始化。
 *
 *     dac_channel_set(1);   ->  PA4，扬声器响（默认）
 *     dac_channel_set(2);   ->  PA5，DAC 测量输出
 *
 * 板子实测：PA4 是扬声器（只能由 DAC1 驱动），PA5 是 DAC2 输出。
 * 一次只能输出一路，因为只有一组 s_buf 和一个 TIM4。
 **************************************************************************************************/
/* 默认通道：1 = PA4（DAC1，接扬声器），2 = PA5（DAC2）*/
#define DAC_DEFAULT_CH      1

/* 当前使用的通道，可用 dac_channel_set() 在运行时切换 */
static uint8_t s_ch = DAC_DEFAULT_CH;

/* 1 = 正在做"流式播放"（音频），此时 s_buf 不参与，DMA 直接搬外部缓冲 */
static uint8_t s_streaming = 0;

/* 当前通道对应的 DAC 通道号 */
static uint32_t dac_chan(void)
{
    return (s_ch == 1u) ? DAC_Channel_1 : DAC_Channel_2;
}

/* 当前通道的数据寄存器地址（DMA 的搬运目标）*/
static uint32_t dac_dreg_addr(void)
{
    return (s_ch == 1u) ? (uint32_t)(&DAC->R12BDHR1) : (uint32_t)(&DAC->R12BDHR2);
}

/* 往当前通道写一个 12 位值 */
static void dac_set_val(uint16_t v)
{
    v = (uint16_t)(v & 0x0FFFu);

    if(s_ch == 1u)
    {
        DAC_SetChannel1Data(DAC_Align_12b_R, v);
    }
    else
    {
        DAC_SetChannel2Data(DAC_Align_12b_R, v);
    }
}

/* 当前通道对应的 DMA 通道（CH32V307 两路 DAC 都挂在 DMA2 上）*/
static DMA_Channel_TypeDef *dac_dma_ch(void)
{
    return (s_ch == 1u) ? DMA2_Channel3 : DMA2_Channel4;
}

/**************************************************************************************************
 *                                  正弦码表（Flash，512 字节）
 **************************************************************************************************/
#define DAC_SINE_TAB_LEN        256

/* 2048 + 2047 * sin(2*pi*i/256)，四舍五入，范围 0~4095 */
static const uint16_t s_sine_tab[DAC_SINE_TAB_LEN] =
{
    0x800, 0x832, 0x864, 0x897, 0x8C9, 0x8FB, 0x92C, 0x95E,
    0x98F, 0x9C1, 0x9F1, 0xA22, 0xA52, 0xA82, 0xAB2, 0xAE1,
    0xB0F, 0xB3E, 0xB6B, 0xB98, 0xBC5, 0xBF1, 0xC1C, 0xC47,
    0xC71, 0xC9B, 0xCC3, 0xCEB, 0xD13, 0xD39, 0xD5F, 0xD83,
    0xDA7, 0xDCB, 0xDED, 0xE0E, 0xE2E, 0xE4E, 0xE6C, 0xE8A,
    0xEA6, 0xEC1, 0xEDC, 0xEF5, 0xF0D, 0xF24, 0xF3A, 0xF4F,
    0xF63, 0xF76, 0xF87, 0xF98, 0xFA7, 0xFB5, 0xFC2, 0xFCD,
    0xFD8, 0xFE1, 0xFE9, 0xFF0, 0xFF5, 0xFF9, 0xFFD, 0xFFE,
    0xFFF, 0xFFE, 0xFFD, 0xFF9, 0xFF5, 0xFF0, 0xFE9, 0xFE1,
    0xFD8, 0xFCD, 0xFC2, 0xFB5, 0xFA7, 0xF98, 0xF87, 0xF76,
    0xF63, 0xF4F, 0xF3A, 0xF24, 0xF0D, 0xEF5, 0xEDC, 0xEC1,
    0xEA6, 0xE8A, 0xE6C, 0xE4E, 0xE2E, 0xE0E, 0xDED, 0xDCB,
    0xDA7, 0xD83, 0xD5F, 0xD39, 0xD13, 0xCEB, 0xCC3, 0xC9B,
    0xC71, 0xC47, 0xC1C, 0xBF1, 0xBC5, 0xB98, 0xB6B, 0xB3E,
    0xB0F, 0xAE1, 0xAB2, 0xA82, 0xA52, 0xA22, 0x9F1, 0x9C1,
    0x98F, 0x95E, 0x92C, 0x8FB, 0x8C9, 0x897, 0x864, 0x832,
    0x800, 0x7CE, 0x79C, 0x769, 0x737, 0x705, 0x6D4, 0x6A2,
    0x671, 0x63F, 0x60F, 0x5DE, 0x5AE, 0x57E, 0x54E, 0x51F,
    0x4F1, 0x4C2, 0x495, 0x468, 0x43B, 0x40F, 0x3E4, 0x3B9,
    0x38F, 0x365, 0x33D, 0x315, 0x2ED, 0x2C7, 0x2A1, 0x27D,
    0x259, 0x235, 0x213, 0x1F2, 0x1D2, 0x1B2, 0x194, 0x176,
    0x15A, 0x13F, 0x124, 0x10B, 0x0F3, 0x0DC, 0x0C6, 0x0B1,
    0x09D, 0x08A, 0x079, 0x068, 0x059, 0x04B, 0x03E, 0x033,
    0x028, 0x01F, 0x017, 0x010, 0x00B, 0x007, 0x003, 0x002,
    0x001, 0x002, 0x003, 0x007, 0x00B, 0x010, 0x017, 0x01F,
    0x028, 0x033, 0x03E, 0x04B, 0x059, 0x068, 0x079, 0x08A,
    0x09D, 0x0B1, 0x0C6, 0x0DC, 0x0F3, 0x10B, 0x124, 0x13F,
    0x15A, 0x176, 0x194, 0x1B2, 0x1D2, 0x1F2, 0x213, 0x235,
    0x259, 0x27D, 0x2A1, 0x2C7, 0x2ED, 0x315, 0x33D, 0x365,
    0x38F, 0x3B9, 0x3E4, 0x40F, 0x43B, 0x468, 0x495, 0x4C2,
    0x4F1, 0x51F, 0x54E, 0x57E, 0x5AE, 0x5DE, 0x60F, 0x63F,
    0x671, 0x6A2, 0x6D4, 0x705, 0x737, 0x769, 0x79C, 0x7CE,
};

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
/* 真正给 DMA 循环搬运的表：整型映射后的最终码 */
static uint16_t   s_buf[DAC_WAVE_MAX_POINTS];

static dac_wave_t s_type   = DAC_WAVE_SINE;
static uint32_t   s_freq   = 1000;                      /* 设定频率 */
static uint32_t   s_rate   = 1000000;                   /* 实际采样率 */
static uint16_t   s_points = 100;                       /* 每周期点数 */
static uint16_t   s_hi_mv  = DAC_DEFAULT_HIGH_MV;       /* 波形最高电平 */
static uint16_t   s_lo_mv  = DAC_DEFAULT_LOW_MV;        /* 波形最低电平 */

/* 码表缓存标记：波形/电压/点数都没变就不再重填 */
static uint8_t    s_tab_type = 0xFF;
static uint16_t   s_tab_hi   = 0;
static uint16_t   s_tab_lo   = 0;
static uint16_t   s_tab_pts  = 0;

static uint8_t    s_running = 0;
static uint32_t   s_tab_seq = 0;                /* 码表版本号，UI 靠它判断要不要重画预览 */
static uint8_t    s_inited  = 0;

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* TIM4 的计数时钟：APB1 分频不为 1 时等于 PCLK1 的两倍，否则等于 PCLK1。
 * 本工程 SYSCLK=144MHz、APB1=HCLK/2=72MHz，所以 TIM4 时钟 = 144MHz = SystemCoreClock。 */
static uint32_t tim4_clock_hz(void)
{
    uint32_t ppre1 = (RCC->CFGR0 >> 8) & 0x07u;

    if(ppre1 >= 4u)
    {
        return (SystemCoreClock >> (ppre1 - 3u)) << 1;
    }

    return SystemCoreClock;
}

/* 毫伏 -> 12 位码，四舍五入 */
static uint16_t mv_to_code(uint16_t mv)
{
    uint32_t code;

    if(mv >= DAC_VREF_MV)
    {
        return 4095;
    }

    code = ((uint32_t)mv * 4095U + DAC_VREF_MV / 2U) / DAC_VREF_MV;

    return (uint16_t)((code > 4095U) ? 4095U : code);
}

/* 归一化码(0~4095) -> [lo, lo+span]，纯整型，用 >>12 代替除法 */
static uint16_t code_map(uint16_t base, uint16_t lo, uint16_t span)
{
    return (uint16_t)(lo + (((uint32_t)span * base) >> 12));
}

/* 按波形和电压范围填搬运表：只做整数运算，不调任何数学库 */
static void table_fill(dac_wave_t type, uint16_t points, uint16_t lo, uint16_t hi)
{
    uint16_t span = (uint16_t)(hi - lo);
    uint16_t half = (uint16_t)(points / 2U);
    uint16_t i;

    for(i = 0; i < points; i++)
    {
        uint16_t base;

        switch(type)
        {
        case DAC_WAVE_SINE:
        {
            uint16_t idx = (uint16_t)(((uint32_t)i * DAC_SINE_TAB_LEN) / points);

            if(idx >= DAC_SINE_TAB_LEN)
            {
                idx = DAC_SINE_TAB_LEN - 1U;
            }

            base = s_sine_tab[idx];
            break;
        }

        case DAC_WAVE_SQUARE:
            base = (i < half) ? 4095U : 0U;
            break;

        case DAC_WAVE_TRIANGLE:
        default:
            if(i < half)
            {
                base = (uint16_t)(((uint32_t)i * 4095U) / half);            /* 低 -> 高 */
            }
            else
            {
                base = (uint16_t)(((uint32_t)(points - i) * 4095U) / half); /* 高 -> 低 */
            }
            break;
        }

        s_buf[i] = code_map(base, lo, span);
    }

    s_tab_type = (uint8_t)type;
    s_tab_hi   = hi;
    s_tab_lo   = lo;
    s_tab_pts  = points;
    s_tab_seq++;
}

/* DMA：把 s_buf 循环搬进 DAC 的 12 位右对齐数据寄存器 */
static void dma_setup(uint16_t points)
{
    DMA_InitTypeDef d = {0};

    DMA_Channel_TypeDef *ch = dac_dma_ch();

    DMA_Cmd(ch, DISABLE);
    DMA_DeInit(ch);

    d.DMA_PeripheralBaseAddr = dac_dreg_addr();
    d.DMA_MemoryBaseAddr     = (uint32_t)s_buf;
    d.DMA_DIR                = DMA_DIR_PeripheralDST;
    d.DMA_BufferSize         = points;
    d.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    d.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    d.DMA_Mode               = DMA_Mode_Circular;
    d.DMA_Priority           = DMA_Priority_VeryHigh;
    d.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(ch, &d);
}

/* 挑一组 (PSC, ARR) 让 TIM4 的更新频率最接近 sample_hz，返回实际采样率 */
static uint32_t tim4_apply(uint32_t sample_hz)
{
    uint32_t clk       = tim4_clock_hz();
    uint32_t best_err  = 0xFFFFFFFFUL;
    uint32_t best_psc  = 0;
    uint32_t best_arr  = 1;
    uint32_t best_rate = sample_hz;
    uint32_t div_a;

    /* ARR+1 从 2 开始：ARR=0 时每个计数都产生更新事件，会失控 */
    for(div_a = 2; div_a <= 1024u; div_a++)
    {
        uint32_t want = sample_hz * div_a;
        uint32_t div_p;
        uint32_t rate;
        uint32_t err;

        if(want == 0u)
        {
            continue;
        }

        div_p = (clk + want / 2u) / want;           /* 四舍五入到最近的分频比 */

        if(div_p < 1u)      { div_p = 1u; }
        if(div_p > 65536u)  { continue; }

        rate = clk / (div_p * div_a);
        err  = (rate > sample_hz) ? (rate - sample_hz) : (sample_hz - rate);

        if(err < best_err)
        {
            best_err  = err;
            best_psc  = div_p - 1u;
            best_arr  = div_a - 1u;
            best_rate = rate;
        }

        if(err == 0u)
        {
            break;
        }
    }

    {
        TIM_TimeBaseInitTypeDef t = {0};

        t.TIM_Prescaler     = (uint16_t)best_psc;
        t.TIM_Period        = (uint16_t)best_arr;
        t.TIM_ClockDivision = TIM_CKD_DIV1;
        t.TIM_CounterMode   = TIM_CounterMode_Up;
        TIM_TimeBaseInit(TIM4, &t);

        /* 更新事件作为 TRGO 送给 DAC（TIM_TimeBaseInit 会清掉，必须重设） */
        TIM_SelectOutputTrigger(TIM4, TIM_TRGOSource_Update);
        TIM_ARRPreloadConfig(TIM4, ENABLE);
    }

    return best_rate;
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化
 */
int dac_init(void)
{
    GPIO_InitTypeDef        g = {0};
    DAC_InitTypeDef         d = {0};
    TIM_TimeBaseInitTypeDef t = {0};

    if(s_inited)
    {
        return DAC_OK;
    }

    /* 1. 输出脚配成模拟输入（PA4=DAC1 / PA5=DAC2）*/
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    g.GPIO_Pin   = GPIO_Pin_4 | GPIO_Pin_5;   /* 两路 DAC 引脚都配成模拟 */
    g.GPIO_Mode  = GPIO_Mode_AIN;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &g);

    /* 2. TIM4：只出 TRGO，不开任何中断，不占中断向量 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    t.TIM_Prescaler     = 0;
    t.TIM_Period        = 99;
    t.TIM_ClockDivision = TIM_CKD_DIV1;
    t.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &t);
    TIM_SelectOutputTrigger(TIM4, TIM_TRGOSource_Update);
    TIM_ARRPreloadConfig(TIM4, ENABLE);
    TIM_Cmd(TIM4, DISABLE);

    /* 3. DMA2 通道：DAC1 用 Channel3，DAC2 用 Channel4 */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
    dma_setup(100);

    /* 4. DAC：外部触发（TIM4 TRGO），不开内部波形发生器，数据全由 DMA 喂 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
    DAC_DeInit();
    DAC_StructInit(&d);

    d.DAC_Trigger                      = DAC_Trigger_T4_TRGO;
    d.DAC_WaveGeneration               = DAC_WaveGeneration_None;
    d.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
#if DAC_OUTPUT_BUFFER
    d.DAC_OutputBuffer                 = DAC_OutputBuffer_Enable;
#else
    d.DAC_OutputBuffer                 = DAC_OutputBuffer_Disable;
#endif
    DAC_Init(DAC_Channel_1, &d);
    DAC_Init(DAC_Channel_2, &d);

    /* 先停在中点，避免上电跳变 */
    dac_set_val((uint16_t)((mv_to_code(s_lo_mv) + mv_to_code(s_hi_mv)) / 2U));
    DAC_Cmd(DAC_Channel_1, ENABLE);
    DAC_Cmd(DAC_Channel_2, ENABLE);

    s_inited = 1;

    return DAC_OK;
}

/**
 * @brief  生成波形并开始输出
 */
int dac_wave_start(dac_wave_t type, uint32_t freq_hz,
                   uint16_t high_mv, uint16_t low_mv)
{

    s_streaming = 0;            /* 上一次如果是音频流，这里接手 */
    uint32_t points;
    uint16_t tmp;
    uint16_t lo_code;
    uint16_t hi_code;

    if(freq_hz < DAC_MIN_FREQ_HZ || freq_hz > DAC_MAX_FREQ_HZ)
    {
        return DAC_ERR_PARAM;
    }
    if((type != DAC_WAVE_SINE) && (type != DAC_WAVE_SQUARE) && (type != DAC_WAVE_TRIANGLE))
    {
        return DAC_ERR_PARAM;
    }

    /* 限幅到 0 ~ 3300mV；顺序传反了自动纠正 */
    if(high_mv > DAC_VREF_MV)   { high_mv = DAC_VREF_MV; }
    if(low_mv  > DAC_VREF_MV)   { low_mv  = DAC_VREF_MV; }

    if(high_mv < low_mv)
    {
        tmp     = high_mv;
        high_mv = low_mv;
        low_mv  = tmp;
    }
    if(high_mv == low_mv)       /* 高低一样就是一条直流 */
    {
        return DAC_ERR_PARAM;
    }

    if(!s_inited)
    {
        if(dac_init() != DAC_OK)
        {
            return DAC_ERR_NOT_INIT;
        }
    }

    dac_stop();

    /* 1. 一个周期的点数：保证采样率不超过 1MHz，同时不超过表长 */
    points = DAC_MAX_SAMPLE_HZ / freq_hz;
    if(points < 8u)                     { points = 8u; }
    if(points > DAC_WAVE_MAX_POINTS)    { points = DAC_WAVE_MAX_POINTS; }

    s_type   = type;
    s_freq   = freq_hz;
    s_hi_mv  = high_mv;
    s_lo_mv  = low_mv;
    s_points = (uint16_t)points;

    lo_code = mv_to_code(s_lo_mv);
    hi_code = mv_to_code(s_hi_mv);

    /* 2. 只有波形/电压/点数变了才重填码表，纯改频率不动表 */
    if((s_tab_type != (uint8_t)type) ||
       (s_tab_hi   != hi_code)       ||
       (s_tab_lo   != lo_code)       ||
       (s_tab_pts  != s_points))
    {
        table_fill(type, s_points, lo_code, hi_code);
    }

    dma_setup(s_points);

    /* 3. 采样率 = 输出频率 x 每周期点数，交给 TIM4 去产生 */
    s_rate = tim4_apply(freq_hz * (uint32_t)s_points);

    /* 4. 启动顺序：DAC 先就绪 -> 开 DMA 请求 -> 开 DMA 通道 -> 最后开定时器 */
    dac_set_val(lo_code);
    DAC_Cmd(dac_chan(), ENABLE);
    DAC_DMACmd(dac_chan(), ENABLE);
    DMA_Cmd(dac_dma_ch(), ENABLE);
    TIM_Cmd(TIM4, ENABLE);

    s_running = 1;

    return DAC_OK;
}

/**
 * @brief  停止输出
 */
void dac_stop(void)
{
    if(!s_inited)
    {
        return;
    }

    TIM_Cmd(TIM4, DISABLE);
    DMA_Cmd(dac_dma_ch(), DISABLE);
    DAC_DMACmd(dac_chan(), DISABLE);

    /* 停在电压中点，不要突然掉到最低电平 */
    dac_set_val((uint16_t)((mv_to_code(s_lo_mv) + mv_to_code(s_hi_mv)) / 2U));

    s_running   = 0;
    s_streaming = 0;
}

/**
 * @brief  流式播放（音频用）：TIM4 按 sample_hz 触发，DMA 循环搬 buf
 * @param  sample_hz  采样率（典型 8000）
 * @param  len        buf 里有多少个 12 位码值
 * @note   1. buf 必须是"永久"的（不能是栈上的临时数组），DMA 一直在读它；
 *         2. 与 dac_wave_start() 互斥（都占 TIM4 + DMA）；
 *         3. 这个函数只负责"把缓存循环播出去"，往里面填数据是调用方的事。
 */
uint16_t *dac_stream_buf(uint16_t *len)
{
    if(len != 0)
    {
        *len = DAC_WAVE_MAX_POINTS;
    }

    return s_buf;
}

int dac_stream_start(uint32_t sample_hz, const uint16_t *buf, uint16_t len)
{
    DMA_InitTypeDef      d = {0};
    DMA_Channel_TypeDef *ch;

    if((buf == 0) || (len == 0u))
    {
        return DAC_ERR_PARAM;
    }

    if(dac_init() != DAC_OK)
    {
        return DAC_ERR_NOT_INIT;
    }

    dac_stop();

    /* TIM4 按采样率走 */
    s_rate = tim4_apply(sample_hz);

    ch = dac_dma_ch();
    DMA_Cmd(ch, DISABLE);
    DMA_DeInit(ch);

    d.DMA_PeripheralBaseAddr = dac_dreg_addr();
    d.DMA_MemoryBaseAddr     = (uint32_t)buf;      /* 直接搬外部缓存 */
    d.DMA_DIR                = DMA_DIR_PeripheralDST;
    d.DMA_BufferSize         = len;
    d.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    d.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    d.DMA_Mode               = DMA_Mode_Circular;
    d.DMA_Priority           = DMA_Priority_VeryHigh;
    d.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(ch, &d);

    dac_set_val(buf[0]);
    DAC_Cmd(dac_chan(), ENABLE);
    DAC_DMACmd(dac_chan(), ENABLE);
    DMA_Cmd(ch, ENABLE);
    TIM_Cmd(TIM4, ENABLE);

    s_running   = 1;
    s_streaming = 1;

    return DAC_OK;
}

/**
 * @brief  停止流式播放
 */
void dac_stream_stop(void)
{
    if(s_streaming == 0u)
    {
        return;
    }

    dac_stop();
    s_streaming = 0;
}

/**
 * @brief  当前是否在流式播放
 */
uint8_t dac_is_streaming(void)
{
    return s_streaming;
}

/**
 * @brief  运行时切换输出通道
 * @param  ch  1 = PA4（DAC1，扬声器）；2 = PA5（DAC2）
 * @return DAC_OK 成功；DAC_ERR_PARAM 参数错
 * @note   切换前会先停波形；DMA 的目标寄存器不同，所以必须重配 DMA
 */
int dac_channel_set(uint8_t ch)
{
    if((ch != 1u) && (ch != 2u))
    {
        return DAC_ERR_PARAM;
    }

    if(ch == s_ch)
    {
        return DAC_OK;
    }

    dac_stop();

    s_ch = ch;

    if(s_inited)
    {
        dma_setup((s_points != 0u) ? s_points : 1u);
        DAC_Cmd(dac_chan(), ENABLE);
    }

    return DAC_OK;
}

/**
 * @brief  当前用的是哪个通道
 */
uint8_t dac_channel_get(void)
{
    return s_ch;
}

/**
 * @brief  改频率
 */
int dac_set_freq(uint32_t freq_hz)
{
    return dac_wave_start(s_type, freq_hz, s_hi_mv, s_lo_mv);
}

/**
 * @brief  改波形
 */
int dac_set_wave(dac_wave_t type)
{
    return dac_wave_start(type, s_freq, s_hi_mv, s_lo_mv);
}

/**
 * @brief  实际输出频率
 */
uint32_t dac_freq_get(void)
{
    if(s_points == 0u)
    {
        return 0;
    }

    return s_rate / (uint32_t)s_points;
}

/**
 * @brief  当前采样率
 */
uint32_t dac_rate_get(void)
{
    return s_rate;
}

/**
 * @brief  每周期点数
 */
uint16_t dac_points_get(void)
{
    return s_points;
}

/**
 * @brief  取当前电压范围
 */
void dac_level_get(uint16_t *high_mv, uint16_t *low_mv)
{
    if(high_mv != (uint16_t *)0)
    {
        *high_mv = s_hi_mv;
    }
    if(low_mv != (uint16_t *)0)
    {
        *low_mv = s_lo_mv;
    }
}

/**
 * @brief  是否正在输出
 */
int dac_is_running(void)
{
    return s_running ? 1 : 0;
}
/**
 * @brief  取出当前码表（UI 画波形预览用）
 */
const uint16_t *dac_wave_table(uint16_t *len)
{
    if(len != (uint16_t *)0)
    {
        *len = s_points;
    }

    return s_buf;
}

/**
 * @brief  码表版本号
 */
uint32_t dac_wave_table_seq(void)
{
    return s_tab_seq;
}


void dac_deinit(void)
{
    dac_stop();                                     /* 停 TIM4 + DMA，输出回中点 */
    DAC_Cmd(DAC_Channel_1, DISABLE);
    DAC_Cmd(DAC_Channel_2, DISABLE);
    s_ch = 1;                                       /* 通道还回 PA4，给 SPEAKER */
    s_inited = 0;
}