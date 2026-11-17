/******************************************************************************
 * 文件名   : ch224.h
 * 芯片     : CH32V307VCT6
 * 说明     : CH224Q USB PD 诱骗芯片驱动（I2C 地址 0x22）
 *
 * 使用示例:
 *     i2c_init(I2C_DEFAULT_SPEED);
 *     ch224_voltage_request(CH224_VOLT_20V);        // 申请 20V
 *     uint8_t state = ch224_protocol_get();         // 看协商结果
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 无应答）
 ******************************************************************************/

#ifndef __CH224_H
#define __CH224_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 参数
 */
#define CH224_ADDRESS               0x22

/* 寄存器 */
#define CH224_REG_PROTOCOL          0x09    /* 当前协议状态 */
#define CH224_REG_VOL_REQ           0x0A    /* 切换申请电压 */
#define CH224_REG_CURRENT_MAX       0x50    /* 协议支持的最大电流 */
#define CH224_REG_AVS_H             0x51
#define CH224_REG_AVS_L             0x52
#define CH224_REG_PPS               0x53

/* 协议状态位 */
#define CH224_STATE_BC              0x01
#define CH224_STATE_QC2             0x02
#define CH224_STATE_QC3             0x04
#define CH224_STATE_PD              0x08
#define CH224_STATE_EPR             0x10
#define CH224_STATE_EPR_SUPPORT     0x20
#define CH224_STATE_AVS_SUPPORT     0x40

/*********************************************************************
 * 类型定义
 */
typedef enum
{
    CH224_VOLT_5V = 0,
    CH224_VOLT_9V,
    CH224_VOLT_12V,
    CH224_VOLT_15V,
    CH224_VOLT_20V,
    CH224_VOLT_28V
} ch224_voltage_t;

/*********************************************************************
 * 函数
 */

/**
 * @brief  探测 CH224 是否在线
 * @return 0 在线，-1 无应答
 */
int ch224_present(void);

/**
 * @brief  读取当前协商到的协议状态
 * @return 状态字节（各 bit 见 CH224_STATE_xxx），读失败返回 0
 */
uint8_t ch224_protocol_get(void);

/**
 * @brief  申请电压
 * @param  vol  目标电压
 * @return 0 成功，-1 无应答
 */
int ch224_voltage_request(ch224_voltage_t vol);

/**
 * @brief  读取协议支持的最大电流
 * @return 电流寄存器原始值（单位见芯片手册），失败返回 0
 */
uint8_t ch224_current_max_get(void);

/**
 * @brief  当前是否已经协商到 PD 协议
 * @return 1 是，0 否
 */
uint8_t ch224_is_pd_mode(void);

/*********************************************************************
 * 旧接口兼容
 */
#define CH224_PROTOCOL_GET_REG      CH224_REG_PROTOCOL
#define CH224_VOL_REQ_REG           CH224_REG_VOL_REQ
#define CH224_CURRENT_MAX_GET_REG   CH224_REG_CURRENT_MAX
#define CH224_BC_ENABLE_STATE       CH224_STATE_BC
#define CH224_QC2_ENABLE_STATE      CH224_STATE_QC2
#define CH224_QC3_ENABLE_STATE      CH224_STATE_QC3
#define CH224_PD_ENABLE_STATE       CH224_STATE_PD
#define CH224_EPR_ENABLE_STATE      CH224_STATE_EPR
#define CH224_EPR_SUPPORT_STATE     CH224_STATE_EPR_SUPPORT
#define CH224_AVS_SUPPORT_STATE     CH224_STATE_AVS_SUPPORT

#define VOL_OPTION_5V               CH224_VOLT_5V
#define VOL_OPTION_9V               CH224_VOLT_9V
#define VOL_OPTION_12V              CH224_VOLT_12V
#define VOL_OPTION_15V              CH224_VOLT_15V
#define VOL_OPTION_20V              CH224_VOLT_20V
#define VOL_OPTION_28V              CH224_VOLT_28V

#define ch224_protpcol_get()        ch224_protocol_get()
#define ch224_vol_request(v)        ch224_voltage_request((ch224_voltage_t)(v))

#ifdef __cplusplus
}
#endif

#endif
