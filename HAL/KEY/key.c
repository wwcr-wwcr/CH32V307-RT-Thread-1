/******************************************************************************
 * 文件名   : key.c
 * 芯片     : CH32V307VCT6
 * 说明     : 4 个板载按键驱动（EXTI 中断 + 消抖线程）
 *
 * 数据流：
 *     EXTI 中断（双边沿） → 释放信号量 → key 线程 → 15ms 消抖 → 产生按下事件
 *
 * 为什么中断里只放一个信号量：
 *     中断里读 GPIO 容易把抖动采样进来，而且拖长中断时间。
 *     让线程在 15ms 后统一读一次稳定电平，中断本身只有几条指令。
 ******************************************************************************/

#include "key.h"
#include "rtthread.h"
#include "rtdevice.h"

/**************************************************************************************************
 *                                        配置
 **************************************************************************************************/
#define KEY_THREAD_STACK        1536
#define KEY_THREAD_PRIORITY     8           /* 比 UI 线程高，保证按键响应及时 */
#define KEY_THREAD_TICK         5
#define KEY_DEBOUNCE_MS         15          /* 消抖窗口 */
#define KEY_SYNC_MS             100         /* 没有中断时的兜底同步周期 */

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
static struct rt_semaphore s_key_sem;
static struct rt_thread    s_key_thread;
static rt_uint8_t          s_key_stack[KEY_THREAD_STACK];

static key_event_cb_t s_key_cb  = RT_NULL;
static uint8_t        s_stable  = 0;        /* 上一次确认过的稳定电平 */
static uint8_t        s_inited  = 0;

/* 按键编号 -> RT-Thread 引脚号 */
static const rt_base_t s_rt_pin[KEY_ID_MAX] =
{
    KEY_RT_PIN_1, KEY_RT_PIN_2, KEY_RT_PIN_3, KEY_RT_PIN_4
};

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* EXTI 回调（中断上下文），只做一件事：唤醒按键线程 */
static void key_isr(void *args)
{
    (void)args;

    rt_sem_release(&s_key_sem);
}

/* drv_gpio 在开中断时会把引脚改成浮空输入，这里恢复成内部上拉，避免悬空误触发 */
static void key_restore_pullup(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.GPIO_Pin   = KEY_PIN_ALL;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(KEY_GPIO, &GPIO_InitStructure);
}

/* 按键线程：被中断唤醒 -> 消抖 -> 和稳定值比较 -> 回调 */
static void key_entry(void *parameter)
{
    (void)parameter;

    for(;;)
    {
        /* 有中断就等信号量，没中断则周期性兜底扫描一次（防止漏掉释放） */
        if(rt_sem_take(&s_key_sem, rt_tick_from_millisecond(KEY_SYNC_MS)) == RT_EOK)
        {
            /* 抖动会连续触发中断，先清计数，避免醒来好几次 */
            rt_sem_control(&s_key_sem, RT_IPC_CMD_RESET, RT_NULL);
            rt_thread_mdelay(KEY_DEBOUNCE_MS);
            rt_sem_control(&s_key_sem, RT_IPC_CMD_RESET, RT_NULL);
        }

        {
            uint8_t now     = key_state_get();
            uint8_t pressed = (uint8_t)(now & (~s_stable));     /* 只有新按下的才算事件 */

            s_stable = now;

            if(pressed != 0 && s_key_cb != RT_NULL)
            {
                for(uint8_t i = 0; i < KEY_ID_MAX; i++)
                {
                    if(pressed & KEY_BIT(i))
                    {
                        s_key_cb((key_id_t)i);
                    }
                }
            }
        }
    }
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化按键
 */
int key_init(void)
{
    for(uint8_t i = 0; i < KEY_ID_MAX; i++)
    {
        rt_pin_mode(s_rt_pin[i], PIN_MODE_INPUT_PULLUP);
        rt_pin_attach_irq(s_rt_pin[i], PIN_IRQ_MODE_RISING_FALLING, key_isr, RT_NULL);
        rt_pin_irq_enable(s_rt_pin[i], PIN_IRQ_ENABLE);
    }

    /* 恢复内部上拉（drv_gpio 的双边沿模式会配置成浮空输入） */
    key_restore_pullup();

    rt_sem_init(&s_key_sem, "key", 0, RT_IPC_FLAG_FIFO);

    s_stable = key_state_get();                 /* 用当前电平做初值，避免上电误报 */

    if(rt_thread_init(&s_key_thread, "key", key_entry, RT_NULL,
                      s_key_stack, sizeof(s_key_stack),
                      KEY_THREAD_PRIORITY, KEY_THREAD_TICK) != RT_EOK)
    {
        return -1;
    }

    s_inited = 1;

    return 0;
}

/**
 * @brief  启动按键线程
 */
int key_start(void)
{
    if(!s_inited)
    {
        return -1;
    }

    return (rt_thread_startup(&s_key_thread) == RT_EOK) ? 0 : -1;
}

/**
 * @brief  注册按键回调
 */
void key_event_register(key_event_cb_t cb)
{
    s_key_cb = cb;
}

/**
 * @brief  读取当前按键电平
 */
uint8_t key_state_get(void)
{
    uint8_t keys = 0;

    if(KEY_PUSH_BUTTON_1()) { keys |= KEY_BUTTON_1; }
    if(KEY_PUSH_BUTTON_2()) { keys |= KEY_BUTTON_2; }
    if(KEY_PUSH_BUTTON_3()) { keys |= KEY_BUTTON_3; }
    if(KEY_PUSH_BUTTON_4()) { keys |= KEY_BUTTON_4; }

    return keys;
}

/**
 * @brief  某个按键是否按下（已消抖）
 */
uint8_t key_is_pressed(key_id_t id)
{
    if(id >= KEY_ID_NONE)
    {
        return 0;
    }

    return (s_stable & KEY_BIT(id)) ? 1 : 0;
}
