/******************************************************************************
 * 文件名   : qmc5883.h
 * 芯片     : CH32V307VCT6
 * 说明     : QMC5883L 三轴磁力计（电子罗盘）驱动，I2C 地址 0x0D
 *
 * 使用示例:
 *     i2c_init(I2C_DEFAULT_SPEED);
 *     qmc5883_init();                       // 连续测量 50Hz，±2G
 *     int16_t x, y, z;
 *     qmc5883_read(&x, &y, &z);
 *     float heading = qmc5883_heading_deg(x, y);     // 航向角（度）
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 无应答，-2 参数错误）
 ******************************************************************************/

#ifndef __QMC5883_H
#define __QMC5883_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 参数
 */
#define QMC5883_ADDRESS         0x0D

#define QMC5883_REG_X_LSB       0x00
#define QMC5883_REG_X_MSB       0x01
#define QMC5883_REG_Y_LSB       0x02
#define QMC5883_REG_Y_MSB       0x03
#define QMC5883_REG_Z_LSB       0x04
#define QMC5883_REG_Z_MSB       0x05
#define QMC5883_REG_STATUS      0x06    /* bit0=数据就绪 bit1=溢出 bit2=数据跳过 */
#define QMC5883_REG_TEMP_LSB    0x07
#define QMC5883_REG_TEMP_MSB    0x08
#define QMC5883_REG_CTRL1       0x09    /* OSR | RNG | ODR | MODE */
#define QMC5883_REG_CTRL2       0x0A    /* SOFT_RST | ROL_PNT | INT_ENB */
#define QMC5883_REG_SET_RESET   0x0B
#define QMC5883_REG_CHIP_ID     0x0D

#define QMC5883_RAW_TO_GAUSS    0.0001220703125f    /* 1/8192 G per LSB（2G 量程） */
#define QMC5883_DECLINATION_DEG 0.0f                /* 磁偏角，按所在地修改 */

/*********************************************************************
 * 类型定义
 */
typedef enum
{
    QMC5883_MODE_STANDBY    = 0x00,
    QMC5883_MODE_CONTINUOUS  = 0x01
} qmc5883_mode_t;

typedef enum
{
    QMC5883_ODR_10HZ  = 0x00,
    QMC5883_ODR_50HZ  = 0x04,
    QMC5883_ODR_100HZ = 0x08,
    QMC5883_ODR_200HZ = 0x0C
} qmc5883_odr_t;

typedef enum
{
    QMC5883_RANGE_2G = 0x00,
    QMC5883_RANGE_8G = 0x10
} qmc5883_range_t;

typedef enum
{
    QMC5883_OSR_512  = 0x00,
    QMC5883_OSR_256  = 0x40,
    QMC5883_OSR_128  = 0x80,
    QMC5883_OSR_64   = 0xC0
} qmc5883_osr_t;

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化磁力计（连续测量 50Hz，±2G，最高过采样）
 * @return 0 成功，-1 无应答
 */
int qmc5883_init(void);

/**
 * @brief  自定义配置
 * @return 0 成功，-1 无应答
 */
int qmc5883_config(qmc5883_mode_t mode, qmc5883_odr_t odr,
                   qmc5883_range_t range, qmc5883_osr_t osr);

/**
 * @brief  软复位
 * @return 0 成功
 */
int qmc5883_reset(void);

/**
 * @brief  读取三轴磁场原始值
 * @param  x,y,z  输出
 * @return 0 成功，-1 无应答
 */
int qmc5883_read(int16_t *x, int16_t *y, int16_t *z);

/**
 * @brief  数据是否就绪
 * @return 1 就绪，0 未就绪
 */
int qmc5883_data_ready(void);

/**
 * @brief  读取芯片温度
 * @param  temp_x10  输出温度（0.01℃），例如 2560 = 25.60℃
 * @return 0 成功，-1 无应答
 */
int qmc5883_temperature_get(int16_t *temp_x10);

/**
 * @brief  计算航向角
 * @param  x  磁场 X 原始值
 * @param  y  磁场 Y 原始值
 * @return 航向角（0~360 度），北为 0
 */
float qmc5883_heading_deg(int16_t x, int16_t y);

/*********************************************************************
 * 旧接口兼容
 */
#define QMC5883L_ADDRESS            QMC5883_ADDRESS
#define NORMAL                      0
#define NO_NEW_DATA                 0
#define NEW_DATA_IS_READY           1
#define DATA_OVERFLOW               1
#define DATA_SKIPPED_FOR_READING    1

#define MODE_CONTROL_STANDBY        QMC5883_MODE_STANDBY
#define MODE_CONTROL_CONTINUOUS     QMC5883_MODE_CONTINUOUS
#define OUTPUT_DATA_RATE_10HZ       QMC5883_ODR_10HZ
#define OUTPUT_DATA_RATE_50HZ       QMC5883_ODR_50HZ
#define OUTPUT_DATA_RATE_100HZ      QMC5883_ODR_100HZ
#define OUTPUT_DATA_RATE_200HZ      QMC5883_ODR_200HZ
#define FULL_SCALE_2G               QMC5883_RANGE_2G
#define FULL_SCALE_8G               QMC5883_RANGE_8G
#define OVER_SAMPLE_RATIO_512       QMC5883_OSR_512
#define OVER_SAMPLE_RATIO_256       QMC5883_OSR_256
#define OVER_SAMPLE_RATIO_128       QMC5883_OSR_128
#define OVER_SAMPLE_RATIO_64        QMC5883_OSR_64

#define QMC5883L_Initialize(m, o, r, s)  qmc5883_config((qmc5883_mode_t)(m), (qmc5883_odr_t)(o), \
                                                        (qmc5883_range_t)(r), (qmc5883_osr_t)(s))
#define QMC5883L_Reset()                 qmc5883_reset()
#define QMC5883L_DataIsReady()           qmc5883_data_ready()
#define QMC5883L_Read_Data(x, y, z)      qmc5883_read((x), (y), (z))

/*********************************************************************
 * 宝宝函数（一行搞定，不用管底层细节）
 *********************************************************************/

/**
 * @brief  一步读出航向角（内部自动读三轴并计算）
 * @return 航向角 0~360 度，读失败返回 -1
 * @note   例：float h = qmc5883_heading();
 */
float qmc5883_heading(void);

#ifdef __cplusplus
}
#endif

#endif
