/******************************************************************************
 * 文件名   : uart_stream.c
 * 芯片     : CH32V307VCT6
 * 说明     : 串口数据流接收（DMA1_Channel5 循环搬运 + 环形缓冲 + 状态机拆帧）
 *
 * 为什么不用 RT-Thread 现成的 rt_device_read：
 *     1. 默认串口驱动是一个字节一次中断，921600bps 下每 10us 就进一次中断，
 *        32 位机扛不住（光中断进出就吃掉大半 CPU）；
 *     2. 驱动里的 RX 是"中断填 fifo"，fifo 只有 64 字节，上层线程稍微一慢就溢出，
 *        而溢出之后是静默丢数据，帧就废了还查不出来。
 *     本文件用 DMA 搬运，CPU 只在半满/装满/空闲时进一次中断，
 *     一次拷 256 字节，CPU 占用降到可忽略；中间再用 4KB 环形缓冲吸收抖动。
 *
 * 缓存堆积怎么处理（这是投屏能不能跑起来的关键）：
 *     - DMA 侧：循环模式，硬件自己转圈写，永不"堵"；
 *     - 环形缓冲：满了就丢字节并置 resync 标志，让状态机重新找帧头，
 *       绝对不会因为缓冲满而卡住生产者（中断里绝不等待）；
 *     - 帧层：CRC 错的帧直接丢，长度非法的帧直接丢，不会污染后面的数据；
 *     - 显示层：由 task_6.c 决定"丢旧帧"，绝不排队。
 ******************************************************************************/

#include "rtthread.h"
#include <rtdevice.h>
#include "ch32v30x_conf.h"
#include "uart_stream.h"

/* drv_usart.c 提供：切流模式（静音控制台 + ISR 改走空闲中断分支） */
extern void ch32_uart1_stream_set(uint8_t on);

#define USTREAM_DMA_SIZE        512u                            /* 必须是 2 的幂 */
#define USTREAM_DMA_MASK        (USTREAM_DMA_SIZE - 1u)
#define USTREAM_RING_MASK       (USTREAM_RING_SIZE - 1u)

/**************************************************************************************************
 *                                        缓冲区
 **************************************************************************************************/
static uint8_t s_dma_buf[USTREAM_DMA_SIZE];
static volatile uint16_t s_dma_rd;                  /* DMA 已经搬到哪了（消费位置） */

static uint8_t s_ring[USTREAM_RING_SIZE];
static volatile uint16_t s_ring_head;               /* 中断写 */
static volatile uint16_t s_ring_tail;               /* 线程读 */

static uint8_t  s_payload[USTREAM_PAYLOAD_MAX];
static volatile uint8_t s_open;

static uint8_t        s_owned;                       /* 设备是不是本模块打开的 */
static ustream_cb_t   s_cb;
static ustream_stat_t s_stat;
static volatile uint8_t s_need_resync;

/**************************************************************************************************
 *                                        CRC
 **************************************************************************************************/
static uint8_t crc8_calc(uint8_t crc, uint8_t dat)
{
    uint8_t i;

    crc ^= dat;
    for(i = 0; i < 8u; i++)
    {
        if(crc & 0x80u) { crc = (uint8_t)((crc << 1) ^ 0x07u); }
        else            { crc = (uint8_t)(crc << 1); }
    }
    return crc;
}

static uint16_t crc16_calc(uint16_t crc, uint8_t dat)
{
    uint8_t i;

    crc ^= (uint16_t)((uint16_t)dat << 8);
    for(i = 0; i < 8u; i++)
    {
        if(crc & 0x8000u) { crc = (uint16_t)((crc << 1) ^ 0x1021u); }
        else              { crc = (uint16_t)(crc << 1); }
    }
    return crc;
}

/**************************************************************************************************
 *                                        环形缓冲（单生产者 / 单消费者）
 **************************************************************************************************/
static void ring_push(uint8_t b)
{
    uint16_t next = (uint16_t)((s_ring_head + 1u) & USTREAM_RING_MASK);

    if(next == s_ring_tail)
    {
        /* 满了：丢这个字节，绝不在这里等。状态机靠 CRC 自动重新同步 */
        s_stat.lost_bytes++;
        s_need_resync = 1;
        return;
    }

    s_ring[s_ring_head] = b;
    s_ring_head = next;
}

static int ring_pop(uint8_t *b)
{
    if(s_ring_tail == s_ring_head)
    {
        return 0;
    }

    *b = s_ring[s_ring_tail];
    s_ring_tail = (uint16_t)((s_ring_tail + 1u) & USTREAM_RING_MASK);
    return 1;
}

/**
 * @brief  把 DMA 缓冲区里"新写进来的"字节搬进环形缓冲
 * @note   中断上下文调用。只搬 [s_dma_rd, 当前写指针) 这段，
 *         这段 DMA 已经写完了、不会再动，所以不加锁也是安全的。
 */
static void dma_drain(void)
{
    uint16_t remain = DMA_GetCurrDataCounter(DMA1_Channel5);
    uint16_t pos    = (uint16_t)((USTREAM_DMA_SIZE - remain) & USTREAM_DMA_MASK);

    while(s_dma_rd != pos)
    {
        ring_push(s_dma_buf[s_dma_rd]);
        s_dma_rd = (uint16_t)((s_dma_rd + 1u) & USTREAM_DMA_MASK);
    }
}

/**************************************************************************************************
 *                                        中断
 **************************************************************************************************/
/* USART1 空闲中断 -> 由 drv_usart.c 的 USART1_IRQHandler 转发过来 */
void ustream_on_idle(void)
{
    dma_drain();
}

/* DMA1 通道 5 = USART1_RX */
void DMA1_Channel5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel5_IRQHandler(void)
{
    GET_INT_SP();
    rt_interrupt_enter();

    if(DMA_GetITStatus(DMA1_IT_HT5) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_HT5);
    }
    if(DMA_GetITStatus(DMA1_IT_TC5) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_TC5);
    }

    dma_drain();

    rt_interrupt_leave();
    FREE_INT_SP();
}

/**************************************************************************************************
 *                                        拆帧状态机
 **************************************************************************************************/
#define P_HUNT      0       /* 等 0xA5 */
#define P_MAGIC     1       /* 等 0x5A */
#define P_TYPE      2
#define P_LEN_L     3
#define P_LEN_H     4
#define P_CRC8      5
#define P_PAYLOAD   6
#define P_CRC16_L   7
#define P_CRC16_H   8

static uint8_t  s_st;
static uint8_t  s_type;
static uint16_t s_len;
static uint16_t s_idx;
static uint8_t  s_hcrc;
static uint16_t s_crc;
static uint16_t s_crc_rx;

static void parser_reset(void)
{
    s_st  = P_HUNT;
    s_idx = 0;
    s_len = 0;
}

/**
 * @brief  喂一个字节
 * @return 1 = 刚好凑完一帧（s_type / s_payload / s_len 有效）
 */
static int frame_feed(uint8_t b)
{
    switch(s_st)
    {
    case P_HUNT:
        if(b == USTREAM_MAGIC0) { s_st = P_MAGIC; }
        break;

    case P_MAGIC:
        if(b == USTREAM_MAGIC1)     { s_st = P_TYPE; }
        else if(b != USTREAM_MAGIC0){ s_st = P_HUNT; }
        break;

    case P_TYPE:
        s_type = b;
        s_hcrc = crc8_calc(0u, b);
        s_st   = P_LEN_L;
        break;

    case P_LEN_L:
        s_len  = b;
        s_hcrc = crc8_calc(s_hcrc, b);
        s_st   = P_LEN_H;
        break;

    case P_LEN_H:
        s_len |= (uint16_t)((uint16_t)b << 8);
        s_hcrc = crc8_calc(s_hcrc, b);
        s_st   = P_CRC8;
        break;

    case P_CRC8:
        if((b != s_hcrc) || (s_len == 0u) || (s_len > USTREAM_PAYLOAD_MAX))
        {
            s_stat.bad_crc++;
            s_st = P_HUNT;
            break;
        }
        s_idx = 0;
        s_crc = 0xFFFFu;
        s_st  = P_PAYLOAD;
        break;

    case P_PAYLOAD:
        s_payload[s_idx++] = b;
        s_crc = crc16_calc(s_crc, b);
        if(s_idx >= s_len) { s_st = P_CRC16_L; }
        break;

    case P_CRC16_L:
        s_crc_rx = b;
        s_st     = P_CRC16_H;
        break;

    case P_CRC16_H:
        s_crc_rx |= (uint16_t)((uint16_t)b << 8);
        s_st = P_HUNT;

        if(s_crc_rx != s_crc)
        {
            s_stat.bad_crc++;
            return 0;
        }

        s_stat.frames++;
        if(s_type == USTREAM_TYPE_VIDEO) { s_stat.video_frames++; }
        if(s_type == USTREAM_TYPE_AUDIO) { s_stat.audio_bytes += s_len; }
        return 1;

    default:
        s_st = P_HUNT;
        break;
    }

    return 0;
}

/**************************************************************************************************
 *                                        对外接口
 **************************************************************************************************/
void ustream_set_cb(ustream_cb_t cb)
{
    s_cb = cb;
}

uint8_t ustream_is_open(void)
{
    return s_open;
}

void ustream_stat(ustream_stat_t *st)
{
    if(st != RT_NULL)
    {
        *st = s_stat;
    }
}

int ustream_open(uint32_t baud)
{
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;
    rt_device_t dev;
    DMA_InitTypeDef  d = {0};
    NVIC_InitTypeDef n = {0};

    if(s_open) { return 0; }

    dev = rt_device_find("uart1");
    if(dev == RT_NULL) { return -1; }

    /* 0. rt_device_control(CONFIG) 只有在设备已经被打开时才会真正下发到硬件，
     *    控制台一般已经打开了它，这里兜一下底，免得换个板子就静悄悄不生效 */
    if(dev->ref_count == 0)
    {
        if(rt_device_open(dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK)
        {
            return -3;
        }
        s_owned = 1;
    }

    /* 1. 先改波特率（这一步会重写 USART 的 CR1，所以必须排在最前面，
     *    否则后面打开的 IDLE 中断会被 USART_Init 清掉） */
    cfg.baud_rate = (baud == 0u) ? BAUD_RATE_115200 : baud;
    cfg.data_bits = DATA_BITS_8;
    cfg.stop_bits = STOP_BITS_1;
    cfg.parity    = PARITY_NONE;
    (void)rt_device_control(dev, RT_DEVICE_CTRL_CONFIG, (void *)&cfg);

    /* 2. 缓冲和状态复位 */
    s_ring_head = 0;
    s_ring_tail = 0;
    s_dma_rd    = 0;
    s_stat.frames       = 0;
    s_stat.bad_crc      = 0;
    s_stat.lost_bytes   = 0;
    s_stat.resync       = 0;
    s_stat.video_frames = 0;
    s_stat.audio_bytes  = 0;
    s_stat.bytes        = 0;
    s_need_resync = 0;
    parser_reset();

    /* 3. 关掉 RXNE 中断：从这一刻起 DATAR 只归 DMA 读，RT-Thread 不再碰 */
    USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);

    /* 4. DMA1 通道 5 = USART1_RX，循环模式 */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_Cmd(DMA1_Channel5, DISABLE);
    DMA_DeInit(DMA1_Channel5);

    d.DMA_PeripheralBaseAddr = (uint32_t)(&USART1->DATAR);
    d.DMA_MemoryBaseAddr     = (uint32_t)s_dma_buf;
    d.DMA_DIR                = DMA_DIR_PeripheralSRC;
    d.DMA_BufferSize         = USTREAM_DMA_SIZE;
    d.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    d.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    d.DMA_Mode               = DMA_Mode_Circular;
    d.DMA_Priority           = DMA_Priority_High;
    d.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel5, &d);

    DMA_ClearITPendingBit(DMA1_IT_GL5);
    DMA_ITConfig(DMA1_Channel5, DMA_IT_HT | DMA_IT_TC, ENABLE);

    n.NVIC_IRQChannel                   = DMA1_Channel5_IRQn;
    n.NVIC_IRQChannelPreemptionPriority = 1;
    n.NVIC_IRQChannelSubPriority        = 1;
    n.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&n);

    DMA_Cmd(DMA1_Channel5, ENABLE);
    USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);

    /* 5. 空闲中断：一帧数据中间的缝隙也能立刻把字节搬走，降低延迟 */
    USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);

    /* 6. 最后才静音控制台：前面任何一步出问题还能看到日志 */
    ch32_uart1_stream_set(1);
    s_open = 1;
    parser_reset();

    return 0;
}

void ustream_close(void)
{
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;
    rt_device_t dev;

    if(!s_open) { return; }

    /* 1. 先恢复控制台输出，让后面的日志能看见 */
    ch32_uart1_stream_set(0);

    /* 2. 关 DMA 和空闲中断 */
    USART_ITConfig(USART1, USART_IT_IDLE, DISABLE);
    USART_DMACmd(USART1, USART_DMAReq_Rx, DISABLE);
    DMA_ITConfig(DMA1_Channel5, DMA_IT_HT | DMA_IT_TC, DISABLE);
    DMA_Cmd(DMA1_Channel5, DISABLE);

    /* 3. 波特率回 115200（同样会重写 CR1，所以 RXNE 要排在它后面） */
    dev = rt_device_find("uart1");
    if(dev != RT_NULL)
    {
        cfg.baud_rate = BAUD_RATE_115200;
        cfg.data_bits = DATA_BITS_8;
        cfg.stop_bits = STOP_BITS_1;
        cfg.parity    = PARITY_NONE;
        (void)rt_device_control(dev, RT_DEVICE_CTRL_CONFIG, (void *)&cfg);
    }

    if(s_owned != 0u)
    {
        rt_device_close(dev);
        s_owned = 0;
    }

    /* 4. RXNE 交回 RT-Thread，控制台和 msh 恢复 */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    s_ring_head = 0;
    s_ring_tail = 0;
    s_dma_rd    = 0;
    parser_reset();
    s_open = 0;
}

int ustream_poll(void)
{
    uint8_t b;
    int n = 0;

    if(!s_open) { return 0; }

    if(s_need_resync)
    {
        /* 上次丢过字节，帧边界已经不可信：回找帧头，别硬解析 */
        s_need_resync = 0;
        parser_reset();
        s_stat.resync++;
    }

    while(ring_pop(&b))
    {
        s_stat.bytes++;
        if(frame_feed(b))
        {
            n++;
            if(s_cb != RT_NULL)
            {
                s_cb(s_type, s_payload, s_len);
            }
        }
    }

    return n;
}
