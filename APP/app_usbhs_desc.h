#ifndef APP_USBHS_DESC_H
#define APP_USBHS_DESC_H
#include <stdint.h>

#define APP_USBHS_DEVICE_DESC_SIZE 18U
#define APP_USBHS_CONFIG_DESC_SIZE 67U
extern const uint8_t app_usbhs_device_descriptor[APP_USBHS_DEVICE_DESC_SIZE];
extern const uint8_t app_usbhs_config_fs_descriptor[APP_USBHS_CONFIG_DESC_SIZE];
extern const uint8_t app_usbhs_config_hs_descriptor[APP_USBHS_CONFIG_DESC_SIZE];
extern const uint8_t app_usbhs_device_qualifier[10];
extern const uint8_t app_usbhs_string_lang[4];
extern const uint8_t app_usbhs_string_manufacturer[8];
extern const uint8_t app_usbhs_string_product[22];
extern const uint8_t app_usbhs_string_serial[18];

/* current_high_speed=1返回全速other-speed配置；0返回高速。
 * 返回持久只读数据，本版直接使用Flash中的常量，不再复制到RAM。
 */
const uint8_t *app_usbhs_get_other_speed_descriptor(uint8_t current_high_speed);

/* 返回只读描述符及长度；type见USB规范，index是描述符序号。
 * other-speed描述符按当前实际协商速度返回相反速度的配置。
 * 未支持的描述符返回NULL；由控制端点对请求回应STALL。
 */
const uint8_t *app_usbhs_descriptor(uint8_t type, uint8_t index, uint8_t high_speed, uint16_t *length);
#endif
