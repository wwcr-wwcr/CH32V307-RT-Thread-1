#ifndef APP_RGB_H
#define APP_RGB_H

#include <stdint.h>

/* 每一个二进制位代表一种基础颜色。 */
#define APP_RGB_OFF       0x00U
#define APP_RGB_RED       0x01U
#define APP_RGB_GREEN     0x02U
#define APP_RGB_BLUE      0x04U

/* 用按位或组合基础颜色。 */
#define APP_RGB_YELLOW    (APP_RGB_RED | APP_RGB_GREEN)
#define APP_RGB_CYAN      (APP_RGB_GREEN | APP_RGB_BLUE)
#define APP_RGB_MAGENTA   (APP_RGB_RED | APP_RGB_BLUE)
#define APP_RGB_WHITE     (APP_RGB_RED | APP_RGB_GREEN | APP_RGB_BLUE)

/*
 * 作用：初始化RGB的三个GPIO，并熄灭RGB灯。
 * 参数：无。
 * 调用：使用app_rgb_set之前调用一次。
 */
void app_rgb_init(void);

/*
 * 作用：设置RGB灯的颜色，设置完成后立即返回。
 * color：上面定义的颜色宏。
 * 示例：app_rgb_set(APP_RGB_RED);
 */
void app_rgb_set(uint8_t color);

/*
 * 功能：初始化TIM7软件PWM，并初始化RGB引脚。
 * 返回：0成功，-1表示定时器时钟不符合要求。
 *
 * 初始亮度为0。
 * 独占TIM7和RGB引脚。
 */
int app_rgb_pwm_init(void);

/*
 * 功能：设置RGB白光亮度。
 * percent：0～100，超过100按100处理。
 *
 * 本版本实际按5%一档输出。
 * 使用前先调用app_rgb_pwm_init()。
 */
void app_rgb_pwm_set_brightness(uint8_t percent);

/*
 * 功能：把RGB白色呼吸灯推进一个5%亮度步进，调用后立即返回。
 * 调用：先调用app_rgb_pwm_init()，再在线程中每隔约50ms调用一次。
 * 效果：50ms调用周期下，完成一次由暗到亮再变暗约需2秒。
 * 注意：本函数不包含延时，不能在中断服务函数中调用。
 */
void app_rgb_breath(void);

/*
 * 功能：停止PWM，并熄灭RGB。
 * 初始化成功后使用。
 * 停止后再次使用，需要重新初始化。
 */
void app_rgb_pwm_stop(void);

#endif
