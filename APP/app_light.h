#ifndef APP_LIGHT_H
#define APP_LIGHT_H
#include <stdint.h>

/*
 * 作用：初始化PB8、PB9和I2C1。
 * 本阶段只准备通信接口，还不配置传感器测量参数。
 */
void app_light_bus_init(void);

/*
 * 作用：检查地址0x10是否有设备应答。
 * 返回：0有应答，-1表示总线忙、无应答或等待超时。
 * 调用：先初始化通信接口，再调用本函数。
 */
int app_light_probe(void);

/*
 * 初始化通信接口，并配置传感器开始测量。
 * 返回：0成功，-1通信失败。
 */
int app_light_init(void);

/*
 * 作用：读取VEML7700光照原始值。
 * raw：用于保存结果的变量地址。
 * 返回：0成功，-1表示参数错误或通信失败。
 * 注意：结果是传感器原始计数，还不是lux。
 */
int app_light_read_raw(uint16_t *raw);

/*
 * 作用：读取并换算光照，使用当前固定增益和积分时间。
 * lux_x10：保存照度乘10的整数，253表示25.3lux。
 * 返回：0成功，-1表示参数错误或读取失败。
 * 本阶段采用线性换算。
 */
int app_light_get_lux_x10(uint32_t *lux_x10);

#endif