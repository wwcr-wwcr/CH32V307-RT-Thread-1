/******************************************************************************
 * 文件名   : uart_stream.h
 * 芯片     : CH32V307VCT6
 * 说明     : 串口"数据流"通道（task6 串口投屏用的底层收包）
 *
 * 为什么单独一个文件：
 *     uart1 同时是控制台（msh/finsh）。要拿它传二进制画面数据，必须做三件事：
 *       1. RX 改成 DMA 循环搬运 + 空闲/半满中断，CPU 不再一个字节一个字节收；
 *       2. 关掉 RXNE 中断，否则 RT-Thread 的串口线程会和 DMA 抢同一个 DATAR；
 *       3. 关掉控制台输出，否则 rt_kprintf 的 ASCII 会插进二进制流里，
 *          而且那个 putc 是死等 TC 的，一条日志就能把投屏卡住。
 *     这三件事都由本文件负责，上层只管"给我一帧"。
 *
 * 数据通路：
 *
 *   USART1 ──DMA1_Channel5(循环)──┐
 *                                 ├─> 4KB 环形缓冲 ─> 状态机拆帧 ─> 回调
 *   IDLE/HT/TC 中断 ──搬字节───┘        （满了丢字节+重新找帧头）
 *
 * 帧格式（小端）：
 *     0    0xA5
 *     1    0x5A
 *     2    type
 *     3    len 低字节
 *     4    len 高字节
 *     5    crc8(type, len_lo, len_hi)      多项式 0x07，初值 0
 *     6..  payload（len 字节）
 *     n    crc16 低字节                     多项式 0x1021，初值 0xFFFF
 *     n+1  crc16 高字节                     （只算 payload）
 *
 * 帧类型：
 *     USTREAM_TYPE_VIDEO  payload = x(2) y(2) w(1) h(1) fmt(1) + 压缩像素数据
 *                         fmt=0：4bit 索引，调色板见 CTRL_PALETTE，
 *                                数据是 PackBits 压缩过的半字节流
 *     USTREAM_TYPE_AUDIO  已停用（投屏不再带声音，板子上没有麦克风，
 *                         喇叭只在 SPEAKER 任务里用）
 *     USTREAM_TYPE_CTRL   payload = cmd(1) + 参数
 *
 * 回调在【任务线程】里被调用（ustream_poll 内部），不要在回调里做耗时操作。
 ******************************************************************************/

#ifndef __UART_STREAM_H
#define __UART_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 协议常量（PC 端脚本 tools/pc_streamer.py 必须和这里一致）
 *********************************************************************/
#define USTREAM_MAGIC0              0xA5u
#define USTREAM_MAGIC1              0x5Au

#define USTREAM_TYPE_VIDEO          0x01u
#define USTREAM_TYPE_AUDIO          0x02u
#define USTREAM_TYPE_CTRL           0x03u

/* 像素格式 */
#define USTREAM_FMT_IDX4            0u      /* 4bit 索引（2 像素/字节）+ PackBits 压缩 */

/* CTRL 命令 */
#define USTREAM_CMD_CLEAR           0u      /* 清屏 */
#define USTREAM_CMD_PALETTE         1u      /* 后面跟 16 个 RGB565（小端，32 字节） */

#define USTREAM_TILE_MAX            32u     /* 单个图块最大边长 */
#define USTREAM_PAYLOAD_MAX         1024u   /* 7 + PackBits(512) 最坏 524，留足余量 */
#define USTREAM_RING_SIZE           4096u

/* 统计信息 */
typedef struct
{
    uint32_t frames;        /* 校验通过的帧总数 */
    uint32_t bad_crc;       /* CRC 错、长度非法被丢掉的帧 */
    uint32_t lost_bytes;    /* 环形缓冲满时丢掉的字节数 */
    uint32_t resync;        /* 因为丢字节而重新找帧头的次数 */
    uint32_t video_frames;
    uint32_t audio_bytes;
    uint32_t bytes;         /* 收到的总字节数（算速率用） */
} ustream_stat_t;

/**
 * @brief  一帧数据到达（校验已通过）
 * @param  type     帧类型
 * @param  payload  负载首地址（下一次 poll 就会被覆盖，要用就自己拷走）
 * @param  len      负载长度
 */
typedef void (*ustream_cb_t)(uint8_t type, const uint8_t *payload, uint16_t len);

/**
 * @brief  进入流模式：改波特率 + 打开 DMA 接收 + 静音控制台
 * @param  baud  目标波特率，0 表示保持当前（115200）
 * @return 0 成功，负数失败（-1 设备打不开，-2 参数错）
 * @note   uart1 只有这一个，进流模式后 msh 就收不到命令了，close 之后恢复。
 */
int  ustream_open(uint32_t baud);

/**
 * @brief  退出流模式：关 DMA / 打开 RXNE / 恢复控制台 / 恢复 115200
 */
void ustream_close(void);

/**
 * @brief  注册收帧回调（在 open 之前调）
 */
void ustream_set_cb(ustream_cb_t cb);

/**
 * @brief  把环形缓冲里现有的字节全部解析掉，触发回调
 * @return 本次解析出的帧数
 * @note   任务线程周期调用（10ms 一次就够）。不阻塞、不分配内存。
 */
int  ustream_poll(void);

/**
 * @brief  取统计信息
 */
void ustream_stat(ustream_stat_t *st);

/**
 * @brief  当前是否在流模式
 */
uint8_t ustream_is_open(void);

#ifdef __cplusplus
}
#endif

#endif /* __UART_STREAM_H */
