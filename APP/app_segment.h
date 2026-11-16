#ifndef APP_SEGMENT_H
#define APP_SEGMENT_H

#include <stdint.h>

/*
 * 作用：初始化数码管接口，配置CH422G，并显示00.00。
 * 参数：无。
 * 调用：使用显示函数之前调用一次。
 */
void app_segment_init(void);

/*
 * 作用：显示两组两位数字，中间带小数点。
 * left：左边两位，范围0～99。
 * right：右边两位，范围0～99。
 * 返回：0表示参数有效且发送完成，-1表示参数超出范围。
 * 示例：app_segment_display(12, 34)，显示12.34。
 * 注意：本函数只显示，不自动计时。
 */
int app_segment_display(uint8_t left, uint8_t right);

/*
 * 显示0～9999的四位整数，不点亮中间小数点。
 * 示例：15显示为0015，1234显示为1234。
 * 使用前先调用app_segment_init()。
 */
int app_segment_display_integer(uint16_t value);

/*
 * 作用：显示保留一位小数的温度。
 * temperature_x10：温度乘10，例如253表示25.3℃。
 * 范围：-99.9℃～999.9℃。
 * 返回：0表示发送完成，-1表示超出显示范围。
 * 调用：先调用一次app_segment_init()。
 */
int app_segment_display_temperature(int32_t temperature_x10);

/*
 * 作用：显示光照强度，单位lux，四舍五入为整数。
 * lux_x10：照度乘10，例如3379表示337.9lux。
 * 返回：0发送完成，-1表示四舍五入后超过9999lux。
 * 调用：先初始化数码管；本函数不负责读取传感器。
 */
int app_segment_display_light(uint32_t lux_x10);

#endif
