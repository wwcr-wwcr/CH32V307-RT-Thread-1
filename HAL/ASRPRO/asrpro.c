/******************************************************************************
 * 文件名   : asrpro.c
 * 芯片     : CH32V307VCT6
 * 说明     : ASRPRO 语音识别模块驱动
 ******************************************************************************/

#include "asrpro.h"
#include "uart6.h"
#include "rtthread.h"

/**************************************************************************************************
 *                                        全局变量
 **************************************************************************************************/
uint8_t uart6ReceiveBuf[ASRPRO_RING_BUFFER_SIZE];
struct ring_buffer uart6ReceiveRingInfo;
ring_buffer_t p_uart6ReceiveRingInfo;

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
static asrpro_cmd_cb_t s_cmd_cb = NULL;

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* 从环形缓冲取一个字节 */
static int asrpro_get_byte(uint8_t *byte)
{
    int ret = -1;

    if(p_uart6ReceiveRingInfo->dat_len == 0)
    {
        return -1;
    }

    NVIC_DisableIRQ(UART6_IRQn);                    /* 防止中断打断读指针 */
    if(rb_read_data(p_uart6ReceiveRingInfo, byte, 1) == RB_SUCCESS)
    {
        ret = 0;
    }
    NVIC_EnableIRQ(UART6_IRQn);

    return ret;
}

/* 默认处理：只打印日志，方便第一次调试 */
static void asrpro_default_handler(asrpro_uart_peripheral peripheral, asrpro_uart_cmd cmd)
{
    static const char *periph_name[] = {
        "ASRPRO", "KEY1", "KEY2", "KEY3", "KEY4", "LED", "FAN",
        "HEATING", "LCD", "DISTANCE", "ATTITUDE", "LIGHT"
    };
    const char *name = "UNKNOWN";

    if(peripheral <= kLIGHT)
    {
        name = periph_name[peripheral];
    }

    rt_kprintf("asrpro: %s cmd 0x%02X\r\n", name, cmd);
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化语音模块
 */
int asrpro_init(void)
{
    uart6_init(UART6_BAUD_DEFAULT);

    p_uart6ReceiveRingInfo = &uart6ReceiveRingInfo;
    rb_param_init(p_uart6ReceiveRingInfo, uart6ReceiveBuf, ASRPRO_RING_BUFFER_SIZE);

    return 0;
}

/**
 * @brief  注册命令回调
 */
void asrpro_callback_set(asrpro_cmd_cb_t cb)
{
    s_cmd_cb = cb;
}

/**
 * @brief  发送命令
 */
int asrpro_send(asrpro_uart_peripheral peripheral, asrpro_uart_cmd cmd)
{
    uint8_t frame[ASRPRO_FRAME_LEN];

    frame[0] = ASRPRO_FRAME_TX_HEAD;
    frame[1] = (uint8_t)peripheral;
    frame[2] = (uint8_t)cmd;
    frame[3] = ASRPRO_FRAME_END1;
    frame[4] = ASRPRO_FRAME_END2;

    return uart6_send(frame, ASRPRO_FRAME_LEN);
}

/**
 * @brief  解析接收缓冲
 */
int asrpro_poll(void)
{
    uint8_t frame[ASRPRO_FRAME_LEN];
    uint8_t byte;
    int     count = 0;

    while(p_uart6ReceiveRingInfo->dat_len > 0)
    {
        /* 1. 找帧头 */
        if(asrpro_get_byte(&byte) != 0)
        {
            break;
        }
        if(byte != ASRPRO_FRAME_RX_HEAD)
        {
            continue;
        }

        /* 2. 收满一帧 */
        frame[0] = byte;
        for(uint8_t i = 1; i < ASRPRO_FRAME_LEN; i++)
        {
            uint32_t timeout = 20;              /* 20ms 收不到就当丢帧 */

            while((p_uart6ReceiveRingInfo->dat_len == 0) && (timeout > 0))
            {
                rt_thread_mdelay(1);
                timeout--;
            }
            if(asrpro_get_byte(&frame[i]) != 0)
            {
                break;
            }
        }

        /* 3. 校验帧尾 */
        if(frame[3] != ASRPRO_FRAME_END1 || frame[4] != ASRPRO_FRAME_END2)
        {
            continue;
        }

        /* 4. 分发 */
        if(s_cmd_cb != NULL)
        {
            s_cmd_cb((asrpro_uart_peripheral)frame[1], (asrpro_uart_cmd)frame[2]);
        }
        else
        {
            asrpro_default_handler((asrpro_uart_peripheral)frame[1], (asrpro_uart_cmd)frame[2]);
        }

        count++;
    }

    return count;
}
