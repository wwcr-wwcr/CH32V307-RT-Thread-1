#include "app_key.h"
#include "ch32v30x.h"
#include <rtthread.h>

#define KEY_GPIO   GPIOE
#define KEY_COUNT  4U

#if (APP_KEY_SCAN_MS == 0U) || (APP_KEY_DEBOUNCE_MS < APP_KEY_SCAN_MS) || (APP_KEY_DOUBLE_MS <= APP_KEY_DEBOUNCE_MS) || (APP_KEY_LONG_MS <= APP_KEY_DEBOUNCE_MS)
#error "Invalid key timing configuration"
#endif

static const uint16_t key_pins[KEY_COUNT] = {GPIO_Pin_2, GPIO_Pin_3, GPIO_Pin_4, GPIO_Pin_5};

/* 每个键独立保存状态，因此同时按键不会互相重置消抖时间。 */
typedef struct
{
    uint8_t last_raw;          /* 最新观察到的原始状态：1按下、0松开。 */
    uint8_t stable;            /* 消抖确认后的状态。 */
    uint8_t armed;             /* 开机已松开，允许识别后续操作。 */
    uint8_t click_waiting;     /* 第一次短按已松开，正在等待第二次按下。 */
    uint8_t second_press;      /* 当前按住的是双击候选的第二次按下。 */
    uint8_t long_reported;     /* 当前这次按住已经报告过长按。 */
    rt_tick_t changed_tick;    /* 原始状态最近改变时刻，用于消抖。 */
    rt_tick_t pressed_tick;    /* 当前这次按下确认时刻，用于长按。 */
    rt_tick_t released_tick;   /* 第一次短按松开确认时刻，用于双击。 */
} app_key_filter_t;

static app_key_filter_t key_filters[KEY_COUNT];
static uint8_t key_initialized;

/* 把低电平按下转换为统一的软件状态：1按下，0松开。 */
static uint8_t app_key_read_raw(uint16_t pin)
{
    return (GPIO_ReadInputDataBit(KEY_GPIO, pin) == Bit_RESET) ? 1U : 0U;
}

void app_key_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    rt_tick_t now = rt_tick_get();
    uint8_t i;
    uint8_t raw;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(KEY_GPIO, &gpio);

    for (i = 0; i < KEY_COUNT; i++)
    {
        raw = app_key_read_raw(key_pins[i]);
        key_filters[i] = (app_key_filter_t){0};
        key_filters[i].last_raw = raw;
        key_filters[i].stable = raw;
        key_filters[i].armed = (raw == 0U) ? 1U : 0U;
        key_filters[i].changed_tick = now;
    }
    key_initialized = 1U;
}

/*
 * 处理一个键；返回0、单击、双击或长按事件。
 * filter传该键的状态地址；mask传APP_KEY1～APP_KEY4；now为本轮tick。
 * 先确认电平变化，再判断长按和双击等待超时。
 */
static uint16_t app_key_update(app_key_filter_t *filter, uint16_t pin, uint8_t mask, rt_tick_t now)
{
    uint8_t raw = app_key_read_raw(pin);
    uint16_t events = APP_KEY_NONE;
    rt_tick_t debounce_ticks = rt_tick_from_millisecond(APP_KEY_DEBOUNCE_MS);
    rt_tick_t double_ticks = rt_tick_from_millisecond(APP_KEY_DOUBLE_MS);
    rt_tick_t long_ticks = rt_tick_from_millisecond(APP_KEY_LONG_MS);

    /* 原始电平每变化一次，都重新计算消抖时间。 */
    if (raw != filter->last_raw)
    {
        filter->last_raw = raw;
        filter->changed_tick = now;
    }

    if ((raw != filter->stable) && ((rt_tick_t)(now - filter->changed_tick) >= debounce_ticks))
    {
        filter->stable = raw;

        if (raw != 0U)
        {
            /* 确认按下。如果前一次单击已经超时，先报告那次单击。 */
            if (filter->click_waiting && ((rt_tick_t)(now - filter->released_tick) > double_ticks))
            {
                events |= APP_KEY_CLICK(mask);
                filter->click_waiting = 0U;
            }
            filter->second_press = filter->click_waiting;
            filter->click_waiting = 0U;
            filter->pressed_tick = now;
            filter->long_reported = 0U;
        }
        else if (filter->armed == 0U)
        {
            /* 初始化时已经按住：本次松开只解除屏蔽。 */
            filter->armed = 1U;
        }
        else
        {
            /* 确认松开，使用释放候选开始时刻估计按住时长。
             * 避免把松开消抖的20ms算进长按，使临界短按误变长按。
             */
            if (filter->long_reported == 0U)
            {
                if ((rt_tick_t)(filter->changed_tick - filter->pressed_tick) >= long_ticks)
                {
                    events |= APP_KEY_LONG_PRESS(mask);
                }
                else if (filter->second_press != 0U)
                {
                    /* 第二次短按松开，双击成立，不再报告单击。 */
                    events |= APP_KEY_DOUBLE_CLICK(mask);
                }
                else
                {
                    /* 第一次短按松开，等待双击窗口结束。 */
                    filter->click_waiting = 1U;
                    filter->released_tick = now;
                }
            }
            filter->second_press = 0U;
        }
    }

    /* 持续按住达到阈值，立即报告一次长按，不等待松手。 */
    if (filter->armed && filter->stable && raw && !filter->long_reported && ((rt_tick_t)(now - filter->pressed_tick) >= long_ticks))
    {
        events |= APP_KEY_LONG_PRESS(mask);
        filter->long_reported = 1U;
        filter->second_press = 0U;
    }

    /* 第一击松开后，没有在窗口内确认第二次按下，单击成立。 */
    if (filter->click_waiting && ((rt_tick_t)(now - filter->released_tick) >= double_ticks))
    {
        events |= APP_KEY_CLICK(mask);
        filter->click_waiting = 0U;
    }

    return events;
}

uint16_t app_key_scan(void)
{
    uint8_t i;
    uint16_t events = APP_KEY_NONE;
    rt_tick_t now;

    if (key_initialized == 0U)
    {
        return APP_KEY_NONE;
    }

    now = rt_tick_get();
    for (i = 0; i < KEY_COUNT; i++)
    {
        events |= app_key_update(&key_filters[i], key_pins[i], (uint8_t)(1U << i), now);
    }
    return events;
}
