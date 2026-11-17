/******************************************************************************
 * 文件名   : GT911_IIC.h
 * 芯片     : CH32V307VCT6
 * 说明     : GT911 触摸屏专用软件 I2C（GPIOD 位操作）
 *
 * 引脚:
 *   SCL -> PD7      SDA -> PD6
 *   INT -> PD3      RST -> PD2
 *
 * 说明: 触摸屏对时序要求不严格，这里用软件模拟，方便和 LCD 的 FSMC 共用引脚组。
 *       所有函数出错时返回非 0 值，不再默默失败。
 ******************************************************************************/

#ifndef _GT911_IIC_H_
#define _GT911_IIC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 引脚定义
 */
#define GT911_SCL_GPIO      GPIOD
#define GT911_SDA_GPIO      GPIOD
#define GT911_RST_GPIO      GPIOD
#define GT911_INT_GPIO      GPIOD

#define GT911_SCL_PIN       GPIO_Pin_7
#define GT911_SDA_PIN       GPIO_Pin_6
#define GT911_RST_PIN       GPIO_Pin_3
#define GT911_INT_PIN       GPIO_Pin_2

/* 电平操作 */
#define GT911_SCL_SET()     (GT911_SCL_GPIO->BSHR = GT911_SCL_PIN)
#define GT911_SCL_CLR()     (GT911_SCL_GPIO->BCR  = GT911_SCL_PIN)
#define GT911_SDA_SET()     (GT911_SDA_GPIO->BSHR = GT911_SDA_PIN)
#define GT911_SDA_CLR()     (GT911_SDA_GPIO->BCR  = GT911_SDA_PIN)
#define GT911_SDA_READ()    (GT911_SDA_GPIO->INDR & GT911_SDA_PIN)
#define GT911_RST_SET()     (GT911_RST_GPIO->BSHR = GT911_RST_PIN)
#define GT911_RST_CLR()     (GT911_RST_GPIO->BCR  = GT911_RST_PIN)
#define GT911_INT_SET()     (GT911_INT_GPIO->BSHR = GT911_INT_PIN)
#define GT911_INT_CLR()     (GT911_INT_GPIO->BCR  = GT911_INT_PIN)
#define GT911_INT_READ()    (GT911_INT_GPIO->INDR & GT911_INT_PIN)

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化触摸屏 I2C 引脚（开漏输出，空闲为高）
 * @return 0 成功
 */
int gt911_iic_init(void);

/**
 * @brief  起始条件
 */
void gt911_iic_start(void);

/**
 * @brief  停止条件
 */
void gt911_iic_stop(void);

/**
 * @brief  发送一个字节
 */
void gt911_iic_send_byte(uint8_t data);

/**
 * @brief  接收一个字节
 * @param  ack  1 = 回 ACK，0 = 回 NACK
 * @return 收到的数据
 */
uint8_t gt911_iic_read_byte(uint8_t ack);

/**
 * @brief  等待从机 ACK
 * @return 0 收到 ACK，1 超时（无应答）
 */
uint8_t gt911_iic_wait_ack(void);

/**
 * @brief  设置 SDA 为输入模式（读数据前调用）
 */
void gt911_iic_sda_input(void);

/**
 * @brief  设置 SDA 为开漏输出模式（写数据前调用）
 */
void gt911_iic_sda_output(void);

/*********************************************************************
 * 旧接口兼容
 */
#define GT911_SDA_IN()      gt911_iic_sda_input()
#define GT911_SDA_OUT()     gt911_iic_sda_output()
#define GT911_SCL_OUT()     do { } while(0)
#define GT911_RST_OUT()     do { } while(0)
#define GT911_INT_OUT()     do { } while(0)
#define GT911_INT_IN()      do { } while(0)
#define GT911_iic_gpio_init()   gt911_iic_init()

#ifdef __cplusplus
}
#endif

#endif
