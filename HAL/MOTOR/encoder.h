/******************************************************************************
 * 文件名   : encoder.h
 * 芯片     : CH32V307VCT6
 * 说明     : 增量式编码器输入（TIM3 编码器模式：PA6=A相，PA7=B相）
 *
 * 使用示例:
 *     encoder_init();
 *     int16_t delta = encoder_count_get();        // 取一次增量（同时清零增量）
 *     float   rpm   = encoder_speed_rpm(100);     // 阻塞 100ms 测一次转速
 *
 * 返回值约定: 0 = 成功，负数 = 失败
 ******************************************************************************/

#ifndef __ENCODER_H
#define __ENCODER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 引脚定义
 */
#define ENCODER_CLK         RCC_APB2Periph_GPIOA
#define ENCODER_PORT        GPIOA
#define ENCODER_PIN_A       GPIO_Pin_6        /* A 相 */
#define ENCODER_PIN_B       GPIO_Pin_7        /* B 相 */

/* 编码器一圈的计数（4 倍频：A/B 双相双边沿）。按实际电机参数修改 */
#define ENCODER_COUNTS_PER_REV      4

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化编码器接口（TIM3 编码器模式，4 倍频计数）
 * @return 0 成功
 */
int encoder_init(void);

/**
 * @brief  读取自上次调用以来的计数增量
 * @return 增量（正=正转，负=反转），读取后增量清零
 */
int16_t encoder_count_get(void);

/**
 * @brief  读取累计计数（不会清零，可用于计算总行程）
 * @return 累计计数
 */
int32_t encoder_total_get(void);

/**
 * @brief  清零累计计数
 */
void encoder_reset(void);

/**
 * @brief  测量转速（阻塞采样）
 * @param  period_ms  采样周期（毫秒），建议 50~500
 * @return 转速（转/分），参数错误返回 0
 */
float encoder_speed_rpm(uint32_t period_ms);

/*********************************************************************
 * 旧接口兼容
 */
#define ENCODER_CONTROL_TIM_CLOCK       100000UL
#define ENCODER_COLLECT_INTERVAL        500
#define encoder_capture_init()          encoder_init()

/*********************************************************************
 * 宝宝函数（一行搞定，不用管底层细节）
 *********************************************************************/

/**
 * @brief  直接测转速（采样 100ms，最省事）
 * @return 转/分
 * @note   例：float rpm = encoder_rpm();
 */
float encoder_rpm(void);

/**
 * @brief  转向
 * @return 1 正转，-1 反转，0 静止
 */
int8_t encoder_direction(void);

#ifdef __cplusplus
}
#endif

#endif
