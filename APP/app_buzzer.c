#include "app_buzzer.h"
#include "ch32v30x.h"

/* 沿用原工程：TIM2时钟96MHz，预分频后为1MHz。 */
#define BUZZER_PRESCALER    95U
#define BUZZER_COUNTER_HZ   1000000UL

void app_buzzer_init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timer;
    TIM_OCInitTypeDef pwm;

    /* PA2属于GPIOA，TIM2属于APB1。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* 将PA2交给定时器输出PWM。 */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_2;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* 初始化期间先停止定时器。 */
    TIM_Cmd(TIM2, DISABLE);

    /* 配置计数频率和周期，默认准备2000Hz。 */
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = BUZZER_PRESCALER;
    timer.TIM_Period = 499U;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM2, &timer);

    /* 配置通道3为PWM模式，初始占空比为0。 */
    TIM_OCStructInit(&pwm);
    pwm.TIM_OCMode = TIM_OCMode_PWM1;
    pwm.TIM_OutputState = TIM_OutputState_Enable;
    pwm.TIM_OCPolarity = TIM_OCPolarity_High;
    pwm.TIM_Pulse = 0U;
    TIM_OC3Init(TIM2, &pwm);

    /* 周期值和比较值使用预装载。 */
    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);

    /* 产生更新事件，让预分频及预装载参数生效。 */
    TIM_GenerateEvent(TIM2, TIM_EventSource_Update);
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);

    /* 定时器运行，但比较值为0，因此不鸣叫。 */
    TIM_Cmd(TIM2, ENABLE);
}

int app_buzzer_start(uint32_t frequency_hz)
{
    uint32_t counts;

    /* 先检查范围，也避免除以0。 */
    if ((frequency_hz < 100U) || (frequency_hz > 10000U))
    {
        return -1;
    }

    /* 计算一个PWM周期需要多少次计数。 */
    counts = BUZZER_COUNTER_HZ / frequency_hz;

    /* 暂停计数，统一修改周期和占空比。 */
    TIM_Cmd(TIM2, DISABLE);

    TIM_SetAutoreload(TIM2, counts - 1U);
    TIM_SetCompare3(TIM2, counts / 2U);
    TIM_SetCounter(TIM2, 0U);

    /* 将修改后的参数装入实际工作的寄存器。 */
    TIM_GenerateEvent(TIM2, TIM_EventSource_Update);
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);

    TIM_Cmd(TIM2, ENABLE);

    return 0;
}

void app_buzzer_stop(void)
{
    /* 将通道3占空比设为0，使输出保持低电平。 */
    TIM_SetCompare3(TIM2, 0U);

    /* 使用了预装载，主动更新让0立即装入。 */
    TIM_GenerateEvent(TIM2, TIM_EventSource_Update);
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);
}