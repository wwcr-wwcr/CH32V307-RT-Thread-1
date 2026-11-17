/******************************************************************************
 * 文件名   : cs100.c
 * 芯片     : CH32V307VCT6
 * 说明     : CS100 超声波测距驱动
 ******************************************************************************/

#include "cs100.h"
#include "rtthread.h"
#include "rthw.h"

/**************************************************************************************************
 *                                        全局变量
 **************************************************************************************************/
volatile uint32_t timerOverflowNum     = 0;     /* 捕获期间定时器溢出次数 */
volatile uint16_t timerCaptureValue    = 0;     /* 捕获到的计数值 */
volatile uint8_t  timerCaptureStartFlag = 0;    /* 已捕获到上升沿 */
volatile uint8_t  timerCaptureOverFlag  = 1;    /* 捕获完成标志，1 = 空闲 */

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* 发送 10us 触发脉冲 */
static void cs100_send_trigger(void)
{
    GPIO_SetBits(CS100_TRIG_GPIO, CS100_TRIG_PIN);
    rt_hw_us_delay(15);
    GPIO_ResetBits(CS100_TRIG_GPIO, CS100_TRIG_PIN);
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化超声波模块
 */
int cs100_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_ICInitTypeDef TIM_ICInitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_TIM10, ENABLE);

    /* Trig：推挽输出，默认低 */
    GPIO_InitStructure.GPIO_Pin   = CS100_TRIG_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(CS100_TRIG_GPIO, &GPIO_InitStructure);
    GPIO_ResetBits(CS100_TRIG_GPIO, CS100_TRIG_PIN);

    /* Echo：浮空输入 */
    GPIO_InitStructure.GPIO_Pin  = CS100_ECHO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(CS100_ECHO_GPIO, &GPIO_InitStructure);

    /* 时基：1MHz，满量程 65535us */
    TIM_TimeBaseInitStructure.TIM_Period        = CS100_TIM_PERIOD;
    TIM_TimeBaseInitStructure.TIM_Prescaler     = CS100_TIM_PRESCALER;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM10, &TIM_TimeBaseInitStructure);

    /* 通道 4 输入捕获，先抓上升沿 */
    TIM_ICInitStructure.TIM_Channel     = TIM_Channel_4;
    TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    TIM_ICInitStructure.TIM_ICFilter    = 0x00;
    TIM_ICInitStructure.TIM_ICPolarity  = TIM_ICPolarity_Rising;
    TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI;
    TIM_ICInit(TIM10, &TIM_ICInitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = TIM10_UP_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 4;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = TIM10_CC_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_Init(&NVIC_InitStructure);

    TIM_ClearFlag(TIM10, TIM_FLAG_Update);
    TIM_ITConfig(TIM10, TIM_IT_Update | TIM_IT_CC4, ENABLE);
    TIM_Cmd(TIM10, DISABLE);

    timerCaptureOverFlag = 1;

    return 0;
}

/**
 * @brief  测一次距离（带超时）
 */
float cs100_measure_timeout(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    uint32_t width_us;

    /* 复位捕获状态 */
    timerCaptureOverFlag  = 0;
    timerCaptureStartFlag = 0;
    timerOverflowNum      = 0;
    timerCaptureValue     = 0;

    TIM_SetCounter(TIM10, 0);
    TIM_Cmd(TIM10, ENABLE);

    cs100_send_trigger();

    while(timerCaptureOverFlag == 0)
    {
        if(elapsed >= timeout_ms)
        {
            TIM_Cmd(TIM10, DISABLE);
            timerCaptureOverFlag = 1;
            return -1.0f;                   /* 没有回波 */
        }
        rt_thread_mdelay(1);
        elapsed++;
    }

    width_us = cs100_capture_time_calculate(timerCaptureValue, timerOverflowNum);

    return cs100_distance_calculate(width_us);
}

/**
 * @brief  测一次距离（默认超时）
 */
float cs100_measure(void)
{
    return cs100_measure_timeout(CS100_DEFAULT_TIMEOUT);
}

/**
 * @brief  测一次距离，结果取整
 */
int cs100_read_cm(uint16_t *cm)
{
    float d;

    if(cm == NULL)
    {
        return -2;
    }

    d = cs100_measure();
    if(d < 0)
    {
        return -1;
    }

    *cm = (uint16_t)(d + 0.5f);

    return 0;
}

/**
 * @brief  捕获值换算高电平时间
 */
uint32_t cs100_capture_time_calculate(uint16_t count, uint32_t overflow)
{
    return (uint32_t)count + (uint32_t)(CS100_TIM_PERIOD + 1) * overflow;
}

/**
 * @brief  时间换算距离
 */
float cs100_distance_calculate(uint32_t time_us)
{
    /* 声速 340m/s = 0.034cm/us，往返除以 2 -> 0.017 * t */
    return (float)time_us * 0.017f;
}

/**************************************************************************************************
 *                                        中断服务
 **************************************************************************************************/
/* 定时器溢出中断：统计回波期间溢出了几次 */
void TIM10_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM10_UP_IRQHandler(void)
{
    GET_INT_SP();
    rt_interrupt_enter();

    if(TIM_GetITStatus(TIM10, TIM_IT_Update) != RESET)
    {
        if((timerCaptureOverFlag == 0) && (timerCaptureStartFlag == 1))
        {
            timerOverflowNum++;
        }
        TIM_ClearITPendingBit(TIM10, TIM_IT_Update);
    }

    rt_interrupt_leave();
    FREE_INT_SP();
}

/* 捕获中断：上升沿开始计时，下降沿记录宽度 */
void TIM10_CC_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM10_CC_IRQHandler(void)
{
    GET_INT_SP();
    rt_interrupt_enter();

    if((TIM_GetITStatus(TIM10, TIM_IT_CC4) != RESET) && (timerCaptureOverFlag == 0))
    {
        if(timerCaptureStartFlag == 1)              /* 下降沿：一个完整回波结束 */
        {
            timerCaptureValue = TIM_GetCapture4(TIM10);
            TIM_Cmd(TIM10, DISABLE);
            TIM_OC4PolarityConfig(TIM10, TIM_ICPolarity_Rising);
            timerCaptureOverFlag = 1;
        }
        else                                        /* 上升沿：回波开始 */
        {
            TIM_SetCounter(TIM10, 0);
            timerOverflowNum      = 0;
            timerCaptureStartFlag = 1;
            TIM_OC4PolarityConfig(TIM10, TIM_ICPolarity_Falling);
        }
        TIM_ClearITPendingBit(TIM10, TIM_IT_CC4);
    }

    rt_interrupt_leave();
    FREE_INT_SP();
}

/**
 * @brief  直接测距离（整数厘米）
 */
uint16_t cs100_distance_cm(void)
{
    uint16_t cm;

    if(cs100_read_cm(&cm) != 0)
    {
        return 0xFFFF;
    }

    return cm;
}
