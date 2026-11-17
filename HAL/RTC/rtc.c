/******************************************************************************
 * 文件名   : rtc.c
 * 芯片     : CH32V307VCT6
 * 说明     : 内部 RTC 实时时钟驱动
 ******************************************************************************/

#include "rtc.h"
#include "string.h"
#include "stdio.h"
#include "rtthread.h"

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
static const uint8_t mon_table[12]  = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const uint8_t table_week[12] = {0, 3, 3, 6, 1, 4, 6, 2, 5, 0, 3, 5};

_calendar_obj calendar;     /* 兼容旧代码的全局时间结构 */

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* 是否闰年 */
static uint8_t rtc_is_leap_year(uint16_t year)
{
    if(year % 4 != 0)
    {
        return 0;
    }
    if(year % 100 != 0)
    {
        return 1;
    }
    return (year % 400 == 0) ? 1 : 0;
}

/* 日期时间转 Unix 时间戳，参数非法返回 0 */
static uint32_t rtc_datetime_to_timestamp(uint16_t year, uint8_t month, uint8_t day,
                                          uint8_t hour, uint8_t minute, uint8_t second)
{
    uint32_t seccount = 0;

    if(year < RTC_START_YEAR || year > RTC_END_YEAR ||
       month < 1 || month > 12 || day < 1 || day > 31 ||
       hour > 23 || minute > 59 || second > 59)
    {
        return 0;
    }

    for(uint16_t y = RTC_START_YEAR; y < year; y++)
    {
        seccount += rtc_is_leap_year(y) ? 31622400UL : 31536000UL;
    }

    for(uint8_t m = 0; m < (month - 1); m++)
    {
        seccount += (uint32_t)mon_table[m] * RTC_SECONDS_PER_DAY;
        if(rtc_is_leap_year(year) && m == 1)
        {
            seccount += RTC_SECONDS_PER_DAY;        /* 闰年 2 月多一天 */
        }
    }

    seccount += (uint32_t)(day - 1) * RTC_SECONDS_PER_DAY;
    seccount += (uint32_t)hour * 3600;
    seccount += (uint32_t)minute * 60;
    seccount += second;

    return seccount;
}

/* 用编译时间初始化 RTC（__DATE__ = "Mmm dd yyyy"，__TIME__ = "hh:mm:ss"） */
static void rtc_use_build_time(void)
{
    const char *date = __DATE__;
    const char *time = __TIME__;
    static const char *month_name[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    uint8_t month = 1;

    for(uint8_t i = 0; i < 12; i++)
    {
        if(memcmp(date, month_name[i], 3) == 0)
        {
            month = i + 1;
            break;
        }
    }

    calendar.w_year  = (uint16_t)((date[7] - '0') * 1000 + (date[8] - '0') * 100 +
                                 (date[9] - '0') * 10 + (date[10] - '0'));
    calendar.w_month = month;
    calendar.w_date  = (uint8_t)(((date[4] == ' ') ? 0 : (date[4] - '0') * 10) + (date[5] - '0'));
    calendar.hour    = (uint8_t)((time[0] - '0') * 10 + (time[1] - '0'));
    calendar.min     = (uint8_t)((time[3] - '0') * 10 + (time[4] - '0'));
    calendar.sec     = (uint8_t)((time[6] - '0') * 10 + (time[7] - '0'));

    rt_kprintf("RTC: use build time %d-%02d-%02d %02d:%02d:%02d\r\n",
               calendar.w_year, calendar.w_month, calendar.w_date,
               calendar.hour, calendar.min, calendar.sec);
}

/* RTC 中断优先级配置 */
static void rtc_nvic_config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    NVIC_InitStructure.NVIC_IRQChannel                   = RTC_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化 RTC
 */
int rtc_init(void)
{
    uint8_t wait = 0;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    /* 已经有电池维持的时间，只需要读回来 */
    if(BKP_ReadBackupRegister(BKP_DR1) == 0xA1A1)
    {
        RTC_WaitForSynchro();
        RTC_Get();
        return 0;
    }

    rtc_use_build_time();
    BKP_DeInit();

    while((RCC_GetFlagStatus(RCC_FLAG_HSERDY) == RESET) && (wait < 250))
    {
        wait++;
        rt_thread_mdelay(20);
    }
    if(wait >= 250)
    {
        rt_kprintf("RTC: HSE not ready\r\n");
        return -2;
    }

    RCC_RTCCLKConfig(RCC_RTCCLKSource_HSE_Div128);
    RCC_RTCCLKCmd(ENABLE);
    RTC_WaitForLastTask();
    RTC_WaitForSynchro();

    RTC_EnterConfigMode();
    RTC_SetPrescaler(RTC_PRESCALER);    /* HSE/128 = 62500Hz，分频到 1Hz */
    RTC_WaitForLastTask();
    RTC_ExitConfigMode();

    rtc_set_datetime(calendar.w_year, calendar.w_month, calendar.w_date,
                     calendar.hour, calendar.min, calendar.sec);

    BKP_WriteBackupRegister(BKP_DR1, 0xA1A1);

    rtc_nvic_config();
    RTC_Get();

    return 0;
}

/**
 * @brief  设置日期时间
 */
int rtc_set_datetime(uint16_t year, uint8_t month, uint8_t day,
                     uint8_t hour, uint8_t minute, uint8_t second)
{
    uint32_t seccount;

    if(year < RTC_START_YEAR || year > RTC_END_YEAR ||
       month < 1 || month > 12 || day < 1 || day > 31 ||
       hour > 23 || minute > 59 || second > 59)
    {
        return -1;
    }

    seccount = rtc_datetime_to_timestamp(year, month, day, hour, minute, second);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetCounter(seccount);
    RTC_WaitForLastTask();

    return 0;
}

/**
 * @brief  直接用时间戳设置时间
 */
int rtc_set_timestamp(uint32_t seconds)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetCounter(seconds);
    RTC_WaitForLastTask();

    return 0;
}

/**
 * @brief  读取时间戳
 */
uint32_t rtc_get_timestamp(void)
{
    return RTC_GetCounter();
}

/**
 * @brief  设置闹钟
 */
int rtc_alarm_set(uint16_t year, uint8_t month, uint8_t day,
                  uint8_t hour, uint8_t minute, uint8_t second)
{
    uint32_t seccount;

    if(year < RTC_START_YEAR || year > RTC_END_YEAR ||
       month < 1 || month > 12 || day < 1 || day > 31 ||
       hour > 23 || minute > 59 || second > 59)
    {
        return -1;
    }

    seccount = rtc_datetime_to_timestamp(year, month, day, hour, minute, second);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetAlarm(seccount);
    RTC_WaitForLastTask();

    return 0;
}

/**
 * @brief  读取当前日期时间
 */
int rtc_get_datetime(rtc_datetime_t *dt)
{
    uint32_t timecount;
    uint32_t days;
    uint32_t secs;
    uint16_t year = RTC_START_YEAR;
    uint8_t  month = 1;

    if(dt == NULL)
    {
        return -1;
    }

    timecount = RTC_GetCounter();
    days      = timecount / RTC_SECONDS_PER_DAY;
    secs      = timecount % RTC_SECONDS_PER_DAY;

    /* 从 1970 年开始逐年扣减 */
    while(days >= 365)
    {
        uint16_t days_of_year = rtc_is_leap_year(year) ? 366 : 365;

        if(days < days_of_year)
        {
            break;
        }
        days -= days_of_year;
        year++;
    }

    /* 逐月扣减 */
    for(uint8_t m = 0; m < 12; m++)
    {
        uint8_t days_of_month = mon_table[m];

        if(m == 1 && rtc_is_leap_year(year))
        {
            days_of_month = 29;
        }
        if(days < days_of_month)
        {
            break;
        }
        days -= days_of_month;
        month++;
    }

    dt->year   = year;
    dt->month  = month;
    dt->day    = (uint8_t)(days + 1);
    dt->hour   = (uint8_t)(secs / 3600);
    dt->minute = (uint8_t)((secs % 3600) / 60);
    dt->second = (uint8_t)(secs % 60);
    dt->week   = rtc_week_calc(dt->year, dt->month, dt->day);

    return 0;
}

/**
 * @brief  读取当前时间到 calendar（兼容旧接口）
 */
int RTC_Get(void)
{
    rtc_datetime_t dt;

    if(rtc_get_datetime(&dt) != 0)
    {
        return -1;
    }

    calendar.w_year  = dt.year;
    calendar.w_month = dt.month;
    calendar.w_date  = dt.day;
    calendar.hour    = dt.hour;
    calendar.min     = dt.minute;
    calendar.sec     = dt.second;
    calendar.week    = dt.week;

    return 0;
}

/**
 * @brief  计算星期
 */
uint8_t rtc_week_calc(uint16_t year, uint8_t month, uint8_t day)
{
    uint16_t temp;
    uint8_t  year_h, year_l;

    if(month < 1 || month > 12)
    {
        return 0;
    }

    year_h = (uint8_t)(year / 100);
    year_l = (uint8_t)(year % 100);
    if(year_h > 19)
    {
        year_l += 100;
    }

    temp = year_l + year_l / 4;
    temp = temp % 7;
    temp = temp + day + table_week[month - 1];
    if((year_l % 4 == 0) && (month < 3))
    {
        temp--;
    }

    return (uint8_t)(temp % 7);
}

/**
 * @brief  星期数字转字符串
 */
const char *rtc_week_name(uint8_t week)
{
    static const char *name[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    return name[week % 7];
}

/**************************************************************************************************
 *                                        宝宝函数
 **************************************************************************************************/
/**
 * @brief  时间字符串
 */
int rtc_time_string(char *buf, uint16_t size)
{
    rtc_datetime_t dt;

    if(buf == NULL || size < 9)
    {
        return -1;
    }

    if(rtc_get_datetime(&dt) != 0)
    {
        return -1;
    }

    snprintf(buf, size, "%02d:%02d:%02d", dt.hour, dt.minute, dt.second);

    return 0;
}

/**
 * @brief  日期字符串
 */
int rtc_date_string(char *buf, uint16_t size)
{
    rtc_datetime_t dt;

    if(buf == NULL || size < 11)
    {
        return -1;
    }

    if(rtc_get_datetime(&dt) != 0)
    {
        return -1;
    }

    snprintf(buf, size, "%04d-%02d-%02d", dt.year, dt.month, dt.day);

    return 0;
}

/**
 * @brief  日期时间字符串
 */
int rtc_datetime_string(char *buf, uint16_t size)
{
    rtc_datetime_t dt;

    if(buf == NULL || size < 20)
    {
        return -1;
    }

    if(rtc_get_datetime(&dt) != 0)
    {
        return -1;
    }

    snprintf(buf, size, "%04d-%02d-%02d %02d:%02d:%02d",
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);

    return 0;
}
