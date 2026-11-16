#include "app_encoder.h"
#include "ch32v30x.h"
#include <rtthread.h>

void app_encoder_init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timer;
    TIM_ICInitTypeDef input;

    /* GPIO及重映射使用APB2，TIM3使用APB1。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    TIM_DeInit(TIM3);

    /* 使用TIM3默认引脚：CH1=PA6，CH2=PA7。 */
    GPIO_PinRemapConfig(GPIO_FullRemap_TIM3, DISABLE);

    /* 沿用原TASK04的输入配置。 */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    /* 使用完整16位计数范围，不对计数进行预分频。 */
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = 0U;
    timer.TIM_Period = 65535U;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM3, &timer);

    /* 配置A相输入通道。 */
    TIM_ICStructInit(&input);
    input.TIM_Channel = TIM_Channel_1;
    input.TIM_ICSelection = TIM_ICSelection_DirectTI;
    input.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    input.TIM_ICPolarity = TIM_ICPolarity_Rising;
    input.TIM_ICFilter = 6U;
    TIM_ICInit(TIM3, &input);

    /* B相使用相同配置，只修改通道号。 */
    input.TIM_Channel = TIM_Channel_2;
    TIM_ICInit(TIM3, &input);

    /* 两相编码器模式，由硬件判断计数方向。 */
    TIM_EncoderInterfaceConfig(TIM3, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);

    TIM_SetCounter(TIM3, 0U);
    TIM_Cmd(TIM3, ENABLE);
}

uint16_t app_encoder_get_count(void)
{
    return TIM_GetCounter(TIM3);
}

//两次采样的计数差
int32_t app_encoder_get_delta(uint16_t current, uint16_t previous)
{
    int32_t delta;

    delta = (int32_t)current - (int32_t)previous;

    /* 正方向经过65535→0的回绕。 */
    if (delta < -32768L)
    {
        delta += 65536L;
    }
    /* 反方向经过0→65535的回绕。 */
    else if (delta > 32767L)
    {
        delta -= 65536L;
    }

    return delta;
}

/* 转速rpm = 计数变化 ÷ 每转计数 ÷ 经过秒数 × 60 */
int app_encoder_calculate_rpm(int32_t delta, uint32_t elapsed_ticks, uint32_t counts_per_rev, int32_t *rpm)
{
    int64_t numerator;
    int64_t denominator;

    if ((rpm == 0) || (elapsed_ticks == 0U) || (counts_per_rev == 0U))
    {
        return -1;
    }

    /* 半个计数范围的变化无法可靠判断方向。 */
    if ((delta <= -32768L) || (delta > 32767L))
    {
        return -1;
    }

    /* 本学习版本要求采样及时，过长间隔不输出转速。 */
    if (elapsed_ticks > (uint32_t)rt_tick_from_millisecond(500))
    {
        return -1;
    }

    numerator = (int64_t)delta * 60LL * RT_TICK_PER_SECOND;
    denominator = (int64_t)counts_per_rev * elapsed_ticks;

    *rpm = (int32_t)(numerator / denominator);

    return 0;
}

void app_encoder_speed_demo(void)
{
    /* 模拟每转400计数，仅用于本次演示。 */
    const uint32_t demo_cpr = 400U;

    /* 每一项表示一个虚拟100ms内的计数变化。 */
    const int32_t changes[] = {40, 80, 0, -40, -80};

    uint16_t previous = 65520U;
    uint16_t current;
    int32_t delta;
    int32_t rpm;
    uint8_t i;
    rt_tick_t elapsed;

    elapsed = rt_tick_from_millisecond(100);

    rt_kprintf("[SIM] encoder demo, CPR=400, interval=100ms\n");

    for (i = 0; i < 5U; i++)
    {
        /* 模拟16位硬件计数器，自然产生回绕。 */
        current = (uint16_t)((int32_t)previous + changes[i]);

        /* 使用原来的函数，计算相邻读数的变化量。 */
        delta = app_encoder_get_delta(current, previous);

        if (app_encoder_calculate_rpm(delta, (uint32_t)elapsed, demo_cpr, &rpm) == 0)
        {
            rt_kprintf("[SIM] CNT=%u, delta=%ld, rpm=%ld\n", (unsigned int)current, (long)delta, (long)rpm);
        }
        else
        {
            rt_kprintf("[SIM] calculation failed\n");
        }

        previous = current;
    }
}