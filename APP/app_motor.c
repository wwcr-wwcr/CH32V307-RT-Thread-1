#include "app_motor.h"
#include "ch32v30x.h"

/* 当前工程TIM1时钟为96MHz，输出10kHz PWM。 */
#define MOTOR_PWM_PSC       95U
#define MOTOR_PWM_ARR       99U
#define MOTOR_PWM_COUNTS    (MOTOR_PWM_ARR + 1U)

void app_motor_init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timer;
    TIM_OCInitTypeDef pwm;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1, ENABLE);

    /* 恢复TIM1初始状态，暂时关闭各通道输出。 */
    TIM_DeInit(TIM1);

    /* PA8和PA11分别交给TIM1的通道1和通道4。 */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* 配置计数速度和周期。 */
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = MOTOR_PWM_PSC;
    timer.TIM_Period = MOTOR_PWM_ARR;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM1, &timer);

    /* 两个通道都配置为PWM1，初始比较值为0。 */
    TIM_OCStructInit(&pwm);
    pwm.TIM_OCMode = TIM_OCMode_PWM1;
    pwm.TIM_OutputState = TIM_OutputState_Enable;
    pwm.TIM_OCPolarity = TIM_OCPolarity_High;
    pwm.TIM_OCIdleState = TIM_OCIdleState_Reset;
    pwm.TIM_Pulse = 0U;

    TIM_OC1Init(TIM1, &pwm);
    TIM_OC4Init(TIM1, &pwm);

    /* 修改比较值后，在更新事件时统一生效。 */
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_OC4PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);

    /* 将预分频、周期和比较值装入工作寄存器。 */
    TIM_GenerateEvent(TIM1, TIM_EventSource_Update);
    TIM_ClearFlag(TIM1, TIM_FLAG_Update);

    /* TIM1是高级定时器，需要额外开启总输出。 */
    TIM_CtrlPWMOutputs(TIM1, ENABLE);

    TIM_Cmd(TIM1, ENABLE);
}

int app_motor_set_duty(uint8_t duty_percent)
{
    uint16_t compare;

    if (duty_percent > 100U)
    {
        return -1;
    }

    /* CCR = 一周期计数次数 × 占空比。 */
    compare = (uint16_t)((uint32_t)MOTOR_PWM_COUNTS * duty_percent / 100U);

    /* 固定一个方向：INB保持低电平，INA输出PWM。 */
    TIM_SetCompare4(TIM1, 0U);
    TIM_SetCompare1(TIM1, compare);

    /* 主动更新，让新的比较值生效。 */
    TIM_GenerateEvent(TIM1, TIM_EventSource_Update);
    TIM_ClearFlag(TIM1, TIM_FLAG_Update);

    return 0;
}