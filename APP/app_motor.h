#ifndef APP_MOTOR_H
#define APP_MOTOR_H

#include <stdint.h>

/*
 * 作用：初始化电机PWM，初始两路输出均为低电平。
 * 调用：使用电机控制前调用一次。
 */
void app_motor_init(void);

/*
 * 作用：设置当前固定方向的PWM占空比。
 * duty_percent：0～100，单位%。
 * 返回：0设置成功，-1表示参数超出范围。
 * 注意：占空比不是实际转速。
 */
int app_motor_set_duty(uint8_t duty_percent);

/* 作用：将两路控制信号设置为低电平，停止驱动。 */
void app_motor_stop(void);

#endif