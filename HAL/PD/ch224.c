/******************************************************************************
 * 文件名   : ch224.c
 * 芯片     : CH32V307VCT6
 * 说明     : CH224Q USB PD 诱骗芯片驱动
 ******************************************************************************/

#include "ch224.h"
#include "i2c.h"

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  探测 CH224
 */
int ch224_present(void)
{
    return i2c_probe(CH224_ADDRESS);
}

/**
 * @brief  读取协议状态
 */
uint8_t ch224_protocol_get(void)
{
    uint8_t state = 0;

    if(i2c_read_byte(CH224_ADDRESS, CH224_REG_PROTOCOL, &state) != I2C_OK)
    {
        return 0;
    }

    return state;
}

/**
 * @brief  申请电压
 */
int ch224_voltage_request(ch224_voltage_t vol)
{
    uint8_t value = (uint8_t)vol & 0x03;

    return (i2c_write_byte(CH224_ADDRESS, CH224_REG_VOL_REQ, value) == I2C_OK) ? 0 : -1;
}

/**
 * @brief  读取最大电流
 */
uint8_t ch224_current_max_get(void)
{
    uint8_t current = 0;

    if(i2c_read_byte(CH224_ADDRESS, CH224_REG_CURRENT_MAX, &current) != I2C_OK)
    {
        return 0;
    }

    return current;
}

/**
 * @brief  是否已经协商到 PD
 */
uint8_t ch224_is_pd_mode(void)
{
    return (ch224_protocol_get() & CH224_STATE_PD) ? 1 : 0;
}
