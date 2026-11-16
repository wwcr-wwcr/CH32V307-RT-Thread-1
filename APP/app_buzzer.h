#ifndef APP_BUZZER_H
#define APP_BUZZER_H

#include <stdint.h>

/*
 * 作用：初始化PA2和TIM2通道3，初始不鸣叫。
 * 调用：使用蜂鸣器之前调用一次。
 */
void app_buzzer_init(void);

/*
 * 作用：开始持续鸣叫，设置完成后立即返回。
 * frequency_hz：PWM频率，单位Hz，范围100～10000。
 * 返回：0表示设置成功，-1表示频率超出范围。
 * 示例：app_buzzer_start(2000);
 */
int app_buzzer_start(uint32_t frequency_hz);

/* 停止鸣叫，将PWM占空比设置为0。 */
void app_buzzer_stop(void);

#endif