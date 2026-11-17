/******************************************************************************
 * 文件名   : uart6.h
 * 芯片     : CH32V307VCT6
 * 说明     : UART6 驱动（PC0=TX，PC1=RX），语音模块 ASRPRO 使用
 *
 * 特点: 中断接收 + 环形缓冲，接收不会丢数据。
 *
 * 使用示例:
 *     uart6_init(9600);
 *     uart6_send(frame, 5);
 *     uint8_t buf[5];
 *     uart6_read(buf, 5, 500);
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误）
 ******************************************************************************/

#ifndef __UART6_H
#define __UART6_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"
#include "ring_buffer.h"

/*********************************************************************
 * 参数
 */
#define UART6_BAUD_DEFAULT      9600UL
#define UART6_RX_BUFFER_SIZE    16

/*********************************************************************
 * 全局变量
 */
extern ring_buffer_t p_uart6ReceiveRingInfo;    /* 接收环形缓冲 */

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化 UART6
 * @param  baud  波特率
 * @return 0 成功
 */
int uart6_init(uint32_t baud);

/**
 * @brief  发送数据（阻塞）
 * @return 0 成功，-1 参数错误
 */
int uart6_send(const void *buf, uint16_t len);

/**
 * @brief  发送一个字节
 */
int uart6_putc(uint8_t ch);

/**
 * @brief  可读字节数
 */
uint16_t uart6_available(void);

/**
 * @brief  读取指定长度（阻塞，带超时）
 * @return 实际读到的字节数
 */
int uart6_read(uint8_t *buf, uint16_t len, uint32_t timeout_ms);

/**
 * @brief  清空接收缓冲
 */
void uart6_flush(void);

/*********************************************************************
 * 宝宝函数（一行搞定，不用管底层细节）
 *********************************************************************/

/**
 * @brief  发字符串
 * @return 0 成功
 */
int uart6_puts(const char *str);

/**
 * @brief  格式化发送，和 printf 一样
 */
int uart6_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
