/******************************************************************************
 * 文件名   : rtc.h
 * 芯片     : CH32V307VCT6
 * 说明     : 内部 RTC 实时时钟（HSE/128 作为时钟源，掉电由纽扣电池维持）
 *
 * 使用示例:
 *     rtc_init();                                  // 首次上电用编译时间初始化
 *     rtc_set_datetime(2026, 10, 29, 22, 30, 0);   // 设置时间
 *     rtc_datetime_t dt;
 *     rtc_get_datetime(&dt);                       // 读取当前时间
 *     printf("%04d-%02d-%02d %02d:%02d:%02d 周%d\n",
 *            dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, dt.week);
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误，-2 硬件未就绪）
 ******************************************************************************/

#ifndef __RTC_H
#define __RTC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 常量
 */
#define RTC_START_YEAR      1970
#define RTC_END_YEAR        2099
#define RTC_SECONDS_PER_DAY 86400UL

/* RTC 时钟源：HSE(8MHz) / 128 = 62500Hz，分频到 1Hz */
#define RTC_HSE_FREQ_HZ     8000000UL
#define RTC_CLOCK_HZ        (RTC_HSE_FREQ_HZ / 128UL)
#define RTC_PRESCALER       (RTC_CLOCK_HZ - 1)

/*********************************************************************
 * 类型定义
 */
typedef struct
{
    uint16_t year;      /* 完整年份，例如 2026 */
    uint8_t  month;     /* 1~12 */
    uint8_t  day;       /* 1~31 */
    uint8_t  hour;      /* 0~23 */
    uint8_t  minute;    /* 0~59 */
    uint8_t  second;    /* 0~59 */
    uint8_t  week;      /* 0=周日 1=周一 ... 6=周六 */
} rtc_datetime_t;

/* 兼容旧代码的结构体名 */
typedef struct
{
    volatile uint8_t  hour;
    volatile uint8_t  min;
    volatile uint8_t  sec;
    volatile uint16_t w_year;
    volatile uint8_t  w_month;
    volatile uint8_t  w_date;
    volatile uint8_t  week;
} _calendar_obj;

/*********************************************************************
 * 全局变量
 */
extern _calendar_obj calendar;      /* 最近一次读取的时间（兼容旧代码） */

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化 RTC
 * @return 0 成功，-2 时钟源未就绪
 * @note   若备份寄存器被写过（有电池），保持原时间；否则用编译时间初始化
 */
int rtc_init(void);

/**
 * @brief  设置日期时间
 * @return 0 成功，-1 参数越界
 */
int rtc_set_datetime(uint16_t year, uint8_t month, uint8_t day,
                     uint8_t hour, uint8_t minute, uint8_t second);

/**
 * @brief  读取当前日期时间
 * @param  dt  输出缓冲区
 * @return 0 成功，-1 参数为空
 */
int rtc_get_datetime(rtc_datetime_t *dt);

/**
 * @brief  直接用 Unix 时间戳设置时间
 * @return 0 成功
 */
int rtc_set_timestamp(uint32_t seconds);

/**
 * @brief  读取 Unix 时间戳
 * @return 自 1970-01-01 起的秒数
 */
uint32_t rtc_get_timestamp(void);

/**
 * @brief  设置闹钟
 * @return 0 成功，-1 参数越界
 */
int rtc_alarm_set(uint16_t year, uint8_t month, uint8_t day,
                  uint8_t hour, uint8_t minute, uint8_t second);

/**
 * @brief  计算某天是星期几
 * @return 0=周日 ... 6=周六
 */
uint8_t rtc_week_calc(uint16_t year, uint8_t month, uint8_t day);

/**
 * @brief  星期数字转字符串
 * @return "Sun" / "Mon" ...
 */
const char *rtc_week_name(uint8_t week);

/*********************************************************************
 * 旧接口兼容
 */
#define RTC_Init()                      rtc_init()
#define RTC_Set(y, mo, d, h, mi, s)     rtc_set_datetime((y), (mo), (d), (h), (mi), (s))
#define RTC_Alarm_Set(y, mo, d, h, mi, s) rtc_alarm_set((y), (mo), (d), (h), (mi), (s))
#define RTC_Get_Week(y, mo, d)          rtc_week_calc((y), (mo), (d))

/**
 * @brief  读取当前时间到 calendar（兼容旧接口）
 * @return 0 成功
 */
int RTC_Get(void);

/*********************************************************************
 * 宝宝函数（直接拿字符串，不用自己拼格式）
 *********************************************************************/

/**
 * @brief  读当前时间字符串 "22:56:22"
 * @param  buf   输出缓冲区（至少 9 字节）
 * @param  size  缓冲区大小
 * @return 0 成功
 * @note   例：char t[9]; rtc_time_string(t, sizeof(t)); lcd_text(0,0,t);
 */
int rtc_time_string(char *buf, uint16_t size);

/**
 * @brief  读当前日期字符串 "2026-10-29"
 * @param  buf   输出缓冲区（至少 11 字节）
 * @param  size  缓冲区大小
 * @return 0 成功
 */
int rtc_date_string(char *buf, uint16_t size);

/**
 * @brief  读日期时间字符串 "2026-10-29 22:56:22"
 * @param  buf   输出缓冲区（至少 20 字节）
 * @param  size  缓冲区大小
 * @return 0 成功
 */
int rtc_datetime_string(char *buf, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif
