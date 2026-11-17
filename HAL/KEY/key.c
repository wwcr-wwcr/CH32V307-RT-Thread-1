/******************************************************************************
 * 文件名   : key.c
 * 芯片     : CH32V307VCT6
 * 说明     : 4 个板载按键驱动（EXTI 中断 + 消抖线程 + 长按/双击状态机）
 *
 * 数据流：
 *     EXTI 中断（双边沿） → 释放信号量 → key 线程 → 每 10ms 采样一次
 *                                                          ↓
 *                                              每个键一个状态机
 *                                                          ↓
 *                                    短按 / 长按 / 双击 三种事件
 *
 * 为什么中断里只放一个信号量：
 *     中断里读 GPIO 容易把抖动采样进来，而且拖长中断时间。
 *     让线程在抖动过去后统一读稳定电平，中断本身只有几条指令。
 *
 * 状态机（每个物理键独立一份）：
 *
 *     KS_UP ──按下──> KS_DOWN_PENDING ──稳定20ms──> KS_DOWN ──按住700ms──> KS_LONG
 *       ^                    │                         │                     │
 *       │                  抖动消失                  松开                  松开
 *       └────────────────────┘                         ↓                     ↓
 *                                                    KS_RELEASED           KS_UP
 *                                                      │      │
 *                                            220ms内再按   220ms到
 *                                                      ↓      ↓
 *                                                  KS_SECOND  上报短按 → KS_UP
 *                                                      │
 *                                                    松开
 *                                                      ↓
 *                                                  上报双击 → KS_UP
 *
 * 计时统一用 rt_tick_get() 的时间戳而不是循环次数，
 * 这样即使中断让线程提前醒来，长按/双击的判定时间也不会提前或拖后。
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

#define KEY_SCAN_MS             10          /* 采样周期，也是状态机的时间分辨率 */
#define KEY_DEBOUNCE_MS         20          /* 电平稳定这么久才算真的按下 */
#define KEY_LONG_MS             700         /* 按住超过这个时间判长按 */
#define KEY_DBL_MS              220         /* 松开后这么久内再按算双击 */

/* 毫秒换算成 tick，用整数运算避免浮点 */
#define KEY_TICKS(ms)           ((rt_tick_t)(((ms) * RT_TICK_PER_SECOND) / 1000u))

/**************************************************************************************************
 *                                        内部类型
 **************************************************************************************************/
typedef enum
{
    KS_UP = 0,          /* 松开，等按下 */
    KS_DOWN_PENDING,    /* 刚检测到按下，消抖确认中 */
    KS_DOWN,            /* 已确认按下，长按计时中 */
    KS_LONG,            /* 长按已上报，等松开 */
    KS_RELEASED,        /* 短按抬起，等双击窗口 */
    KS_SECOND           /* 双击的第二击按下中，等松开确认 */
} key_state_t;

typedef struct
{
    uint8_t   state;    /* key_state_t */
    rt_tick_t stamp;    /* 进入当前状态的时刻 */
} key_slot_t;

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
static struct rt_semaphore s_key_sem;
static struct rt_thread    s_key_thread;
static rt_uint8_t          s_key_stack[KEY_THREAD_STACK];

static key_event_cb_t s_key_cb  = RT_NULL;
static uint8_t        s_stable  = 0;        /* 最近一次采样到的电平（已消抖） */
static uint8_t        s_inited  = 0;

static key_slot_t     s_slot[KEY_PHYS_NUM];

/* 按键编号 -> RT-Thread 引脚号，长度用物理键个数而不是事件总数 */
static const rt_base_t s_rt_pin[KEY_PHYS_NUM] =
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

/* 上报一个事件，id 由「物理键 + 事件类型」拼出来 */
static void key_emit(uint8_t phys, uint8_t kind)
{
    if(s_key_cb != RT_NULL)
    {
        s_key_cb(KEY_ID_OF(phys, kind));
    }
}

/* 推进所有按键的状态机一步，每 KEY_SCAN_MS 调一次 */
static void key_scan(void)
{
    uint8_t   now = key_state_get();        /* bit i = 1 表示第 i 个键按下 */
    rt_tick_t t   = rt_tick_get();
    uint8_t   i;

    s_stable = now;

    for(i = 0; i < KEY_PHYS_NUM; i++)
    {
        key_slot_t *k  = &s_slot[i];
        uint8_t     down = (uint8_t)((now >> i) & 0x01u);
        rt_tick_t   held = (rt_tick_t)(t - k->stamp);   /* 当前状态已经持续多久 */

        switch(k->state)
        {
            case KS_UP:
                if(down)
                {
                    k->state = KS_DOWN_PENDING;
                    k->stamp = t;
                }
                break;

            case KS_DOWN_PENDING:
                if(!down)
                {
                    k->state = KS_UP;               /* 抖动，当作没按 */
                }
                else if(held >= KEY_TICKS(KEY_DEBOUNCE_MS))
                {
                    k->state = KS_DOWN;             /* 稳定按下了，开始长按计时 */
                    k->stamp = t;
                }
                break;

            case KS_DOWN:
                if(!down)
                {
                    k->state = KS_RELEASED;         /* 按了但没到长按，可能是短按 */
                    k->stamp = t;
                }
                else if(held >= KEY_TICKS(KEY_LONG_MS))
                {
                    k->state = KS_LONG;
                    key_emit(i, KEY_KIND_LONG);     /* 按住就报，不等松开 */
                }
                break;

            case KS_LONG:
                if(!down)
                {
                    k->state = KS_UP;               /* 长按报过了，松开不再产生短按 */
                }
                break;

            case KS_RELEASED:
                if(down)
                {
                    k->state = KS_SECOND;           /* 双击窗口内又按下 */
                    k->stamp = t;
                }
                else if(held >= KEY_TICKS(KEY_DBL_MS))
                {
                    k->state = KS_UP;
                    key_emit(i, KEY_KIND_SHORT);    /* 窗口过了没再按，确认是短按 */
                }
                break;

            case KS_SECOND:
                if(!down)
                {
                    k->state = KS_UP;
                    key_emit(i, KEY_KIND_DBL);      /* 第二击松开，确认双击 */
                }
                break;

            default:
                k->state = KS_UP;                   /* 状态异常就复位，别卡死 */
                k->stamp = t;
                break;
        }
    }
}

/* 按键线程：被中断唤醒或 10ms 超时，统一推进一拍 */
static void key_entry(void *parameter)
{
    (void)parameter;

    for(;;)
    {
        /* 有中断就提前醒来，没有就等满一个采样周期 */
        if(rt_sem_take(&s_key_sem, rt_tick_from_millisecond(KEY_SCAN_MS)) == RT_EOK)
        {
            /* 抖动会连发几次中断，清掉计数避免下一轮反复空转 */
            rt_sem_control(&s_key_sem, RT_IPC_CMD_RESET, RT_NULL);
        }

        key_scan();
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
    uint8_t i;

    for(i = 0; i < KEY_PHYS_NUM; i++)
    {
        rt_pin_mode(s_rt_pin[i], PIN_MODE_INPUT_PULLUP);
        rt_pin_attach_irq(s_rt_pin[i], PIN_IRQ_MODE_RISING_FALLING, key_isr, RT_NULL);
        rt_pin_irq_enable(s_rt_pin[i], PIN_IRQ_ENABLE);
    }

    /* 恢复内部上拉（drv_gpio 的双边沿模式会配置成浮空输入） */
    key_restore_pullup();

    rt_sem_init(&s_key_sem, "key", 0, RT_IPC_FLAG_FIFO);

    s_stable = key_state_get();                 /* 用当前电平做初值，避免上电误报 */

    for(i = 0; i < KEY_PHYS_NUM; i++)           /* 状态机复位，上电时按住的键不算事件 */
    {
        s_slot[i].state = KS_UP;
        s_slot[i].stamp = rt_tick_get();
    }

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
 * @brief  某个物理按键是否按下（已消抖）
 */
uint8_t key_is_pressed(key_id_t id)
{
    if((uint8_t)id >= KEY_PHYS_NUM)             /* 只接受物理键编号 0~3 */
    {
        return 0;
    }

    return (s_stable & KEY_BIT(id)) ? 1 : 0;
}
