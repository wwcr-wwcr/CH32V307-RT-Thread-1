/******************************************************************************
 * 文件名   : adc_dma.c
 * 芯片     : CH32V307VCT6
 * 说明     : ADC1 + DMA1_Channel1 + TIM3 TRGO 定时采样
 *
 * 为什么 CPU 全程不参与：
 *     TIM3 的更新事件直接触发 ADC 转换，ADC 转换完 DMA 自己来取 RDATAR，
 *     写进 s_buf。1024 点全部由硬件完成，CPU 一次中断都不进。
 *     只有采满 1024 点时 DMA 产生一次传输完成中断，这里只置一个标志。
 ******************************************************************************/

#include "adc_dma.h"
#include "adc.h"
#include "rtthread.h"
#include "rthw.h"

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
/* 采样缓冲：DMA 的目标地址，必须是全局静态 + 4 字节对齐 */
static uint16_t s_buf[ADC_DMA_POINTS] __attribute__((aligned(4)));

/* 当前采集通道 / 对应引脚。默认 PB1（示波器外部输入），
 * 麦克风在 PA1，由 task_fft 用 adc_dma_channel_set(MIC_CH_1) 切过去。 */
static uint8_t       s_ch   = ADC_DMA_CHANNEL;
static GPIO_TypeDef *s_port = ADC_DMA_GPIO;
static uint16_t      s_pin  = ADC_DMA_PIN;

static volatile uint8_t s_done = 0;
static volatile uint8_t s_busy = 0;
static uint32_t s_rate_hz = 4000;
static adc_dma_cb_t s_cb = RT_NULL;

static struct rt_semaphore s_dma_sem;
static uint8_t s_inited = 0;      /* ADC/DMA/信号量是否已初始化过 */       /* 用于 adc_dma_wait 阻塞等待 */

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* TIM3：10MHz 计数，更新事件作为 ADC 的触发源（不开中断，只用 TRGO） */
static void adc_dma_tim_init(uint32_t sample_rate_hz)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};

    if(sample_rate_hz == 0)
    {
        sample_rate_hz = 1000;
    }
    if(sample_rate_hz > ADC_DMA_MAX_RATE_HZ)
    {
        sample_rate_hz = ADC_DMA_MAX_RATE_HZ;
    }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    /* 计数基准 1MHz：ARR = 1MHz/rate，PSC = SystemCoreClock/1MHz。
     * 原来用 10MHz 基准除不尽（实际 10.28MHz），采样率会偏 2.9%，
     * 示波器测出来的频率就跟着偏。 */
    TIM_TimeBaseInitStructure.TIM_Period        = (uint16_t)(1000000UL / sample_rate_hz - 1);
    TIM_TimeBaseInitStructure.TIM_Prescaler     = (uint16_t)(SystemCoreClock / 1000000UL - 1);
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);

    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);
    TIM_ARRPreloadConfig(TIM3, ENABLE);

    /* 注意：不使能 TIM3 中断，中断留给 DMA，避免和 L_adc.c 的 TIM3_IRQHandler 冲突 */
    TIM_Cmd(TIM3, DISABLE);
}

/* ADC1：外部触发（TIM3 TRGO）+ 连续转换 + DMA 请求 */
static void adc_dma_adc_init(void)
{
    ADC_InitTypeDef ADC_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    /* PCLK2 已降到 72MHz（APB2 二分频），ADC 再 /8 = 9MHz，低于 14MHz 上限 */
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    ADC_DeInit(ADC1);
    ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode       = DISABLE;
    /* 单次转换：每个 TIM3 更新事件只触发一次转换，采样率才等于 TIM3 频率。
     * 写成 ENABLE 的话首次触发后 ADC 会自己连轴转，采样率不受 TIM3 控制。 */
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_T3_TRGO;
    ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_RegularChannelConfig(ADC1, s_ch, 1, ADC_DMA_SAMPLE_TIME);

    ADC_Cmd(ADC1, ENABLE);

    ADC_BufferCmd(ADC1, DISABLE);
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1))
    {
        /* 等待复位校准 */
    }
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1))
    {
        /* 等待校准 */
    }

    /* ADC_Init 只写了 EXTSEL，EXTTRIG 位必须单独打开，
     * 否则 TIM3 的 TRGO 进不来，DMA 永远等不到数据。 */
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);

    ADC_DMACmd(ADC1, ENABLE);                              /* ADC 转换完自动请求 DMA */
}
/* DMA1_Channel1：外设(ADC1->RDATAR) -> 内存(s_buf)，1024 个半字 */
static void adc_dma_hw_init(void)
{
    DMA_InitTypeDef DMA_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(DMA1_Channel1);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)s_buf;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize         = ADC_DMA_POINTS;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc         = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;     /* 采满 1024 点就停 */
    DMA_InitStructure.DMA_Priority           = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);

    DMA_ITConfig(DMA1_Channel1, DMA_IT_TC, ENABLE);
    DMA_ClearFlag(DMA1_FLAG_TC1);

    NVIC_InitStructure.NVIC_IRQChannel                   = DMA1_Channel1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    DMA_Cmd(DMA1_Channel1, ENABLE);
}

/* 重新装载 DMA 计数器（Normal 模式采完会停，再次采集要先复位） */
static void adc_dma_reload(void)
{
    DMA_Cmd(DMA1_Channel1, DISABLE);
    DMA_SetCurrDataCounter(DMA1_Channel1, ADC_DMA_POINTS);
    DMA_ClearFlag(DMA1_FLAG_TC1);
    DMA_ClearITPendingBit(DMA1_IT_TC1);
    DMA_Cmd(DMA1_Channel1, ENABLE);
}

/**************************************************************************************************
 *                                        DMA 中断
 **************************************************************************************************/
void DMA1_Channel1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel1_IRQHandler(void)
{
    GET_INT_SP();
    rt_interrupt_enter();

    if(DMA_GetITStatus(DMA1_IT_TC1) != RESET)
    {
        /* 采满 1024 点：置标志、停定时器、唤醒等待者，全程不做数据处理 */
        TIM_Cmd(TIM3, DISABLE);

        s_done = 1;
        s_busy = 0;

        DMA_ClearITPendingBit(DMA1_IT_TC1);

        rt_sem_release(&s_dma_sem);

        if(s_cb != RT_NULL)
        {
            s_cb(s_buf, ADC_DMA_POINTS);
        }
    }

    rt_interrupt_leave();
    FREE_INT_SP();
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/* ADC 通道 -> 引脚，和 adc.c 里的映射保持一致 */
static int adc_dma_pin_of(uint8_t ch, GPIO_TypeDef **port, uint16_t *pin)
{
    if(ch <= ADC_Channel_7)
    {
        *port = GPIOA;
        *pin  = (uint16_t)(GPIO_Pin_0 << ch);
        return 0;
    }
    if(ch == ADC_Channel_8)
    {
        *port = GPIOB;
        *pin  = GPIO_Pin_0;
        return 0;
    }
    if(ch == ADC_Channel_9)
    {
        *port = GPIOB;
        *pin  = GPIO_Pin_1;
        return 0;
    }
    return -1;
}

/**
 * @brief  切换采集通道
 * @note   在 adc_dma_init() 之前调只是记住设置；已经初始化过就立刻生效。
 */
int adc_dma_channel_set(uint8_t adc_ch)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_TypeDef    *port;
    uint16_t         pin;

    if(adc_dma_pin_of(adc_ch, &port, &pin) != 0)
    {
        return -1;
    }

    s_ch   = adc_ch;
    s_port = port;
    s_pin  = pin;

    if(s_inited)
    {
        /* 已经在跑：把新引脚配成模拟输入，并把规则组第 1 个通道换掉 */
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
        GPIO_InitStructure.GPIO_Pin  = s_pin;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(s_port, &GPIO_InitStructure);

        ADC_RegularChannelConfig(ADC1, s_ch, 1, ADC_DMA_SAMPLE_TIME);
    }

    return 0;
}

/**
 * @brief  当前采集通道
 */
uint8_t adc_dma_channel_get(void)
{
    return s_ch;
}

/**
 * @brief  初始化
 */
int adc_dma_init(uint32_t sample_rate_hz)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    if(sample_rate_hz == 0 || sample_rate_hz > ADC_DMA_MAX_RATE_HZ)
    {
        return ADC_DMA_ERR_PARAM;
    }

    s_rate_hz = sample_rate_hz;

    /* 1. 引脚：PB1 模拟输入 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin  = s_pin;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(s_port, &GPIO_InitStructure);

    /* 2. ADC / DMA / TIM3 */
    adc_dma_adc_init();
    adc_dma_hw_init();
    adc_dma_tim_init(sample_rate_hz);

    /* 3. 完成信号量 */
    /* 只初始化一次：重复 rt_sem_init 会破坏 RT-Thread 对象链表，
     * 之后一次 rt_sem_release 就可能硬 fault（进页面后整机卡死）。 */
    if(!s_inited)
    {
        rt_sem_init(&s_dma_sem, "adma", 0, RT_IPC_FLAG_FIFO);
        s_inited = 1;
    }

    s_done = 0;
    s_busy = 0;

    return ADC_DMA_OK;
}

/**
 * @brief  开始一次采集
 */
/**
 * @brief  只改采样率（重配 TIM3）
 */
int adc_dma_set_rate(uint32_t sample_rate_hz)
{
    if((sample_rate_hz == 0u) || (sample_rate_hz > ADC_DMA_MAX_RATE_HZ))
    {
        return ADC_DMA_ERR_PARAM;
    }
    if(!s_inited)
    {
        return ADC_DMA_ERR_PARAM;
    }

    TIM_Cmd(TIM3, DISABLE);
    adc_dma_tim_init(sample_rate_hz);
    s_rate_hz = sample_rate_hz;

    return ADC_DMA_OK;
}
int adc_dma_start(void)
{
    if(s_busy)
    {
        return ADC_DMA_ERR_BUSY;
    }

    s_done = 0;
    s_busy = 1;

    /* 清掉信号量的历史计数：上一次采集完成时 release 过一次，
     * 不清的话这次 wait() 会立刻返回，读到半截缓冲。 */
    if(s_inited)
    {
        (void)rt_sem_control(&s_dma_sem, RT_IPC_CMD_RESET, RT_NULL);
    }

    adc_dma_reload();

    TIM_SetCounter(TIM3, 0);
    TIM_Cmd(TIM3, ENABLE);

    return ADC_DMA_OK;
}

/**
 * @brief  停止采集
 */
void adc_dma_stop(void)
{
    TIM_Cmd(TIM3, DISABLE);
    ADC_DMACmd(ADC1, DISABLE);              /* 新增 */
    ADC_ExternalTrigConvCmd(ADC1, DISABLE); /* 新增：EXTSEL 还回软件触发 */
    DMA_Cmd(DMA1_Channel1, DISABLE);        /* 新增 */
    ADC_Cmd(ADC1, DISABLE);                 /* 新增 */
    ADC_DeInit(ADC1);                       /* 新增：连同校准值一起清掉 */
    s_busy = 0;
}
/**
 * @brief  是否完成
 */
int adc_dma_is_done(void)
{
    return s_done ? 1 : 0;
}

/**
 * @brief  等待采满（超时会自动复位 busy，避免之后再也 start 不了）
 */
int adc_dma_wait(uint32_t timeout_ms)
{
    if(rt_sem_take(&s_dma_sem, rt_tick_from_millisecond(timeout_ms)) != RT_EOK)
    {
        TIM_Cmd(TIM3, DISABLE);
        s_busy = 0;
        return ADC_DMA_ERR_TIMEOUT;
    }

    return ADC_DMA_OK;
}
/**
 * @brief  取波形
 */
const uint16_t *adc_dma_buffer(void)
{
    return s_buf;
}

/**
 * @brief  当前采样率
 */
uint32_t adc_dma_rate_hz(void)
{
    return s_rate_hz;
}

/**
 * @brief  注册回调
 */
void adc_dma_callback_set(adc_dma_cb_t cb)
{
    s_cb = cb;
}
