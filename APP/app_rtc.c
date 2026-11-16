#include "app_rtc.h"
#include "ch32v30x.h"
#include <rtthread.h>

/*
 * 等待指定RTC标志置位。
 * flag：RTC_FLAG_RSF或RTC_FLAG_RTOFF。
 * 返回：0成功，-1超时。
 */
static int app_rtc_wait_flag(uint16_t flag)
{
    rt_tick_t start = rt_tick_get();

    while (RTC_GetFlagStatus(flag) == RESET)
    {
        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(1000))
        {
            return -1;
        }

        rt_thread_mdelay(1);
    }

    return 0;
}

/* 标志	含义
RTC_FLAG_RSF	RTC 寄存器已经完成同步，可以正常访问
RTC_FLAG_RTOFF	上一次 RTC 写操作已经完成，可以进行下一次写操作 */


int app_rtc_init(void)
{
    rt_tick_t start;
    uint32_t clock_source;

    /* 开启电源控制和后备寄存器接口时钟。 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);

    /* 允许修改后备域中的RTC相关配置。 */
    PWR_BackupAccessCmd(ENABLE);

    /* 检查RTC是否已经选择了其他时钟源。 */
    clock_source = RCC->BDCTLR & 0x300U;

    if ((clock_source != 0U) && (clock_source != RCC_RTCCLKSource_LSE))
    {
        return -1;
    }

    /* 启动外部32.768kHz低速晶振。 */
    RCC_LSEConfig(RCC_LSE_ON);

    start = rt_tick_get();

    while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET)
    {
        /* 晶振启动可能较慢，最多等待5秒。 */
        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(5000))
        {
            return -1;
        }

        rt_thread_mdelay(10);
    }

    /* 首次配置时，选择LSE作为RTC时钟。 */
    if (clock_source == 0U)
    {
        RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
    }

    RCC_RTCCLKCmd(ENABLE);

    /* 清除同步标志，等待硬件重新同步。 */
    RTC_ClearFlag(RTC_FLAG_RSF);

    if (app_rtc_wait_flag(RTC_FLAG_RSF) != 0)
    {
        return -1;
    }

    if (app_rtc_wait_flag(RTC_FLAG_RTOFF) != 0)
    {
        return -1;
    }

    /* 32768Hz分频成1Hz。 */
    RTC_SetPrescaler(32767U);

    if (app_rtc_wait_flag(RTC_FLAG_RTOFF) != 0)
    {
        return -1;
    }

    /* 本轮从0秒开始观察。 */
    RTC_SetCounter(0U);

    if (app_rtc_wait_flag(RTC_FLAG_RTOFF) != 0)
    {
        return -1;
    }

    return 0;
}

uint32_t app_rtc_get_seconds(void)
{
    return RTC_GetCounter();
}

int app_rtc_set_time(uint8_t hour, uint8_t minute, uint8_t second)
{
    uint32_t total_seconds;

    /* 拒绝无效时间，避免写入错误结果。 */
    if ((hour > 23U) || (minute > 59U) || (second > 59U))
    {
        return -1;
    }

    /* 将时分秒统一换算成秒。 */
    total_seconds = (uint32_t)hour * 3600U + (uint32_t)minute * 60U + second;

    /* 确认上一次RTC写操作已完成。 */
    if (app_rtc_wait_flag(RTC_FLAG_RTOFF) != 0)
    {
        return -1;
    }

    /* 将新的总秒数写入RTC计数器。 */
    RTC_SetCounter(total_seconds);

    /* 等待本次写入完成，并返回结果。 */
    return app_rtc_wait_flag(RTC_FLAG_RTOFF);
}

int app_rtc_get_time(uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    uint32_t total_seconds;

    /* 三个结果都必须有有效的存放地址。 */
    if ((hour == 0) || (minute == 0) || (second == 0))
    {
        return -1;
    }

    /* 只读取一次，保证三个结果来自同一个时间值。 */
    total_seconds = app_rtc_get_seconds();

    *hour = (uint8_t)((total_seconds / 3600U) % 24U);
    *minute = (uint8_t)((total_seconds / 60U) % 60U);
    *second = (uint8_t)(total_seconds % 60U);

    return 0;
}