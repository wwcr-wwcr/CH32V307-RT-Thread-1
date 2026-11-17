/******************************************************************************
 * 文件名   : qmc5883.c
 * 芯片     : CH32V307VCT6
 * 说明     : QMC5883L 三轴磁力计驱动
 ******************************************************************************/

#include "qmc5883.h"
#include "i2c.h"
#include "math.h"

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
static int qmc5883_write_reg(uint8_t reg, uint8_t value)
{
    return (i2c_write_byte(QMC5883_ADDRESS, reg, value) == I2C_OK) ? 0 : -1;
}

static int qmc5883_read_regs(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return (i2c_read_reg(QMC5883_ADDRESS, reg, buf, len) == I2C_OK) ? 0 : -1;
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化磁力计
 */
int qmc5883_init(void)
{
    return qmc5883_config(QMC5883_MODE_CONTINUOUS, QMC5883_ODR_50HZ,
                          QMC5883_RANGE_2G, QMC5883_OSR_512);
}

/**
 * @brief  自定义配置
 */
int qmc5883_config(qmc5883_mode_t mode, qmc5883_odr_t odr,
                   qmc5883_range_t range, qmc5883_osr_t osr)
{
    /* 设置/复位周期，推荐值 0x01 */
    if(qmc5883_write_reg(QMC5883_REG_SET_RESET, 0x01) != 0)
    {
        return -1;
    }

    return qmc5883_write_reg(QMC5883_REG_CTRL1,
                             (uint8_t)(mode | odr | range | osr));
}

/**
 * @brief  软复位
 */
int qmc5883_reset(void)
{
    return qmc5883_write_reg(QMC5883_REG_CTRL2, 0x81);
}

/**
 * @brief  读取三轴磁场
 */
int qmc5883_read(int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t buf[6];

    if(x == NULL || y == NULL || z == NULL)
    {
        return -2;
    }

    if(qmc5883_read_regs(QMC5883_REG_X_LSB, buf, 6) != 0)
    {
        return -1;
    }

    *x = (int16_t)(buf[0] | (buf[1] << 8));
    *y = (int16_t)(buf[2] | (buf[3] << 8));
    *z = (int16_t)(buf[4] | (buf[5] << 8));

    return 0;
}

/**
 * @brief  数据是否就绪
 */
int qmc5883_data_ready(void)
{
    uint8_t status = 0;

    if(qmc5883_read_regs(QMC5883_REG_STATUS, &status, 1) != 0)
    {
        return 0;
    }

    return (status & 0x01) ? 1 : 0;
}

/**
 * @brief  读取温度
 */
int qmc5883_temperature_get(int16_t *temp_x10)
{
    uint8_t buf[2];

    if(temp_x10 == NULL)
    {
        return -2;
    }

    if(qmc5883_read_regs(QMC5883_REG_TEMP_LSB, buf, 2) != 0)
    {
        return -1;
    }

    *temp_x10 = (int16_t)(buf[0] | (buf[1] << 8));

    return 0;
}

/**
 * @brief  计算航向角（度）
 */
float qmc5883_heading_deg(int16_t x, int16_t y)
{
    float heading;

    if(x == 0 && y == 0)
    {
        return 0.0f;
    }

    heading = atan2f((float)y, (float)x) * 180.0f / 3.14159265358979f;
    heading += QMC5883_DECLINATION_DEG;

    while(heading < 0.0f)
    {
        heading += 360.0f;
    }
    while(heading >= 360.0f)
    {
        heading -= 360.0f;
    }

    return heading;
}

/**
 * @brief  一步读出航向角
 */
float qmc5883_heading(void)
{
    int16_t x, y, z;

    if(qmc5883_read(&x, &y, &z) != 0)
    {
        return -1.0f;
    }

    return qmc5883_heading_deg(x, y);
}
