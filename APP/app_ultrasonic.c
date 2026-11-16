#include "app_ultrasonic.h"
#include "ch32v30x.h"
#include <rtthread.h>

#define ULTRASONIC_GPIO GPIOC
#define ULTRASONIC_TRIG_PIN GPIO_Pin_10
#define ULTRASONIC_ECHO_PIN GPIO_Pin_11

static struct rt_mutex ultrasonic_mutex;
static uint8_t mutex_ready;
static uint8_t ultrasonic_ready;
static uint8_t measured_before;
static rt_tick_t last_trigger_tick;

static void ultrasonic_lock_init(void)
{
    rt_enter_critical();
    if (mutex_ready == 0U)
    {
        rt_mutex_init(&ultrasonic_mutex, "ultrasonic", RT_IPC_FLAG_PRIO);
        mutex_ready = 1U;
    }
    rt_exit_critical();
}

/* 结束本轮测量，无论成功还是失败都关闭捕获，并保证TRIG回到低电平。 */
static void ultrasonic_stop(void)
{
    GPIO_ResetBits(ULTRASONIC_GPIO, ULTRASONIC_TRIG_PIN);
    TIM_CCxCmd(TIM10, TIM_Channel_3, TIM_CCx_Disable);
    TIM_CCxCmd(TIM10, TIM_Channel_4, TIM_CCx_Disable);
    TIM_Cmd(TIM10, DISABLE);
}

int app_ultrasonic_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_TimeBaseInitTypeDef timer = {0};
    TIM_ICInitTypeDef capture = {0};
    RCC_ClocksTypeDef clocks;
    uint32_t timer_hz;

    ultrasonic_lock_init();
    if (rt_mutex_take(&ultrasonic_mutex, RT_WAITING_FOREVER) != RT_EOK) return APP_ULTRASONIC_ESTATE;
    ultrasonic_ready = 0U;

    /* TIM10位于APB2，APB2分频时定时器时钟乘2。 */
    RCC_GetClocksFreq(&clocks);
    timer_hz = clocks.PCLK2_Frequency;
    if (clocks.PCLK2_Frequency != clocks.HCLK_Frequency) timer_hz *= 2U;
    if (timer_hz < 1000000U || timer_hz % 1000000U != 0U || timer_hz / 1000000U > 65536U)
    {
        rt_mutex_release(&ultrasonic_mutex);
        return APP_ULTRASONIC_ECLOCK;
    }

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO | RCC_APB2Periph_TIM10, ENABLE);
    /* PC11是TIM10_CH4默认映射，不把通道改到LCD等其他引脚。 */
    GPIO_PinRemapConfig(GPIO_PartialRemap_TIM10, DISABLE);
    GPIO_PinRemapConfig(GPIO_FullRemap_TIM10, DISABLE);
    GPIO_ResetBits(ULTRASONIC_GPIO, ULTRASONIC_TRIG_PIN);
    gpio.GPIO_Pin = ULTRASONIC_TRIG_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(ULTRASONIC_GPIO, &gpio);
    gpio.GPIO_Pin = ULTRASONIC_ECHO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(ULTRASONIC_GPIO, &gpio);

    TIM_DeInit(TIM10);
    timer.TIM_Prescaler = timer_hz / 1000000U - 1U;
    timer.TIM_Period = 65535U;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM10, &timer);

    /* CH4直接捕获TI4上升沿。输入滤波3用于过滤很窄的毛刺，不是距离平均滤波。 */
    capture.TIM_Channel = TIM_Channel_4;
    capture.TIM_ICPolarity = TIM_ICPolarity_Rising;
    capture.TIM_ICSelection = TIM_ICSelection_DirectTI;
    capture.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    capture.TIM_ICFilter = 3;
    TIM_ICInit(TIM10, &capture);

    /* CH3通过间接输入也选择TI4，只捕获下降沿；不需要连接PC3。 */
    capture.TIM_Channel = TIM_Channel_3;
    capture.TIM_ICPolarity = TIM_ICPolarity_Falling;
    capture.TIM_ICSelection = TIM_ICSelection_IndirectTI;
    TIM_ICInit(TIM10, &capture);

    ultrasonic_stop();
    TIM_ClearFlag(TIM10, TIM_FLAG_Update | TIM_FLAG_CC3 | TIM_FLAG_CC4 | TIM_FLAG_CC3OF | TIM_FLAG_CC4OF);
    measured_before = 0U;
    ultrasonic_ready = 1U;
    rt_mutex_release(&ultrasonic_mutex);
    return APP_ULTRASONIC_OK;
}

/* 不使用会改写SysTick的裸机Delay_Us；计时器不走时也能超时退出。
 * 线程抢占可能延长TRIG高电平，因此这里提供至少50us的触发，不承诺精密脉宽输出。
 */
static int ultrasonic_trigger(void)
{
    uint16_t start;
    rt_tick_t timeout_start;

    GPIO_SetBits(ULTRASONIC_GPIO, ULTRASONIC_TRIG_PIN);
    start = TIM_GetCounter(TIM10);
    timeout_start = rt_tick_get();
    while ((uint16_t)(TIM_GetCounter(TIM10) - start) < 50U)
    {
        if ((rt_tick_t)(rt_tick_get() - timeout_start) >= rt_tick_from_millisecond(2))
        {
            GPIO_ResetBits(ULTRASONIC_GPIO, ULTRASONIC_TRIG_PIN);
            return APP_ULTRASONIC_ETIMEOUT;
        }
    }
    GPIO_ResetBits(ULTRASONIC_GPIO, ULTRASONIC_TRIG_PIN);
    return APP_ULTRASONIC_OK;
}

int app_ultrasonic_read_us(uint32_t *pulse_us)
{
    uint16_t rising, falling, width;
    rt_tick_t start;
    int result;

    if (pulse_us == RT_NULL) return APP_ULTRASONIC_EINVAL;
    if (ultrasonic_ready == 0U) return APP_ULTRASONIC_ESTATE;
    if (rt_mutex_take(&ultrasonic_mutex, RT_WAITING_FOREVER) != RT_EOK) return APP_ULTRASONIC_ESTATE;
    if (ultrasonic_ready == 0U) { rt_mutex_release(&ultrasonic_mutex); return APP_ULTRASONIC_ESTATE; }

    while (measured_before && (rt_tick_t)(rt_tick_get() - last_trigger_tick) < rt_tick_from_millisecond(APP_ULTRASONIC_INTERVAL_MS))
    {
        rt_thread_mdelay(1);
    }
    if (GPIO_ReadInputDataBit(ULTRASONIC_GPIO, ULTRASONIC_ECHO_PIN) != RESET)
    {
        rt_mutex_release(&ultrasonic_mutex);
        return APP_ULTRASONIC_ECAPTURE;
    }

    ultrasonic_stop();
    TIM_SetCounter(TIM10, 0);
    TIM_ClearFlag(TIM10, TIM_FLAG_Update | TIM_FLAG_CC3 | TIM_FLAG_CC4 | TIM_FLAG_CC3OF | TIM_FLAG_CC4OF);
    TIM_CCxCmd(TIM10, TIM_Channel_3, TIM_CCx_Enable);
    TIM_CCxCmd(TIM10, TIM_Channel_4, TIM_CCx_Enable);
    TIM_Cmd(TIM10, ENABLE);

    last_trigger_tick = rt_tick_get();
    measured_before = 1U;
    result = ultrasonic_trigger();
    start = last_trigger_tick;
    while (result == APP_ULTRASONIC_OK)
    {
        if (TIM_GetFlagStatus(TIM10, TIM_FLAG_CC3OF | TIM_FLAG_CC4OF) != RESET)
        {
            result = APP_ULTRASONIC_ECAPTURE;
            break;
        }
        if (TIM_GetFlagStatus(TIM10, TIM_FLAG_CC4) != RESET && TIM_GetFlagStatus(TIM10, TIM_FLAG_CC3) != RESET) break;
        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(APP_ULTRASONIC_TIMEOUT_MS))
        {
            result = APP_ULTRASONIC_ETIMEOUT;
            break;
        }
        rt_thread_mdelay(1);
    }

    /* 先冻结硬件，防止读取两个时间戳期间有新边沿覆盖寄存器。 */
    ultrasonic_stop();
    if (result == APP_ULTRASONIC_OK)
    {
        if (TIM_GetFlagStatus(TIM10, TIM_FLAG_CC3OF | TIM_FLAG_CC4OF) != RESET) result = APP_ULTRASONIC_ECAPTURE;
        else
        {
            rising = TIM_GetCapture4(TIM10);
            falling = TIM_GetCapture3(TIM10);
            /* 16位无符号减法也能处理一次计数回绕。 */
            width = (uint16_t)(falling - rising);
            if (width < APP_ULTRASONIC_MIN_US || width >= APP_ULTRASONIC_MAX_US) result = APP_ULTRASONIC_ERANGE;
            else *pulse_us = width;
        }
    }

    rt_mutex_release(&ultrasonic_mutex);
    return result;
}

int32_t app_ultrasonic_get_distance_mm(void)
{
    uint32_t pulse_us;
    int result = app_ultrasonic_read_us(&pulse_us);
    if (result != APP_ULTRASONIC_OK) return result;
    /* 声音往返，距离mm=微秒*340/2000；加1000再除是四舍五入。 */
    return (int32_t)((pulse_us * 340U + 1000U) / 2000U);
}
