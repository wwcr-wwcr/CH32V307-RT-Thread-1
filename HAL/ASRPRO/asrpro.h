/******************************************************************************
 * 文件名   : asrpro.h
 * 芯片     : CH32V307VCT6
 * 说明     : ASRPRO 语音识别模块（UART6，9600 8N1）
 *
 * 帧格式: 0xA5 + 外设编号 + 命令 + 0x0D + 0x0A（接收帧头为 0x5A）
 *
 * 使用示例:
 *     static void on_cmd(asrpro_uart_peripheral p, asrpro_uart_cmd c)
 *     {
 *         if(p == kLED && c == kON_PRESS_WAKE) { ... }
 *     }
 *     asrpro_init();
 *     asrpro_callback_set(on_cmd);
 *     while(1) { asrpro_poll(); rt_thread_mdelay(10); }
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误）
 ******************************************************************************/

#ifndef __ASRPRO_H
#define __ASRPRO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"
#include "ring_buffer.h"

/*********************************************************************
 * 参数
 */
#define ASRPRO_RING_BUFFER_SIZE     32
#define ASRPRO_FRAME_LEN            5

#define ASRPRO_FRAME_RX_HEAD        0x5A    /* 模块发来的帧头 */
#define ASRPRO_FRAME_TX_HEAD        0xA5    /* 发给模块的帧头 */
#define ASRPRO_FRAME_END1           0x0D
#define ASRPRO_FRAME_END2           0x0A

/*********************************************************************
 * 类型定义
 */
typedef enum
{
    kASRPRO = 0,    /* 语音模块本身 */
    kKEY1,          /* 按键 1 */
    kKEY2,          /* 按键 2 */
    kKEY3,          /* 按键 3 */
    kKEY4,          /* 按键 4 */
    kLED,           /* LED */
    kFAN,           /* 风扇 */
    kHEATING,       /* 加热模块 */
    kLCD,           /* LCD */
    kDISTANCE,      /* 距离传感器 */
    kATTITUDE,      /* 姿态传感器 */
    kLIGHT,         /* 光照传感器 */
    kVoice = 0xFF   /* 语音 */
} asrpro_uart_peripheral;

typedef enum
{
    kOFF_LIFT_SLEEP = 0,        /* 关 / 抬起 / 睡眠 */
    kON_PRESS_WAKE,             /* 开 / 按下 / 唤醒 */
    kBURN_SLOWER_LOWER,         /* 变暗 / 减速 / 降低 */
    kDODGE_FASTER_HIGHER,       /* 变亮 / 加速 / 升高 */
    kGET_STATE,                 /* 读取当前状态 */
    kSHOW_SET_TEMPERATURE,      /* 显示设定温度（加热模块） */
    kSHOW_CURRENT_TEMPERATURE,  /* 显示当前温度（加热模块） */
    kLOWER_HEAT = 0x12,         /* （自定义）降低目标温度 */
    kHIGHER_HEAT = 0x13,        /* （自定义）提高目标温度 */
    kGET_STATE_CUSTOM = 0x14    /* （自定义）读取光照值 */
} asrpro_uart_cmd;

/* 命令回调：解析出一条完整命令后被调用 */
typedef void (*asrpro_cmd_cb_t)(asrpro_uart_peripheral peripheral, asrpro_uart_cmd cmd);

/*********************************************************************
 * 全局变量
 */
extern ring_buffer_t p_uart6ReceiveRingInfo;

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化语音模块（UART6 + 环形缓冲）
 * @return 0 成功
 */
int asrpro_init(void);

/**
 * @brief  注册命令回调
 * @param  cb  回调函数，NULL 表示使用默认处理（打印日志）
 */
void asrpro_callback_set(asrpro_cmd_cb_t cb);

/**
 * @brief  给模块发送命令
 * @param  peripheral  外设编号
 * @param  cmd         命令
 * @return 0 成功
 */
int asrpro_send(asrpro_uart_peripheral peripheral, asrpro_uart_cmd cmd);

/**
 * @brief  解析接收缓冲（需要在主循环里周期调用）
 * @return 解析出的命令条数
 */
int asrpro_poll(void);

/*********************************************************************
 * 旧接口兼容
 */
#define asrpro_uart6_cmd_prpare(p, c)   asrpro_send((p), (c))
#define asrpro_uart6_cmd_process()      asrpro_poll()

#ifdef __cplusplus
}
#endif

#endif
