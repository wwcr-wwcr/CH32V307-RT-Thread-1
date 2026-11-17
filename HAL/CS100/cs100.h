/******************************************************************************
 * 文件名   : cs100.h
 * 芯片     : CH32V307VCT6
 * 说明     : CS100 超声波测距（PC10=Trig，PC11=Echo，TIM10 输入捕获）
 *
 * 使用示例:
 *     cs100_init();
 *     float d = cs100_measure();          // 直接拿到距离（cm），超时返回 -1
 *     uint16_t cm;
 *     if(cs100_read_cm(&cm) == 0) { ... }
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 超时，-2 参数错误）
 ******************************************************************************/

#ifndef __CS100_H
#define __CS100_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 引脚与参数
 */
#define CS100_TRIG_GPIO         GPIOC
#define CS100_TRIG_PIN          GPIO_Pin_10
#define CS100_ECHO_GPIO         GPIOC
#define CS100_ECHO_PIN          GPIO_Pin_11

#define CS100_TIM_CLOCK         1000000UL                               /* 计数频率 1MHz，1us 一个数 */
#define CS100_TIM_PRESCALER     ((SystemCoreClock / CS100_TIM_CLOCK) - 1)
#define CS100_TIM_PERIOD        0xFFFF
#define CS100_DEFAULT_TIMEOUT   100                                     /* 默认等待回波的时间（ms） */

/*********************************************************************
 * 全局变量
 **************************************************************************************************/
extern volatile uint32_t timerOverflowNum;      /* 捕获期间定时器溢出次数 */
extern volatile uint16_t timerCaptureValue;     /* 捕获到的计数值 */
extern volatile uint8_t  timerCaptureStartFlag; /* 已捕获上升沿 */
extern volatile uint8_t  timerCaptureOverFlag;  /* 捕获完成 */

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化超声波模块（定时器 10 输入捕获）
 * @return 0 成功
 */
int cs100_init(void);

/**
 * @brief  测一次距离（阻塞）
 * @param  timeout_ms  等待回波的最长时间（毫秒）
 * @return 距离（cm），超时或没有回波返回 -1
 */
float cs100_measure_timeout(uint32_t timeout_ms);

/**
 * @brief  测一次距离（阻塞，使用默认超时）
 * @return 距离（cm），失败返回 -1
 */
float cs100_measure(void);

/**
 * @brief  测一次距离，结果取整
 * @param  cm  输出距离（厘米）
 * @return 0 成功，-1 超时
 */
int cs100_read_cm(uint16_t *cm);

/**
 * @brief  把捕获值换算成高电平时间
 * @param  count     捕获寄存器值
 * @param  overflow  溢出次数
 * @return 时间（us）
 */
uint32_t cs100_capture_time_calculate(uint16_t count, uint32_t overflow);

/**
 * @brief  高电平时间换算距离
 * @param  time_us  回波高电平时间（us）
 * @return 距离（cm）
 */
float cs100_distance_calculate(uint32_t time_us);

/*********************************************************************
 * 旧接口兼容
 */
#define CS100_CONTROL_TIM_CLOCK         CS100_TIM_CLOCK
#define CS100_CONTROL_TIM_PRESCALER     CS100_TIM_PRESCALER
#define CS100_CONTROL_TIM_PERIOD        ((CS100_TIM_CLOCK / 1000000 * 65535) - 1)
#define cs100_capture_init(arr, psc)    cs100_init()

/*********************************************************************
 * 宝宝函数（一行搞定，不用管底层细节）
 *********************************************************************/

/**
 * @brief  直接测距离，只要整数厘米（最省事）
 * @return 距离（cm），测不到返回 0xFFFF
 * @note   例：uint16_t d = cs100_distance_cm();
 */
uint16_t cs100_distance_cm(void);

#ifdef __cplusplus
}
#endif

#endif
