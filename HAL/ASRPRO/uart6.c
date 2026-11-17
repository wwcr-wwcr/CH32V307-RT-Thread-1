/******************************************************************************
 * 文件名   : uart6.c
 * 芯片     : CH32V307VCT6
 * 说明     : UART6 驱动（中断接收 + 环形缓冲）
 ******************************************************************************/

#include "uart6.h"
#include "rtthread.h"
#include "rtconfig.h"
#include "rthw.h"
#include "stdio.h"
#include "stdarg.h"
#include "string.h"

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化 UART6
 */
int uart6_init(uint32_t baud)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef  NVIC_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART6, ENABLE);

    /* TX: PC0 复用推挽 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* RX: PC1 浮空输入 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = baud;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(UART6, &USART_InitStructure);

    USART_ITConfig(UART6, USART_IT_RXNE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = UART6_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 4;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(UART6, ENABLE);

    return 0;
}

/**
 * @brief  发送数据
 */
int uart6_send(const void *buf, uint16_t len)
{
    const uint8_t *p = (const uint8_t *)buf;

    if(buf == NULL || len == 0)
    {
        return -1;
    }

    for(uint16_t i = 0; i < len; i++)
    {
        while(USART_GetFlagStatus(UART6, USART_FLAG_TXE) == RESET)
        {
            /* 等待发送寄存器空 */
        }
        USART_SendData(UART6, p[i]);
    }

    while(USART_GetFlagStatus(UART6, USART_FLAG_TC) == RESET)
    {
        /* 等待发送完成 */
    }

    return 0;
}

/**
 * @brief  发送一个字节
 */
int uart6_putc(uint8_t ch)
{
    return uart6_send(&ch, 1);
}

/**
 * @brief  可读字节数
 */
uint16_t uart6_available(void)
{
    if(p_uart6ReceiveRingInfo == NULL)
    {
        return 0;
    }

    return p_uart6ReceiveRingInfo->dat_len;
}

/**
 * @brief  读取指定长度
 */
int uart6_read(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    uint16_t got = 0;

    if(buf == NULL || len == 0 || p_uart6ReceiveRingInfo == NULL)
    {
        return -1;
    }

    while(got < len)
    {
        if(p_uart6ReceiveRingInfo->dat_len > 0)
        {
            if(rb_read_data(p_uart6ReceiveRingInfo, &buf[got], 1) == RB_SUCCESS)
            {
                got++;
                continue;
            }
        }

        if(timeout_ms && (elapsed >= timeout_ms))
        {
            break;
        }

        rt_thread_mdelay(1);
        if(timeout_ms)
        {
            elapsed++;
        }
    }

    return (int)got;
}

/**
 * @brief  清空接收缓冲
 */
void uart6_flush(void)
{
    if(p_uart6ReceiveRingInfo != NULL)
    {
        p_uart6ReceiveRingInfo->wr_index = 0;
        p_uart6ReceiveRingInfo->rd_index = 0;
        p_uart6ReceiveRingInfo->dat_len  = 0;
    }
}

/**************************************************************************************************
 *                                        中断服务
 **************************************************************************************************/
#ifndef RT_USING_UART6
void UART6_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void UART6_IRQHandler(void)
{
    GET_INT_SP();
    rt_interrupt_enter();

    if(USART_GetITStatus(UART6, USART_IT_RXNE) != RESET)
    {
        uint8_t data = (uint8_t)USART_ReceiveData(UART6);

        if(p_uart6ReceiveRingInfo != NULL)
        {
            (void)rb_write_byte(p_uart6ReceiveRingInfo, data);
        }
    }

    rt_interrupt_leave();
    FREE_INT_SP();
}
#endif /* RT_USING_UART6 */

/**
 * @brief  发字符串
 */
int uart6_puts(const char *str)
{
    if(str == NULL)
    {
        return -1;
    }

    return uart6_send(str, (uint16_t)strlen(str));
}

/**
 * @brief  格式化发送
 */
int uart6_printf(const char *fmt, ...)
{
    char buf[64];
    va_list args;
    int len;

    if(fmt == NULL)
    {
        return -1;
    }

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if(len <= 0)
    {
        return -1;
    }
    if(len > (int)sizeof(buf) - 1)
    {
        len = (int)sizeof(buf) - 1;
    }

    return uart6_send(buf, (uint16_t)len);
}
