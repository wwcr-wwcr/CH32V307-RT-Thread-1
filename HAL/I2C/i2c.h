/******************************************************************************
 * 文件名   : i2c.h
 * 芯片     : CH32V307VCT6
 * 说明     : 硬件 I2C1（重映射到 PB8=SCL / PB9=SDA），主机模式
 *
 * 设计说明:
 *   1. 所有传输都有超时保护，不会像原版那样死等导致整机卡死；
 *   2. 传输失败时自动产生 STOP 复位总线，返回错误码由上层决定是否重试；
 *   3. 提供按字节读写的快捷函数，常见场景一行搞定。
 *
 * 使用示例:
 *     i2c_init(I2C_DEFAULT_SPEED);
 *     if(i2c_probe(0x22) == 0) { ... }               // 探测设备
 *     i2c_write_byte(0x22, 0x0A, 0x01);              // 写寄存器
 *     i2c_read_byte(0x22, 0x09, &val);               // 读寄存器
 *
 * 返回值约定: 0 = 成功，负数 = 失败
 *     I2C_ERR_PARAM    -1  参数错误
 *     I2C_ERR_TIMEOUT  -2  等待超时（总线被拉低 / 从机不回 ACK 时钟）
 *     I2C_ERR_NACK     -3  从机没有应答（器件没接或地址错误）
 ******************************************************************************/

#ifndef __I2C_H
#define __I2C_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 参数
 */
#define I2C_PORT                I2C1
#define I2C_SCL_PIN             GPIO_Pin_8      /* PB8 */
#define I2C_SDA_PIN             GPIO_Pin_9      /* PB9 */
#define I2C_GPIO                GPIOB

#define I2C_SPEED_100K          100000UL
#define I2C_SPEED_400K          400000UL
#define I2C_DEFAULT_SPEED       I2C_SPEED_400K
#define I2C_TIMEOUT_LOOP        100000UL        /* 等待标志的最大轮询次数 */

/* 返回值 */
#define I2C_OK                  0
#define I2C_ERR_PARAM           (-1)
#define I2C_ERR_TIMEOUT         (-2)
#define I2C_ERR_NACK            (-3)

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化 I2C1（主机模式）
 * @param  speed_hz  时钟频率，一般用 I2C_SPEED_100K / I2C_SPEED_400K
 * @return 0 成功，-1 参数错误
 */
int i2c_init(uint32_t speed_hz);

/**
 * @brief  向从机连续写数据（不带寄存器地址）
 * @param  dev_addr  7 位从机地址
 * @param  buf       数据
 * @param  len       长度
 * @return I2C_OK / 负数错误码
 */
int i2c_write(uint8_t dev_addr, const uint8_t *buf, uint16_t len);

/**
 * @brief  从从机连续读数据（不带寄存器地址）
 * @return I2C_OK / 负数错误码
 */
int i2c_read(uint8_t dev_addr, uint8_t *buf, uint16_t len);

/**
 * @brief  写寄存器（先写寄存器地址，再写数据）
 * @param  reg  寄存器地址（8 位）
 * @return I2C_OK / 负数错误码
 */
int i2c_write_reg(uint8_t dev_addr, uint8_t reg, const uint8_t *buf, uint16_t len);

/**
 * @brief  读寄存器
 * @return I2C_OK / 负数错误码
 */
int i2c_read_reg(uint8_t dev_addr, uint8_t reg, uint8_t *buf, uint16_t len);

/**
 * @brief  写单个 8 位寄存器
 * @return I2C_OK / 负数错误码
 */
int i2c_write_byte(uint8_t dev_addr, uint8_t reg, uint8_t value);

/**
 * @brief  读单个 8 位寄存器
 * @param  value  输出
 * @return I2C_OK / 负数错误码
 */
int i2c_read_byte(uint8_t dev_addr, uint8_t reg, uint8_t *value);

/**
 * @brief  探测设备是否存在
 * @return I2C_OK 存在，I2C_ERR_NACK 无应答
 */
int i2c_probe(uint8_t dev_addr);

/**
 * @brief  总线恢复：从机把 SDA 拉低卡死总线时，手动发 9 个时钟解锁
 * @return I2C_OK 成功，I2C_ERR_TIMEOUT 无法恢复
 */
int i2c_bus_recover(void);

/*********************************************************************
 * 旧接口兼容
 */
#define HOST_MODE                       0
#define SLAVE_MODE                      1
#define I2C_MODE                        HOST_MODE
#define i2c_write_command(a, b, l)      i2c_write((a), (b), (l))
#define i2c_read_command(a, b, l)       i2c_read((a), (b), (l))
#define i2c_write_register(a, r, b, l)  i2c_write_reg((a), (r), (b), (l))
#define i2c_read_register(a, r, b, l)   i2c_read_reg((a), (r), (b), (l))
#define i2c_hang_check()                i2c_bus_recover()

/*********************************************************************
 * 宝宝函数（一行搞定，不用管底层细节）
 *********************************************************************/

/**
 * @brief  扫描 I2C 总线，把有应答的地址打印出来（调试神器）
 * @note   例：i2c_scan();  串口会输出 "i2c: found 0x22 0x5D"
 */
void i2c_scan(void);

#ifdef __cplusplus
}
#endif

#endif
