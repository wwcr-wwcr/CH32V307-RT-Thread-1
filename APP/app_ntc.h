#ifndef APP_NTC_H
#define APP_NTC_H

#include <stdint.h>

/*
 * 作用：初始化PB0和ADC1，并完成校准。
 * 返回：0成功，-1校准超时。
 * 调用：在线程中调用一次，再开始读取。
 */
int app_ntc_init(void);

/*
 * 作用：采集一次热敏电阻电压，返回校准后的ADC码。
 * 返回：0～4095；-1表示未初始化或转换超时。
 * 注意：结果不是摄氏温度。
 */
int32_t app_ntc_read_raw(void);

/*
 * 作用：读取并计算热敏电阻温度。
 * temperature_x10：保存温度结果的变量地址，单位为0.1℃。
 * 示例：返回253，表示25.3℃；返回-52，表示-5.2℃。
 * 返回值：0成功，-1表示参数错误或采样异常。
 */
/* 返回一阶平滑后的温度，单位0.1摄氏度；首次有效值直接输出。
 * 建议由一个固定线程每500ms调用一次；无效采样不更新滤波和输出。
 * app_ntc_init复位滤波；数码管只显示，不再次滤波。
 */
int app_ntc_get_temperature(int32_t *temperature_x10);



#endif