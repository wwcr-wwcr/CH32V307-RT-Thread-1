/* USBHS CDC控制协议参考WCH CH32V307EVT SimulateCDC。
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * 原例程及修改版仅用于沁恒微控制器；VID/PID沿用官方学习例程。
 */
#include <stddef.h>
#include "app_usbhs_desc.h"

#include <string.h>



/*

 * 功能：向电脑介绍当前USB设备的基本信息。

 * 类型：CDC通信类设备，后面配置成虚拟串口。

 * 使用：收到GET_DESCRIPTOR请求后，由控制端点发送给电脑。

 * 注意：这是只读数据，不是需要在main中调用的函数。

 */

const uint8_t app_usbhs_device_descriptor[APP_USBHS_DEVICE_DESC_SIZE] =

{

    0x12,             /* 描述符长度：18字节。 */

    0x01,             /* 描述符类型：设备描述符。 */



    0x00, 0x02,       /* USB规范版本：USB 2.00。 */



    0x02,             /* 设备类别：CDC通信类。 */

    0x00,             /* 设备子类别。 */

    0x00,             /* 设备协议。 */



    0x40,             /* 控制端点EP0最大包长度：64字节。 */



    0x86, 0x1A,       /* VID：0x1A86，沿用WCH官方学习例程。 */

    0x0C, 0xFE,       /* PID：0xFE0C，沿用WCH官方学习例程。 */



    0x00, 0x01,       /* 设备版本号：1.00。 */



    0x01,             /* 厂商字符串描述符编号。 */

    0x02,             /* 产品字符串描述符编号。 */

    0x03,             /* 序列号字符串描述符编号。 */



    0x01              /* 当前设备提供1种配置。 */

};



/*

 * 功能：描述高速模式下CDC设备的接口和端点。

 * 使用：电脑请求配置描述符时，通过EP0发送本数组。

 * 注意：数组中各段顺序必须与接口、端点的归属一致。

 */

const uint8_t app_usbhs_config_hs_descriptor[APP_USBHS_CONFIG_DESC_SIZE] =

{

    /* ---------- 配置描述符：9字节 ---------- */

    0x09,             /* 当前这段描述符长度：9字节。 */

    0x02,             /* 类型：配置描述符。 */

    0x43, 0x00,       /* 整组描述符总长度：67字节。 */

    0x02,             /* 接口数量：2个。 */

    0x01,             /* 配置编号：1。 */

    0x00,             /* 无配置名称字符串。 */

    0x80,             /* 总线供电，不支持远程唤醒。 */

    0x32,             /* 声明最大耗电：50 × 2mA = 100mA。 */



    /* ---------- 接口0：CDC控制接口，9字节 ---------- */

    0x09,             /* 接口描述符长度。 */

    0x04,             /* 类型：接口描述符。 */

    0x00,             /* 接口编号：0。 */

    0x00,             /* 备用设置编号：0。 */

    0x01,             /* 此接口有1个非EP0端点。 */

    0x02,             /* 接口类别：CDC通信类。 */

    0x02,             /* 子类别：ACM，抽象控制模型。 */

    0x01,             /* 协议：AT命令协议标识，沿用官方例程。 */

    0x00,             /* 无接口名称字符串。 */



    /* ---------- CDC头功能描述符：5字节 ---------- */

    0x05,             /* 当前描述符长度。 */

    0x24,             /* 类型：类专用接口描述符。 */

    0x00,             /* 子类型：CDC头。 */

    0x10, 0x01,       /* CDC规范版本：1.10。 */



    /* ---------- CDC呼叫管理描述符：5字节 ---------- */

    0x05,

    0x24,

    0x01,             /* 子类型：呼叫管理。 */

    0x00,             /* 不提供额外呼叫管理能力。 */

    0x01,             /* 对应的数据接口编号：1。 */



    /* ---------- CDC ACM功能描述符：4字节 ---------- */

    0x04,

    0x24,

    0x02,             /* 子类型：ACM。 */

    0x02,             /* 支持线编码、控制线状态和串口状态通知。 */



    /* ---------- CDC联合功能描述符：5字节 ---------- */

    0x05,

    0x24,

    0x06,             /* 子类型：联合功能。 */

    0x00,             /* 主接口：控制接口0。 */

    0x01,             /* 从接口：数据接口1。 */



    /* ---------- EP3 IN通知端点：7字节 ---------- */

    0x07,             /* 端点描述符长度。 */

    0x05,             /* 类型：端点描述符。 */

    0x83,             /* 端点3，IN方向。 */

    0x03,             /* 中断传输。 */

    0x10, 0x00,       /* 最大包长度：16字节。 */

    0x08,             /* 高速中断端点服务间隔：16ms。 */



    /* ---------- 接口1：CDC数据接口，9字节 ---------- */

    0x09,

    0x04,

    0x01,             /* 接口编号：1。 */

    0x00,             /* 备用设置编号：0。 */

    0x02,             /* 此接口有2个端点。 */

    0x0A,             /* 接口类别：CDC数据类。 */

    0x00,

    0x00,

    0x00,



    /* ---------- EP2 OUT数据端点：7字节 ---------- */

    0x07,

    0x05,

    0x02,             /* 端点2，OUT：电脑发给开发板。 */

    0x02,             /* 批量传输。 */

    0x00, 0x02,       /* 最大包长度：0x0200 = 512字节。 */

    0x00,             /* 本方案批量端点不使用固定轮询间隔。 */



    /* ---------- EP2 IN数据端点：7字节 ---------- */

    0x07,

    0x05,

    0x82,             /* 端点2，IN：开发板发给电脑。 */

    0x02,             /* 批量传输。 */

    0x00, 0x02,       /* 最大包长度：512字节。 */

    0x00

};



/*

 * 功能：描述全速模式下CDC设备的接口和端点。

 * 使用：设备实际运行在全速模式时，返回这份配置描述符。

 * 区别：批量端点最大包长度为64字节；

 *       中断端点的bInterval按全速规则解释。

 */

const uint8_t app_usbhs_config_fs_descriptor[APP_USBHS_CONFIG_DESC_SIZE] =

{

    /* ---------- 配置描述符 ---------- */

    0x09,

    0x02,

    0x43, 0x00,       /* 整组描述符长度：67字节。 */

    0x02,             /* 两个接口。 */

    0x01,             /* 配置编号1。 */

    0x00,

    0x80,

    0x32,



    /* ---------- 接口0：CDC控制接口 ---------- */

    0x09,

    0x04,

    0x00,

    0x00,

    0x01,

    0x02,

    0x02,

    0x01,

    0x00,



    /* ---------- CDC头功能描述符 ---------- */

    0x05,

    0x24,

    0x00,

    0x10, 0x01,



    /* ---------- CDC呼叫管理描述符 ---------- */

    0x05,

    0x24,

    0x01,

    0x00,

    0x01,



    /* ---------- CDC ACM功能描述符 ---------- */

    0x04,

    0x24,

    0x02,

    0x02,



    /* ---------- CDC联合功能描述符 ---------- */

    0x05,

    0x24,

    0x06,

    0x00,

    0x01,



    /* ---------- EP3 IN：状态通知 ---------- */

    0x07,

    0x05,

    0x83,

    0x03,

    0x10, 0x00,       /* 通知端点最大包长度：16字节。 */

    0x10,             /* 全速模式：服务间隔16ms。 */



    /* ---------- 接口1：CDC数据接口 ---------- */

    0x09,

    0x04,

    0x01,

    0x00,

    0x02,

    0x0A,

    0x00,

    0x00,

    0x00,



    /* ---------- EP2 OUT：电脑发送到开发板 ---------- */

    0x07,

    0x05,

    0x02,

    0x02,

    0x40, 0x00,       /* 全速批量端点最大包长度：64字节。 */

    0x00,



    /* ---------- EP2 IN：开发板发送到电脑 ---------- */

    0x07,

    0x05,

    0x82,

    0x02,

    0x40, 0x00,       /* 全速批量端点最大包长度：64字节。 */

    0x00

};



/*

 * 字符串编号0具有特殊用途：列出支持的语言。

 * 0x0409代表英语（美国），低字节在前。

 */

const uint8_t app_usbhs_string_lang[4] =

{

    0x04,             /* 描述符总长度：4字节。 */

    0x03,             /* 描述符类型：字符串。 */

    0x09, 0x04        /* 语言编号：0x0409。 */

};



/*

 * 字符串编号1：厂商名称“WCH”。

 * 普通USB字符串使用UTF-16LE编码。

 */

const uint8_t app_usbhs_string_manufacturer[8] =

{

    0x08,             /* 总长度：2 + 3 × 2 = 8字节。 */

    0x03,

    'W', 0x00,

    'C', 0x00,

    'H', 0x00

};



/* 字符串编号2：产品名称“CH32 USBHS”。 */

const uint8_t app_usbhs_string_product[22] =

{

    0x16,             /* 总长度：2 + 10 × 2 = 22字节。 */

    0x03,

    'C', 0x00,

    'H', 0x00,

    '3', 0x00,

    '2', 0x00,

    ' ', 0x00,

    'U', 0x00,

    'S', 0x00,

    'B', 0x00,

    'H', 0x00,

    'S', 0x00

};



/* 字符串编号3：固定的学习用序列号“00000001”。 */

const uint8_t app_usbhs_string_serial[18] =

{

    0x12,             /* 总长度：2 + 8 × 2 = 18字节。 */

    0x03,

    '0', 0x00,

    '0', 0x00,

    '0', 0x00,

    '0', 0x00,

    '0', 0x00,

    '0', 0x00,

    '0', 0x00,

    '1', 0x00

};



/*

 * 功能：根据当前实际速度，返回另一种速度的配置。

 * 使用：收到GET_DESCRIPTOR中的OTHER_SPEED_CONFIGURATION请求时调用。

 */


/* 设备限定描述符：从原APP文件归位到描述符文件。 */
const uint8_t app_usbhs_device_qualifier[10] =

{

    0x0A,             /* 当前描述符长度：10字节。 */

    0x06,             /* 类型：设备限定描述符。 */

    0x00, 0x02,       /* USB规范版本：2.00。 */

    0x02,             /* 设备类别：CDC。 */

    0x00,             /* 设备子类别。 */

    0x00,             /* 设备协议。 */

    0x40,             /* 另一种速度下EP0最大包长度：64字节。 */

    0x01,             /* 另一种速度下提供1种配置。 */

    0x00              /* 保留字段，必须为0。 */

};

#define CDC_CONFIG(dtype, bulk_lo, bulk_hi, interval) \
    9,dtype,67,0,2,1,0,0x80,50, \
    9,4,0,0,1,2,2,1,0, \
    5,0x24,0,0x10,0x01, \
    5,0x24,1,0,1, \
    4,0x24,2,2, \
    5,0x24,6,0,1, \
    7,5,0x83,3,16,0,interval, \
    9,4,1,0,2,0x0A,0,0,0, \
    7,5,0x02,2,bulk_lo,bulk_hi,0, \
    7,5,0x82,2,bulk_lo,bulk_hi,0


/* 另一种速度的配置直接保存为只读常量，避免控制请求中反复复制到RAM。 */
static const uint8_t other_fs[67] = {CDC_CONFIG(7,64,0,16)};
static const uint8_t other_hs[67] = {CDC_CONFIG(7,0,2,8)};

const uint8_t *app_usbhs_get_other_speed_descriptor(uint8_t current_high_speed)
{
    return current_high_speed ? other_fs : other_hs;
}

const uint8_t *app_usbhs_descriptor(uint8_t type, uint8_t index, uint8_t high_speed, uint16_t *length)
{
    const uint8_t *result = NULL;
    *length = 0;
    if (index == 0U)
    {
        if (type == 1U) { result = app_usbhs_device_descriptor; *length = sizeof(app_usbhs_device_descriptor); }
        if (type == 2U) { result = high_speed ? app_usbhs_config_hs_descriptor : app_usbhs_config_fs_descriptor; *length = APP_USBHS_CONFIG_DESC_SIZE; }
        if (type == 6U) { result = app_usbhs_device_qualifier; *length = sizeof(app_usbhs_device_qualifier); }
        if (type == 7U) { result = app_usbhs_get_other_speed_descriptor(high_speed); *length = sizeof(other_fs); }
    }
    if (type == 3U)
    {
        switch (index)
        {
            case 0: result = app_usbhs_string_lang; break;
            case 1: result = app_usbhs_string_manufacturer; break;
            case 2: result = app_usbhs_string_product; break;
            case 3: result = app_usbhs_string_serial; break;
            default: break;
        }
        if (result != NULL) { *length = result[0]; }
    }
    return result;
}
