/******************************************************************************
 * 文件名   : led.c
 * 芯片     : CH32V307VCT6
 * 说明     : 板载 RGB LED 驱动（GPIO 开关 + TIM10 PWM 调光）
 ******************************************************************************/

#include "led.h"
#include "rtthread.h"

/**************************************************************************************************
 *                                        全局变量
 **************************************************************************************************/
uint8_t led_level;                  /* 当前亮度百分比 0~100 */
static uint8_t s_pwm_ready = 0;     /* PWM 是否已经初始化 */

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* 根据 LED 编号点亮/熄灭，on=1 点亮（低电平有效） */
static void led_pin_write(led_id_t id, uint8_t on)
{
    switch(id)
    {
    case LED_ALL:
        if(on)  { LED_RED_ON();  LED_GREEN_ON();  LED_BLUE_ON();  }
        else    { LED_ALL_OFF(); }
        break;

    case LED_RED:
        if(on)  { LED_RED_ON();  }  else { LED_RED_OFF();  }
        break;

    case LED_GREEN:
        if(on)  { LED_GREEN_ON(); } else { LED_GREEN_OFF(); }
        break;

    case LED_BLUE:
        if(on)  { LED_BLUE_ON();  } else { LED_BLUE_OFF();  }
        break;

    default:
        break;
    }
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  GPIO 方式初始化 LED
 */
int led_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = LED_RED_PIN | LED_GREEN_PIN | LED_BLUE_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    LED_ALL_OFF();          /* 默认全灭 */
    s_pwm_ready = 0;

    return 0;
}

/**
 * @brief  点亮指定 LED
 */
void led_on(led_id_t id)
{
    led_pin_write(id, 1);
}

/**
 * @brief  熄灭指定 LED
 */
void led_off(led_id_t id)
{
    led_pin_write(id, 0);
}

/**
 * @brief  翻转指定 LED 状态
 */
void led_toggle(led_id_t id)
{
    uint8_t is_on = 0;

    switch(id)
    {
    case LED_RED:   is_on = (GPIO_ReadOutputDataBit(LED_RED_GPIO,   LED_RED_PIN)   == Bit_RESET); break;
    case LED_GREEN: is_on = (GPIO_ReadOutputDataBit(LED_GREEN_GPIO, LED_GREEN_PIN) == Bit_RESET); break;
    case LED_BLUE:  is_on = (GPIO_ReadOutputDataBit(LED_BLUE_GPIO,  LED_BLUE_PIN)  == Bit_RESET); break;
    default:        return;
    }

    led_pin_write(id, is_on ? 0 : 1);
}

/**
 * @brief  按颜色显示
 */
void led_color_set(led_color_t color)
{
    uint8_t r = 0, g = 0, b = 0;

    switch(color)
    {
    case LED_COLOR_BLACK:   r = 0; g = 0; b = 0; break;
    case LED_COLOR_WHITE:   r = 1; g = 1; b = 1; break;
    case LED_COLOR_RED:     r = 1; g = 0; b = 0; break;
    case LED_COLOR_GREEN:   r = 0; g = 1; b = 0; break;
    case LED_COLOR_BLUE:    r = 0; g = 0; b = 1; break;
    case LED_COLOR_YELLOW:  r = 1; g = 1; b = 0; break;
    case LED_COLOR_MAGENTA: r = 1; g = 0; b = 1; break;
    case LED_COLOR_CYAN:    r = 0; g = 1; b = 1; break;
    default:                return;
    }

    if(r) { LED_RED_ON();   } else { LED_RED_OFF();   }
    if(g) { LED_GREEN_ON(); } else { LED_GREEN_OFF(); }
    if(b) { LED_BLUE_ON();  } else { LED_BLUE_OFF();  }
}

/**
 * @brief  PWM 方式初始化（亮度可调）
 */
int led_pwm_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM10, ENABLE);

    GPIO_PinRemapConfig(GPIO_PartialRemap_TIM10, ENABLE);            /* PB3/PB4/PB5 复用为 TIM10 通道 */
    GPIO_InitStructure.GPIO_Pin   = LED_RED_PIN | LED_GREEN_PIN | LED_BLUE_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    TIM_TimeBaseInitStructure.TIM_Period        = LED_PWM_PERIOD;
    TIM_TimeBaseInitStructure.TIM_Prescaler     = LED_PWM_PRESCALER;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM10, &TIM_TimeBaseInitStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_Pulse       = 0;                        /* 上电全灭 */
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_Low;        /* 低电平有效，占空比越大越亮 */
    TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Set;
    TIM_OC1Init(TIM10, &TIM_OCInitStructure);
    TIM_OC2Init(TIM10, &TIM_OCInitStructure);
    TIM_OC3Init(TIM10, &TIM_OCInitStructure);

    TIM_CtrlPWMOutputs(TIM10, ENABLE);
    TIM_OC1PreloadConfig(TIM10, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM10, TIM_OCPreload_Enable);
    TIM_OC3PreloadConfig(TIM10, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM10, ENABLE);
    TIM_Cmd(TIM10, ENABLE);

    led_level   = 0;
    s_pwm_ready = 1;

    return 0;
}

/**
 * @brief  设置亮度
 */
int led_brightness_set(led_id_t id, uint8_t percent)
{
    uint16_t duty;

    if(percent > 100)
    {
        percent = 100;
    }

    duty = (uint16_t)(((uint32_t)(LED_PWM_PERIOD + 1) * percent) / 100);

    switch(id)
    {
    case LED_ALL:   TIM_SetCompare1(TIM10, duty);
                    TIM_SetCompare2(TIM10, duty);
                    TIM_SetCompare3(TIM10, duty);
                    break;

    case LED_RED:   TIM_SetCompare1(TIM10, duty); break;
    case LED_GREEN: TIM_SetCompare2(TIM10, duty); break;
    case LED_BLUE:  TIM_SetCompare3(TIM10, duty); break;

    default:
        return -1;
    }

    led_level = percent;

    return 0;
}

/**
 * @brief  按颜色闪烁
 */
void led_blink(led_color_t color, uint8_t times, uint16_t on_ms, uint16_t off_ms)
{
    for(uint8_t i = 0; i < times; i++)
    {
        led_color_set(color);
        rt_thread_mdelay(on_ms);
        led_color_set(LED_COLOR_BLACK);
        rt_thread_mdelay(off_ms);
    }
}
