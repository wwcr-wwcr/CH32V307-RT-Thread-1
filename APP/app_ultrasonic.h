#ifndef APP_ULTRASONIC_H
#define APP_ULTRASONIC_H

#include <stdint.h>

#define APP_ULTRASONIC_OK        0
#define APP_ULTRASONIC_EINVAL   (-1)
#define APP_ULTRASONIC_ESTATE   (-2)
#define APP_ULTRASONIC_ETIMEOUT (-3)
#define APP_ULTRASONIC_ERANGE   (-4)
#define APP_ULTRASONIC_ECLOCK   (-5)
#define APP_ULTRASONIC_ECAPTURE (-6)

/* ESAD2025 P5：1=GND，2=PC10/TRIG，3=PC11/ECHO，4=3V3。
 * 对应照片中的3.3V CS100A模块，必须按信号名接线。
 * 独占PC10、PC11、TIM10；不创建线程，不配置任何中断。
 */
#define APP_ULTRASONIC_INTERVAL_MS 50U
#define APP_ULTRASONIC_TIMEOUT_MS  40U
#define APP_ULTRASONIC_MIN_US      118U
#define APP_ULTRASONIC_MAX_US      32000U

/* 初始化GPIO和1MHz TIM10，返回0或负错误码；仅在线程/main中调用。
 * 初始化后不自动发出测距脉冲；重复初始化会重置本模块测量状态。
 */
int app_ultrasonic_init(void);

/* 测量一次ECHO高电平脉宽，成功时写入*pulse_us，单位微秒。
 * 返回0成功，负数失败；失败保持调用者原值。
 * 函数内部保证两次触发至少间隔50ms，等待期间让出CPU。
 * 每次测量最长等待40ms；连续调用还可能先等待上次测量周期结束。
 * 不在ISR或LVGL回调中调用；模块内部互斥锁串行化本模块访问。
 * PC11同时送入CH4上升沿和CH3下降沿捕获，避免轮询切换极性漏掉短脉冲。
 * 软件有效窗口118..31999us，约20mm..5.44m；约33ms的无目标回波判为超量程。
 */
int app_ultrasonic_read_us(uint32_t *pulse_us);

/* 一次调用完成触发、捕获、距离换算，直接返回距离mm，失败返回负错误码。
 * 声速按CS100A手册340m/s固定计算，不做温度补偿、滤波或标定。
 * main示例：
 * int32_t mm = app_ultrasonic_get_distance_mm();
 * if (mm >= 0) rt_kprintf("distance=%ld mm\n", (long)mm);
 */
int32_t app_ultrasonic_get_distance_mm(void);

#endif
