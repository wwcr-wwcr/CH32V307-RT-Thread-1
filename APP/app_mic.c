#include "app_mic.h"
#include "ch32v30x.h"
#include "ch32v30x_opa.h"
#include <rtthread.h>
#include <math.h>


static int app_mic_adc_init(void);
static int app_mic_timer_init(void);
static void app_mic_adc_set_trigger(uint32_t trigger);
/*
 * 功能：开启麦克风电路使用的内部运放OPA3。
 * 同时配置ADC1、TIM3和DMA1时钟；定时器只在采集期间启动。
 */
int app_mic_init(void)
{
    app_mic_filter_reset();
    GPIO_InitTypeDef gpio = {0};
    OPA_InitTypeDef opa = {0};

    /* PA1为运放输出，PC2、PC5为运放输入。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC, ENABLE);

    /* 关闭数字输入输出功能，让引脚用于模拟信号。 */
    gpio.GPIO_Mode = GPIO_Mode_AIN;

    gpio.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_5;
    GPIO_Init(GPIOC, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_1;
    GPIO_Init(GPIOA, &gpio);

    /* 使用内部第3个运算放大器。 */
    opa.OPA_NUM = OPA3;

    /* 同相输入选择CHP1，对应PC5。 */
    opa.PSEL = CHP1;

    /* 反相输入选择CHN1，对应PC2。 */
    opa.NSEL = CHN1;

    /* 输出选择OUT0，对应PA1。 */
    opa.Mode = OUT_IO_OUT0;

    /* 写入运放配置，然后开启OPA3。 */
    OPA_Init(&opa);
    OPA_Cmd(OPA3, ENABLE);

    /* 给模拟电路留出稳定时间，同时让出CPU。 */
    rt_thread_mdelay(20);

    rt_kprintf("[MIC] OPA3 initialized\n");

    if (app_mic_timer_init() != 0)
    {
        return -4;
    }

    return app_mic_adc_init();
}

#define MIC_TIMEOUT_MS 100U

/* 保存ADC校准修正值。 */
static int16_t mic_calibration;

/* 只有初始化成功，才允许读取。 */
static uint8_t mic_ready;

/*
 * 功能：配置ADC1，采样PA1对应的通道1。
 * 返回：0成功，-1表示校准超时。
 */
static int app_mic_adc_init(void)
{
    ADC_InitTypeDef adc = {0};
    rt_tick_t start;

    mic_ready = 0U;

    /* 开启ADC1时钟。PA1的模拟模式已经在前一步配置。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    /* 当前PCLK2为96MHz，除以8后ADC时钟为12MHz。 */
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    /* 清除ADC1之前的配置。 */
    ADC_DeInit(ADC1);

    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1;

    ADC_Init(ADC1, &adc);

    /* PA1是通道1，放到规则转换序列的第1个位置。 */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 1, ADC_SampleTime_239Cycles5);

    ADC_Cmd(ADC1, ENABLE);

    /* 按WCH例程的方式进行ADC校准。 */
    ADC_BufferCmd(ADC1, DISABLE);
    rt_thread_mdelay(1);

    /* 先复位校准。 */
    start = rt_tick_get();
    ADC_ResetCalibration(ADC1);

    while (ADC_GetResetCalibrationStatus(ADC1) != RESET)
    {
        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(MIC_TIMEOUT_MS))
        {
            ADC_Cmd(ADC1, DISABLE);
            return -1;
        }

        rt_thread_mdelay(1);
    }

    /* 再执行校准。 */
    start = rt_tick_get();
    ADC_StartCalibration(ADC1);

    while (ADC_GetCalibrationStatus(ADC1) != RESET)
    {
        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(MIC_TIMEOUT_MS))
        {
            ADC_Cmd(ADC1, DISABLE);
            return -1;
        }

        rt_thread_mdelay(1);
    }

    mic_calibration = Get_CalibrationValue(ADC1);

    ADC_BufferCmd(ADC1, ENABLE);

    mic_ready = 1U;

    return 0;
}

int app_mic_read_raw(uint16_t *value)
{
    rt_tick_t start;
    uint16_t raw;
    int32_t corrected;

    if (value == NULL)
    {
        return -1;
    }

    if (mic_ready == 0U)
    {
        return -2;
    }

    /* 清除上一次的转换完成标志。 */
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);

    start = rt_tick_get();

    /* 启动一次ADC转换。 */
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    /* 等待本次转换结束，设置超时避免一直卡住。 */
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
    {
        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(MIC_TIMEOUT_MS))
        {
            ADC_SoftwareStartConvCmd(ADC1, DISABLE);
            return -3;
        }
    }

    raw = ADC_GetConversionValue(ADC1);

    /* 应用校准修正，并限制到12位ADC有效范围。 */
    corrected = (int32_t)raw + mic_calibration;

    if ((raw == 0U) || (corrected < 0))
    {
        corrected = 0;
    }
    else if ((raw == 4095U) || (corrected > 4095))
    {
        corrected = 4095;
    }

    *value = (uint16_t)corrected;

    return 0;
}

/* 在ADC停止且外部触发关闭时切换触发源，不复位ADC、不重新校准。
 * DMA结束后恢复软件触发，使原有read_raw仍可使用。
 */
static void app_mic_adc_set_trigger(uint32_t trigger)
{
    ADC_InitTypeDef adc = {0};
    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv = trigger;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &adc);
}

/* TIM3只产生内部触发，不输出PWM，不占用TIM3通道引脚。 */
static int app_mic_timer_init(void)
{
    RCC_ClocksTypeDef clocks;
    TIM_TimeBaseInitTypeDef timer = {0};
    uint32_t timer_hz;

    RCC_GetClocksFreq(&clocks);
    timer_hz = clocks.PCLK1_Frequency;
    if (clocks.PCLK1_Frequency != clocks.HCLK_Frequency)
    {
        timer_hz *= 2U;
    }

    /* 本例先把计数时钟分到1MHz；不符合整数分频条件时报告错误。 */
    if (timer_hz < 1000000U || timer_hz % 1000000U != 0U || timer_hz / 1000000U > 65536U)
    {
        return -4;
    }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    TIM_DeInit(TIM3);
    timer.TIM_Prescaler = timer_hz / 1000000U - 1U;
    timer.TIM_Period = 1000000U / APP_MIC_SAMPLE_RATE_HZ - 1U;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &timer);

    /* 每次更新事件通过TRGO触发一次ADC转换。 */
    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);
    TIM_Cmd(TIM3, DISABLE);

    return 0;
}

int app_mic_read_dma(uint16_t *buffer, uint16_t count)
{
    DMA_InitTypeDef dma = {0};
    rt_tick_t start;
    int result = 0;

    if (buffer == NULL || ((uintptr_t)buffer & 1U) != 0U || count == 0U || count > APP_MIC_DMA_MAX_SAMPLES)
    {
        return -1;
    }
    if (mic_ready == 0U)
    {
        return -2;
    }

    /* 重装DMA前先关闭采样源，避免配置期间发生搬运。 */
    TIM_Cmd(TIM3, DISABLE);
    ADC_ExternalTrigConvCmd(ADC1, DISABLE);
    ADC_DMACmd(ADC1, DISABLE);
    ADC_Cmd(ADC1, DISABLE);
    app_mic_adc_set_trigger(ADC_ExternalTrigConv_T3_TRGO);

    /*
     * ADC1还与NTC共用。每次DMA采样前重新选择麦克风所在的通道1，
     * 防止前一次NTC转换把规则通道留在通道8。
     */
    ADC_BufferCmd(ADC1, ENABLE);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 1, ADC_SampleTime_239Cycles5);
    DMA_DeInit(DMA1_Channel1);

    dma.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
    dma.DMA_MemoryBaseAddr = (uint32_t)buffer;
    dma.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize = count;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &dma);
    DMA_ClearFlag(DMA1_FLAG_GL1);

    /* 更新PSC、ARR并清零计数；此时ADC外部触发仍关闭，不产生额外采样。 */
    TIM_SetCounter(TIM3, 0);
    TIM_GenerateEvent(TIM3, TIM_EventSource_Update);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);

    ADC_Cmd(ADC1, ENABLE);
    rt_thread_mdelay(1);
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);

    /* 先让接收链路准备好，最后再开启定时器。 */
    DMA_Cmd(DMA1_Channel1, ENABLE);
    ADC_DMACmd(ADC1, ENABLE);
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
    start = rt_tick_get();
    TIM_Cmd(TIM3, ENABLE);

    while (DMA_GetFlagStatus(DMA1_FLAG_TC1) == RESET)
    {
        if (DMA_GetFlagStatus(DMA1_FLAG_TE1) != RESET)
        {
            result = -4;
            break;
        }
        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(MIC_TIMEOUT_MS))
        {
            result = -3;
            break;
        }

        /* 等待期间让出CPU，ADC和DMA仍按硬件时钟工作。 */
        rt_thread_mdelay(1);
    }
    if (DMA_GetFlagStatus(DMA1_FLAG_TE1) != RESET)
    {
        result = -4;
    }

    /* 成功和失败都停止本轮采样；关闭ADC终止可能残留的一次转换。 */
    TIM_Cmd(TIM3, DISABLE);
    ADC_ExternalTrigConvCmd(ADC1, DISABLE);
    ADC_DMACmd(ADC1, DISABLE);
    DMA_Cmd(DMA1_Channel1, DISABLE);
    ADC_Cmd(ADC1, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_GL1);

    /* 恢复软件单次采样，保持旧的app_mic_read_raw接口兼容。 */
    app_mic_adc_set_trigger(ADC_ExternalTrigConv_None);
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);
    ADC_Cmd(ADC1, ENABLE);
    rt_thread_mdelay(1);

    return result;
}

/* 纯计算，不访问硬件：返回ADC计数的峰峰值，参数错误返回-1。 */
int app_mic_peak_to_peak(const uint16_t *buffer, uint16_t count)
{
    uint16_t minimum;
    uint16_t maximum;
    uint16_t i;

    if (buffer == NULL || count == 0U)
    {
        return -1;
    }

    minimum = buffer[0];
    maximum = buffer[0];

    for (i = 1; i < count; i++)
    {
        if (buffer[i] < minimum)
        {
            minimum = buffer[i];
        }
        if (buffer[i] > maximum)
        {
            maximum = buffer[i];
        }
    }

    return maximum - minimum;
}

/* 只保存上一帧的平滑能量，不保存整段声音，不申请动态内存。 */
static float mic_filtered_energy;
static uint8_t mic_filter_ready;

void app_mic_filter_reset(void)
{
    mic_filtered_energy = 0.0f;
    mic_filter_ready = 0U;
}

static float app_mic_energy_to_dbfs(float energy)
{
    /* 本实现参考能量为2047.5^2；对数前设置显示下限，避免log10(0)。 */
    const float reference_energy = 2047.5f * 2047.5f;
    const float floor_energy = 2047.5f * 2047.5f * 1.0e-12f;

    if (energy <= floor_energy)
    {
        return -120.0f;
    }

    return 10.0f * log10f(energy / reference_energy);
}

int app_mic_analyze(const uint16_t *buffer, uint16_t count, app_mic_level_t *out)
{
    app_mic_level_t value = {0};
    uint32_t sum = 0;
    uint16_t i;
    float mean;
    float ac;
    float energy = 0.0f;

    if (buffer == NULL || out == NULL || count < 2U || count > APP_MIC_DMA_MAX_SAMPLES)
    {
        return -1;
    }

    value.minimum = buffer[0];
    value.maximum = buffer[0];

    /* 先校验并求均值。采用真实均值，不固定减2048，允许电路有直流偏差。 */
    for (i = 0; i < count; i++)
    {
        if (buffer[i] > 4095U)
        {
            return -1;
        }
        sum += buffer[i];
        if (buffer[i] < value.minimum) value.minimum = buffer[i];
        if (buffer[i] > value.maximum) value.maximum = buffer[i];
        if (buffer[i] == 0U || buffer[i] == 4095U) value.rail_samples++;
    }

    mean = (float)sum / count;
    value.mean = (uint16_t)(mean + 0.5f);
    value.peak_to_peak = value.maximum - value.minimum;

    /* 能量=交流样本平方的平均；正负声音都贡献正能量，不会相互抵消。 */
    for (i = 0; i < count; i++)
    {
        ac = (float)buffer[i] - mean;
        energy += ac * ac;
    }
    energy /= count;

    /* 第一帧直接初始化，避免从0能量缓慢爬升。之后做能量域EMA。 */
    if (mic_filter_ready == 0U)
    {
        mic_filtered_energy = energy;
        mic_filter_ready = 1U;
    }
    else
    {
        mic_filtered_energy += (energy - mic_filtered_energy) / APP_MIC_FILTER_DIV;
    }

    value.rms_counts = sqrtf(energy);
    value.filtered_rms_counts = sqrtf(mic_filtered_energy);
    value.dbfs = app_mic_energy_to_dbfs(energy);
    value.filtered_dbfs = app_mic_energy_to_dbfs(mic_filtered_energy);
    value.filtered_dbfs_x10 = (int32_t)lroundf(value.filtered_dbfs * 10.0f);

    *out = value;

    return 0;
}

/* 将采集和计算封装在APP中，调用者只接收最终数值。 */
int32_t app_mic_get_db_x10(void)
{
    static uint16_t samples[APP_MIC_FRAME_SAMPLES];
    app_mic_level_t level;

    if (app_mic_read_dma(samples, APP_MIC_FRAME_SAMPLES) != 0)
    {
        return APP_MIC_DB_ERROR;
    }

    if (app_mic_analyze(samples, APP_MIC_FRAME_SAMPLES, &level) != 0)
    {
        return APP_MIC_DB_ERROR;
    }

    return level.filtered_dbfs_x10;
}
