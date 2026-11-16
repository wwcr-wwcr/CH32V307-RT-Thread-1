#include "app_ntc.h"
#include "ch32v30x.h"
#include <rtthread.h>
#include <math.h>
#include "app_segment.h"

/* NTC阻值 = 固定电阻 × 采样电压 ÷ (供电电压－采样电压) */
#define NTC_TIMEOUT_MS   100

/* 指数平滑除数：越大越平滑，但响应越慢；建议每500ms调用一次。 */
#define NTC_FILTER_DIVISOR  4.0f
/* 沿用参考工程的热敏电阻参数。 */
#define NTC_FIXED_R       10000.0f  /* 分压固定电阻，单位Ω */
#define NTC_R25           10000.0f  /* NTC在25℃时的阻值 */
#define NTC_B_VALUE       3380.0f   /* B值，单位K */
#define NTC_T25_K         298.15f   /* 25℃对应的开尔文温度 */

/* ADC校准偏移量，可能为正数或负数。 */
static int16_t ntc_calibration;

/* 标记初始化是否完成。 */
static uint8_t ntc_ready;

/* 保存带小数的温度乘10滤波值，以及首次有效采样标记。 */
static float ntc_filtered_x10;
static uint8_t ntc_filter_ready;

int app_ntc_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    ADC_InitTypeDef adc = {0};
    rt_tick_t start;

    ntc_ready = 0U;
    ntc_filter_ready = 0U;
    ntc_filtered_x10 = 0.0f;

    /* 开启GPIOB与ADC1时钟。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_ADC1, ENABLE);

    /* 当前PCLK2为96MHz，除以8后ADC时钟为12MHz。 */
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    /* PB0用于测量电压，配置成模拟输入。 */
    gpio.GPIO_Pin = GPIO_Pin_0;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOB, &gpio);

    /* 将ADC1恢复为初始状态，再填写本次配置。 */
    ADC_DeInit(ADC1);

    adc.ADC_Mode = ADC_Mode_Independent; //独立工作
    adc.ADC_ScanConvMode = DISABLE;//关闭扫描模式
    adc.ADC_ContinuousConvMode = DISABLE;//关闭连续转换
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;//不使用外部触发源
    adc.ADC_DataAlign = ADC_DataAlign_Right;//转化结果右对齐
    adc.ADC_NbrOfChannel = 1;

    ADC_Init(ADC1, &adc);

    /* 单通道采样：通道8放在转换序列的第1个位置。 */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_8, 1, ADC_SampleTime_239Cycles5);

    ADC_Cmd(ADC1, ENABLE);

    /* 沿用WCH外部通道示例的校准方式。 */
    ADC_BufferCmd(ADC1, DISABLE);
    rt_thread_mdelay(1);

    /* 第一步：复位校准。 */
    start = rt_tick_get();
    ADC_ResetCalibration(ADC1);

    while (ADC_GetResetCalibrationStatus(ADC1) != RESET)
    {
        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(NTC_TIMEOUT_MS))
        {
            ADC_Cmd(ADC1, DISABLE);
            return -1;
        }

        rt_thread_mdelay(1);
    }

    /* 第二步：执行校准。 */
    start = rt_tick_get();
    ADC_StartCalibration(ADC1);

    while (ADC_GetCalibrationStatus(ADC1) != RESET)
    {
        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(NTC_TIMEOUT_MS))
        {
            ADC_Cmd(ADC1, DISABLE);
            return -1;
        }

        rt_thread_mdelay(1);
    }

    ntc_calibration = Get_CalibrationValue(ADC1);
    ntc_ready = 1U;

    return 0;
}

int32_t app_ntc_read_raw(void)
{
    rt_tick_t start;
    uint16_t raw;
    int32_t value;

    if (ntc_ready == 0U)
    {
        return -1;
    }

    /*
     * ADC1还与麦克风共用。TASK12会依次读取NTC和麦克风，因此每次
     * 转换前都重新选择通道8，不能假定最后初始化ADC的一定是NTC。
     */
    ADC_BufferCmd(ADC1, DISABLE);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_8, 1, ADC_SampleTime_239Cycles5);

    /* 清除上一次的转换完成标志。 */
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);

    start = rt_tick_get();

    /* 软件启动一次转换。 */
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    /* 等待本次转换完成。 */
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
    {
        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(NTC_TIMEOUT_MS))
        {
            ADC_SoftwareStartConvCmd(ADC1, DISABLE);
            return -1;
        }
    }

    raw = ADC_GetConversionValue(ADC1);

    /* 按校准偏移修正，并限制在12位ADC的有效范围内。 */
    value = (int32_t)raw + ntc_calibration;

    if ((raw == 0U) || (value < 0))
    {
        return 0;
    }

    if ((raw == 4095U) || (value > 4095))
    {
        return 4095;
    }

    return value;
}

int app_ntc_get_temperature(int32_t *temperature_x10)
{
    int32_t raw;
    float resistance;
    float temperature_k;
    float temperature_c;
    float scaled;

    /* 调用者必须提供有效的结果存放地址。 */
    if (temperature_x10 == 0)
    {
        return -1;
    }

    raw = app_ntc_read_raw();

    /* 排除读取失败和接近电源轨的端点值。 */
    if ((raw <= 0) || (raw >= 4095))
    {
        return -1;
    }

    /* 第一步：根据板上的分压接法，计算NTC阻值。 */
    resistance = NTC_FIXED_R * (float)raw / (4095.0f - (float)raw);

    /* 第二步：通过B参数公式，计算开尔文温度。 */
    temperature_k = 1.0f / (1.0f / NTC_T25_K + logf(resistance / NTC_R25) / NTC_B_VALUE);

    /* 第三步：转换成摄氏温度。 */
    temperature_c = temperature_k - 273.15f;

    /* 第四步：放大10倍，并四舍五入为整数。 */
    scaled = temperature_c * 10.0f;

    if (ntc_filter_ready == 0U)
    {
        /* 第一笔直接采用实际温度，避免从0摄氏度缓慢爬升。 */
        ntc_filtered_x10 = scaled;
        ntc_filter_ready = 1U;
    }
    else
    {
        /* 一阶指数平滑：旧结果 + 本次差值的四分之一。 */
        ntc_filtered_x10 += (scaled - ntc_filtered_x10) / NTC_FILTER_DIVISOR;
    }

    /* 滤波后再取整，防止微小温度变化被提前舍弃。 */
    if (ntc_filtered_x10 >= 0.0f)
    {
        *temperature_x10 = (int32_t)(ntc_filtered_x10 + 0.5f);
    }
    else
    {
        *temperature_x10 = (int32_t)(ntc_filtered_x10 - 0.5f);
    }

    return 0;
}

