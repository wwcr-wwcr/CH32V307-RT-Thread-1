#ifndef APP_RTC_H
#define APP_RTC_H

#include <stdint.h>

/*
 * 作用：使用外部32.768kHz晶振启动RTC，设置每秒计数一次。
 * 本轮学习版本每次初始化都将秒计数设为0。
 * 返回：0成功，-1表示时钟源冲突或等待超时。
 * 调用：在线程环境中初始化一次。
 */
int app_rtc_init(void);

/*
 * 作用：读取RTC当前秒计数。
 * 返回：32位累计秒数。
 * 调用：必须先确认初始化成功。
 */
uint32_t app_rtc_get_seconds(void);

/*
 * 设置24小时制时间。
 * hour：0～23，minute：0～59，second：0～59。
 * 返回：0成功，-1表示参数错误或写入超时。
 * 必须在RTC初始化成功后调用。
 */
int app_rtc_set_time(uint8_t hour, uint8_t minute, uint8_t second);

/*
 * 读取时分秒，将结果写入三个变量。
 * 参数：对应变量的地址。
 * 返回：0成功，-1表示传入空指针。
 * 必须在RTC初始化成功后调用。
 */
int app_rtc_get_time(uint8_t *hour, uint8_t *minute, uint8_t *second);

#endif