#include "app_rgb.h"
#include "ch32v30x.h"
#include "ch32v30x_it.h"
#include <rtthread.h>

/* 硬件连接定义，只在当前文件中使用。 */
#define RGB_GPIO       GPIOB
#define RGB_RED_PIN    GPIO_Pin_3
#define RGB_GREEN_PIN  GPIO_Pin_4
#define RGB_BLUE_PIN   GPIO_Pin_5
#define RGB_ALL_PINS   (RGB_RED_PIN | RGB_GREEN_PIN | RGB_BLUE_PIN)

/* 一个PWM周期分成20份。 */
#define RGB_PWM_STEPS  20U

/* 一个周期内需要点亮的份数，由线程修改、中断读取。 */
static volatile uint8_t rgb_pwm_level = 0U;

/* 当前进行到第几份，范围0～19。 */
static uint8_t rgb_pwm_phase = 0U;

/* 呼吸过程共40步：前20步变亮，后20步变暗。 */
static uint8_t rgb_breath_phase = 0U;

void app_rgb_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 先开启GPIOB时钟，才能配置和操作GPIOB。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    /* 预先将输出寄存器设置为高电平，避免切换输出模式时短暂亮灯。 */
    GPIO_SetBits(RGB_GPIO, RGB_ALL_PINS);

    /* 三个引脚使用相同模式，可以一次初始化。 */
    gpio.GPIO_Pin = RGB_ALL_PINS;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(RGB_GPIO, &gpio);
}

void app_rgb_set(uint8_t color)
{
    /* 每次先熄灭全部颜色，避免保留上一次的亮灯状态。 */
    GPIO_SetBits(RGB_GPIO, RGB_ALL_PINS);

    /* 选中红色标志，就将红灯引脚拉低。 */
    if ((color & APP_RGB_RED) != 0U)
    {
        GPIO_ResetBits(RGB_GPIO, RGB_RED_PIN);
    }

    /* 选中绿色标志，就将绿灯引脚拉低。 */
    if ((color & APP_RGB_GREEN) != 0U)
    {
        GPIO_ResetBits(RGB_GPIO, RGB_GREEN_PIN);
    }

    /* 选中蓝色标志，就将蓝灯引脚拉低。 */
    if ((color & APP_RGB_BLUE) != 0U)
    {
        GPIO_ResetBits(RGB_GPIO, RGB_BLUE_PIN);
    }
}

void app_rgb_pwm_set_brightness(uint8_t percent)
{
    if (percent > 100U)
    {
        percent = 100U;
    }

    /* 百分比转换成0～20份，取最接近的5%档位。 */
    rgb_pwm_level = (uint8_t)((percent + 2U) / 5U);
}

void app_rgb_breath(void)
{
    /* 0～20逐渐变亮，21～39逐渐变暗。 */
    rgb_pwm_level = rgb_breath_phase <= RGB_PWM_STEPS ?
                    rgb_breath_phase : (uint8_t)(40U - rgb_breath_phase);
    rgb_breath_phase++;
    if (rgb_breath_phase >= 40U) rgb_breath_phase = 0U;
}

void TIM7_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void TIM7_IRQHandler(void)
{
    /* 沿用本工程RTT中断栈约定，禁止这里发生中断嵌套。 */
    __asm volatile ("csrci mstatus, 8" ::: "memory");
    GET_INT_SP();
    rt_interrupt_enter();

    if (TIM_GetITStatus(TIM7, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM7, TIM_IT_Update);

        if (rgb_pwm_phase < rgb_pwm_level)
        {
            /* 本板RGB低电平点亮。 */
            GPIO_ResetBits(RGB_GPIO, RGB_ALL_PINS);
        }
        else
        {
            /* 高电平熄灭。 */
            GPIO_SetBits(RGB_GPIO, RGB_ALL_PINS);
        }

        rgb_pwm_phase++;

        if (rgb_pwm_phase >= RGB_PWM_STEPS)
        {
            rgb_pwm_phase = 0U;
        }
    }

    rt_interrupt_leave();
    FREE_INT_SP();
}

int app_rgb_pwm_init(void)
{
    RCC_ClocksTypeDef clocks;
    TIM_TimeBaseInitTypeDef timer = {0};
    NVIC_InitTypeDef nvic = {0};
    uint32_t timer_clock;

    /* 获取当前时钟配置。 */
    RCC_GetClocksFreq(&clocks);

    timer_clock = clocks.PCLK1_Frequency;

    /* APB1有分频时，定时器时钟为PCLK1的2倍。 */
    if (clocks.PCLK1_Frequency != clocks.HCLK_Frequency)
    {
        timer_clock *= 2U;
    }

    /* 本例要求可以准确分频到1MHz。 */
    if ((timer_clock < 1000000UL) || ((timer_clock % 1000000UL) != 0U))
    {
        return -1;
    }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);

    /* 配置期间停止TIM7中断，并复位定时器。 */
    NVIC_DisableIRQ(TIM7_IRQn);
    TIM_DeInit(TIM7);

    /* 复用原来的GPIO初始化，三个灯初始熄灭。 */
    app_rgb_init();

    rgb_pwm_level = 0U;
    rgb_pwm_phase = 0U;
    rgb_breath_phase = 0U;

    /* 计数频率1MHz，即每1μs计数一次。 */
    timer.TIM_Prescaler = (uint16_t)(timer_clock / 1000000UL - 1U);

    /* 计数0～249，每250μs产生一次更新事件。 */
    timer.TIM_Period = 249U;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;

    TIM_TimeBaseInit(TIM7, &timer);

    /* 初始化可能产生更新标志，先清除再允许中断。 */
    TIM_ClearITPendingBit(TIM7, TIM_IT_Update);

    nvic.NVIC_IRQChannel = TIM7_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 3U;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;

    NVIC_Init(&nvic);

    TIM_ITConfig(TIM7, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM7, ENABLE);

    return 0;
}

void app_rgb_pwm_stop(void)
{
    NVIC_DisableIRQ(TIM7_IRQn);

    TIM_ITConfig(TIM7, TIM_IT_Update, DISABLE);
    TIM_Cmd(TIM7, DISABLE);
    TIM_ClearITPendingBit(TIM7, TIM_IT_Update);

    rgb_pwm_level = 0U;
    rgb_pwm_phase = 0U;
    rgb_breath_phase = 0U;

    GPIO_SetBits(RGB_GPIO, RGB_ALL_PINS);
}
