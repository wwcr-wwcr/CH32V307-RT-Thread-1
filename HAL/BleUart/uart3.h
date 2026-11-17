/******************************************************************************
 * 文件名   : uart3.h
 * 芯片     : CH32V307VCT6
 * 说明     : UART3 驱动（PB10 = TX，PB11 = RX），通用串口，不绑定任何模块
 *
 * 特点：中断接收 + 环形缓冲，收发都是非阻塞式，缓冲满了丢新字节不阻塞中断。
 *
 * 缓冲由本模块自己管理（64 字节），调用方不需要关心，也不用再单独初始化。
 * uart3_init() 会清空缓冲，重复调用等于重新开始。
 *
 * 使用示例:
 *     uart3_init(115200);
 *     uart3_printf("temp=%d\r\n", 25);
 *
 *     uint8_t ch;
 *     while(uart3_available() > 0)
 *     {
 *         if(uart3_read(&ch, 1, 10) == 1) { ... }
 *     }
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误，-2 超时）
 ******************************************************************************/

#ifndef __UART3_H
#define __UART3_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 参数
 */
#define UART3_BAUD_DEFAULT      115200UL
#define UART3_RX_BUFFER_SIZE    64          /* 接收环形缓冲大小（字节） */

/*********************************************************************
 * 函数
 *********************************************************************/

/**
 * @brief  初始化 UART3（会清空接收缓冲）
 * @param  baud  波特率
 * @return 0 成功
 */
int uart3_init(uint32_t baud);

/**
 * @brief  发送数据（阻塞到发完）
 * @param  buf  数据
 * @param  len  长度
 * @return 0 成功，-1 参数错误
 */
int uart3_send(const void *buf, uint16_t len);

/**
 * @brief  发送一个字节
 * @return 0 成功
 */
int uart3_putc(uint8_t ch);

/**
 * @brief  接收缓冲里还有多少字节可读
 */
uint16_t uart3_available(void);

/**
 * @brief  读取指定长度的数据（不够就等，超时返回已读到的）
 * @param  buf        读到哪里
 * @param  len        想读多少
 * @param  timeout_ms 超时（毫秒），0 表示一直等
 * @return 实际读到的字节数，负数 = 参数错误
 */
int uart3_read(uint8_t *buf, uint16_t len, uint32_t timeout_ms);

/**
 * @brief  清空接收缓冲
 */
void uart3_flush(void);

/**
 * @brief  发字符串（自动算长度）
 */
int uart3_puts(const char *str);

/**
 * @brief  格式化发送，用法同 printf
 * @note   单次最长 64 字节，超出会被截断
 */
int uart3_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
