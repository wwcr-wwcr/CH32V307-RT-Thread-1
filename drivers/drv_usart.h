/*
 * File      : usart.h
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2009, RT-Thread Development Team
 *
 * The license and distribution terms for this file may be
 * found in the file LICENSE in this distribution or at
 * http://www.rt-thread.org/license/LICENSE
 *
 * Change Logs:
 * Date           Author       Notes
 * 2009-01-05     Bernard      the first version
 */
#ifndef __USART_H__
#define __USART_H__
#include "rthw.h"
#include "rtthread.h"

int rt_hw_usart_init(void);

/*********************************************************************
 * 串口流模式（uart_stream.c 用）
 *
 * uart1 只有这一个：平时当控制台，投屏时当二进制数据通道。
 * 切到流模式后控制台输出会被丢弃，原因有两个：
 *   1. rt_kprintf 的 ASCII 会插进二进制流里，PC 端直接解不出来；
 *   2. 驱动里的 putc 是死等 TC 标志的，921600 下一个 128 字节的日志
 *      要 1.4ms，一条日志就能把投屏节奏打乱。
 *********************************************************************/
void    ch32_uart1_stream_set(uint8_t on);
uint8_t ch32_uart1_stream_get(void);
#endif
