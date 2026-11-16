#ifndef APP_ENCODER_H
#define APP_ENCODER_H

#include <stdint.h>

/*
 * 被测轴转一整圈时，TIM3实际变化的计数。
 * 必须包含当前编码器模式的倍频，以及需要计入的减速比。
 * 0表示尚未确认，禁止输出未经标定的rpm。
 */
#define APP_ENCODER_COUNTS_PER_REV  0UL

/* 初始化PA6、PA7和TIM3编码器模式，初始计数为0。 */
void app_encoder_init(void);

/* 读取当前16位计数，范围0～65535，不清零计数器。 */
uint16_t app_encoder_get_count(void);

/* 计算两次读数之间的有符号变化量，处理16位回绕。 */
int32_t app_encoder_get_delta(uint16_t current, uint16_t previous);

/*
 * delta：本次采样间隔内的计数变化。
 * elapsed_ticks：两次采样实际间隔，单位为RTT系统tick。
 * counts_per_rev：被测轴每转一圈的实际计数。
 * rpm：保存有符号转速的变量地址，单位转/分钟。
 * 返回：0成功，-1表示参数未知、无效或采样间隔过长。
 */
int app_encoder_calculate_rpm(int32_t delta, uint32_t elapsed_ticks, uint32_t counts_per_rev, int32_t *rpm);

/* 使用模拟数据演示计数差和转速计算，不操作编码器硬件。 */
void app_encoder_speed_demo(void);

#endif