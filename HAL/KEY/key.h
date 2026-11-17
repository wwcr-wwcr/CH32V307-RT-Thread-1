/******************************************************************************
 * 文件名   : key.h
 * 芯片     : CH32V307VCT6
 * 说明     : 4 个板载按键驱动（PE2~PE5，上拉输入，按下为低）
 *
 * 事件模型：
 *     每个物理键产生三种事件，编号连成一张表：
 *
 *         物理键      短按     长按     双击
 *         K1 (PE2)     1        5        9
 *         K2 (PE3)     2        6       10
 *         K3 (PE4)     3        7       11
 *         K4 (PE5)     4        8       12
 *
 *     编号规律：id = 物理键序号(0~3) + 事件类型 * 4，见 KEY_ID_OF()。
 *
 * 时序（阈值在 key.c 顶部配置区，可按手感调）：
 *     消抖 20ms → 按住 700ms 报长按（按住过程中就报，不用等松开）
 *     短按要等松开后 220ms 内没有第二次按下才上报，所以单次短按有 220ms 延迟。
 *     这样做是为了让「短按」和「双击」互斥——否则双击会先冒出两个短按，
 *     菜单会连翻两格。嫌延迟大就把 KEY_DBL_MS 调小。
 *
 * 上层用法只有三步：
 *     key_init();                          // 初始化（中断 + 建线程，不启动）
 *     key_event_register(my_key_cb);       // 注册回调
 *     key_start();                         // 启动按键线程
 *
 * 回调在 key 线程上下文执行，里面不要做耗时操作。
 ******************************************************************************/

#ifndef __KEY_H
#define __KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 引脚定义
 */
#define KEY1_GPIO           GPIOE
#define KEY2_GPIO           GPIOE
#define KEY3_GPIO           GPIOE
#define KEY4_GPIO           GPIOE
#define KEY1_PIN            GPIO_Pin_2     /* PE2 */
#define KEY2_PIN            GPIO_Pin_3     /* PE3 */
#define KEY3_PIN            GPIO_Pin_4     /* PE4 */
#define KEY4_PIN            GPIO_Pin_5     /* PE5 */
#define KEY_GPIO            GPIOE
#define KEY_PIN_ALL         (KEY1_PIN | KEY2_PIN | KEY3_PIN | KEY4_PIN)

/* RT-Thread 引脚编号，对应 drivers/drv_gpio.c 的 pins[]（1=PE2 2=PE3 3=PE4 4=PE5） */
#define KEY_RT_PIN_1        1
#define KEY_RT_PIN_2        2
#define KEY_RT_PIN_3        3
#define KEY_RT_PIN_4        4

/*********************************************************************
 * 类型定义
 */

/** 物理按键个数。注意这是「板子上有几个键」，不是「有几种事件」 */
#define KEY_PHYS_NUM        4

/**
 * 逻辑事件编号：前 4 个短按，接 4 个长按，再 4 个双击，枚举顺序即 id 大小。
 * 数组按事件编号索引时，请用 KEY_ID_MAX 做长度、用 KEY_PHYS_NUM 做物理键循环。
 */
typedef enum
{
    /* ---- 短按 1~4 ---- */
    KEY_ID_1 = 0,       /* K1 短按 */
    KEY_ID_2,           /* K2 短按 */
    KEY_ID_3,           /* K3 短按 */
    KEY_ID_4,           /* K4 短按 */

    /* ---- 长按 5~8 ---- */
    KEY_ID_LONG_1,      /* K1 长按 */
    KEY_ID_LONG_2,      /* K2 长按 */
    KEY_ID_LONG_3,      /* K3 长按 */
    KEY_ID_LONG_4,      /* K4 长按，任务页固定用它返回菜单 */

    /* ---- 双击 9~12 ---- */
    KEY_ID_DBL_1,       /* K1 双击 */
    KEY_ID_DBL_2,       /* K2 双击 */
    KEY_ID_DBL_3,       /* K3 双击 */
    KEY_ID_DBL_4,       /* K4 双击 */

    KEY_ID_NONE,        /* 无事件，同时作为非法 id 的哨兵 */
    KEY_ID_MAX = KEY_ID_NONE
} key_id_t;

/** 事件类型，与 KEY_PHYS_NUM 一起拼出 key_id_t */
typedef enum
{
    KEY_KIND_SHORT = 0,
    KEY_KIND_LONG,
    KEY_KIND_DBL
} key_kind_t;

/** 由「物理键序号 + 事件类型」拼出事件 id */
#define KEY_ID_OF(phys, kind)   ((key_id_t)((phys) + (kind) * KEY_PHYS_NUM))

/** 从事件 id 拆出物理键序号（0~3） */
#define KEY_PHYS_OF(id)         ((uint8_t)((id) % KEY_PHYS_NUM))

/** 从事件 id 拆出事件类型（KEY_KIND_xxx） */
#define KEY_KIND_OF(id)         ((uint8_t)((id) / KEY_PHYS_NUM))

/** 是否为某物理键的短按事件 */
#define KEY_IS_SHORT(id)        ((id) < KEY_ID_LONG_1)

/* 位掩码，只用于物理键电平；一个中断可同时触发多个键，所以 id 只取 0~3 */
#define KEY_BIT(id)         ((uint8_t)(1 << (id)))
#define KEY_BUTTON_1        KEY_BIT(KEY_ID_1)
#define KEY_BUTTON_2        KEY_BIT(KEY_ID_2)
#define KEY_BUTTON_3        KEY_BIT(KEY_ID_3)
#define KEY_BUTTON_4        KEY_BIT(KEY_ID_4)
#define KEY_BUTTON_ALL      (KEY_BUTTON_1 | KEY_BUTTON_2 | KEY_BUTTON_3 | KEY_BUTTON_4)

/* 原始电平判断，未消抖 */
#define KEY_PUSH_BUTTON_1() (GPIO_ReadInputDataBit(KEY1_GPIO, KEY1_PIN) == Bit_RESET)
#define KEY_PUSH_BUTTON_2() (GPIO_ReadInputDataBit(KEY2_GPIO, KEY2_PIN) == Bit_RESET)
#define KEY_PUSH_BUTTON_3() (GPIO_ReadInputDataBit(KEY3_GPIO, KEY3_PIN) == Bit_RESET)
#define KEY_PUSH_BUTTON_4() (GPIO_ReadInputDataBit(KEY4_GPIO, KEY4_PIN) == Bit_RESET)

/**
 * @brief 按键事件回调，在 key 线程里被调用
 * @param key 事件编号：短按 1~4 / 长按 5~8 / 双击 9~12
 */
typedef void (*key_event_cb_t)(key_id_t key);

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化按键：配引脚 + 挂双边沿 EXTI + 建信号量 + 建线程
 * @return 0 成功，-1 失败
 * @note   只创建线程不启动，注册好回调再调 key_start()
 */
int key_init(void);

/**
 * @brief  启动按键线程
 * @return 0 成功，-1 未初始化
 */
int key_start(void);

/**
 * @brief  注册按键事件回调（传 NULL 取消）
 */
void key_event_register(key_event_cb_t cb);

/**
 * @brief  读取当前按键电平（未消抖，位掩码）
 * @return KEY_BUTTON_x 的或
 */
uint8_t key_state_get(void);

/**
 * @brief  某个物理按键当前是否按下（已消抖）
 * @param  id 物理键编号 0~3；传长按/双击 id 一律返回 0
 */
uint8_t key_is_pressed(key_id_t id);

#ifdef __cplusplus
}
#endif

#endif
