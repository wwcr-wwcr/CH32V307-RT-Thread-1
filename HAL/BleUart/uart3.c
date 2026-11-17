/******************************************************************************
 * 文件名   : uart3.c
 * 芯片     : CH32V307VCT6
 * 说明     : UART3 驱动（PB10 = TX，PB11 = RX），中断接收 + 环形缓冲
 *
 * 接收环形缓冲是本文件私有的 static 变量，外部看不到也不需要初始化。
 * 调用方只要 uart3_init() 一次，收发就都能用。
 ******************************************************************************/

#include "uart3.h"
#include "ring_buffer.h"
#include "rtthread.h"
#include "rthw.h"
#include "stdio.h"
#include "stdarg.h"
#include "string.h"

/**************************************************************************************************
 *                                        接收缓冲
 **************************************************************************************************/
static uint8_t            s_rx_buf[UART3_RX_BUFFER_SIZE];
static struct ring_buffer s_rx_ring;

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
int uart3_init(uint32_t baud)
{
    GPIO_InitTypeDef  GPIO_InitStructure  = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef  NVIC_InitStructure  = {0};

    /* 先挂好接收缓冲，再开中断：顺序反了中断里会往空指针写 */
    rb_param_init(&s_rx_ring, s_rx_buf, (rb_uint16_t)sizeof(s_rx_buf));

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    /* TX: PB10 复用推挽 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* RX: PB11 上拉输入（悬空时保持高电平，不会收到乱字节） */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = baud;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART3, &USART_InitStructure);

    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 4;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART3, ENABLE);

    return 0;
}

int uart3_send(const void *buf, uint16_t len)
{
    const uint8_t *p = (const uint8_t *)buf;

    if((buf == NULL) || (len == 0))
    {
        return -1;
    }

    for(uint16_t i = 0; i < len; i++)
    {
        while(USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET)
        {
            /* 等待发送寄存器空 */
        }
        USART_SendData(USART3, p[i]);
    }

    while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET)
    {
        /* 等待最后一个字节真正发完 */
    }

    return 0;
}

int uart3_putc(uint8_t ch)
{
    return uart3_send(&ch, 1);
}

uint16_t uart3_available(void)
{
    return s_rx_ring.dat_len;
}

int uart3_read(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    uint16_t got     = 0;

    if((buf == NULL) || (len == 0))
    {
        return -1;
    }

    while(got < len)
    {
        if(s_rx_ring.dat_len > 0u)
        {
            if(rb_read_data(&s_rx_ring, &buf[got], 1) == RB_SUCCESS)
            {
                got++;
                continue;
            }
        }

        if((timeout_ms != 0u) && (elapsed >= timeout_ms))
        {
            break;
        }

        rt_thread_mdelay(1);

        if(timeout_ms != 0u)
        {
            elapsed++;
        }
    }

    return (int)got;
}

void uart3_flush(void)
{
    rb_flush(&s_rx_ring);
}

int uart3_puts(const char *str)
{
    if(str == NULL)
    {
        return -1;
    }

    return uart3_send(str, (uint16_t)strlen(str));
}

int uart3_printf(const char *fmt, ...)
{
    char    buf[64];
    va_list args;
    int     len;

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

    return uart3_send(buf, (uint16_t)len);
}

/**************************************************************************************************
 *                                        中断服务
 **************************************************************************************************/
void USART3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART3_IRQHandler(void)
{
    GET_INT_SP();
    rt_interrupt_enter();

    if(USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    {
        uint8_t data = (uint8_t)USART_ReceiveData(USART3);

        /* 缓冲满了直接丢这个字节，绝不在中断里等待 */
        (void)rb_write_byte(&s_rx_ring, data);
    }

    rt_interrupt_leave();
    FREE_INT_SP();
}
