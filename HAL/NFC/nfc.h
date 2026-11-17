/******************************************************************************
 * 文件名   : nfc.h
 * 芯片     : CH32V307VCT6
 * 说明     : NFC 读卡模块驱动（SPI2，片选 PB12）
 *
 * 帧格式: 0x55 + 命令 + 长度 + 数据 + 校验和
 *
 * 使用示例:
 *     nfc_init();
 *     nfc_enable(1);
 *     if(nfc_card_find(0, NULL) == NFC_OK)          // 找到卡
 *     {
 *         uint8_t buf[16];
 *         if(nfc_block_read(0, buf) == NFC_OK) { ... }
 *     }
 *
 * 返回值约定:
 *     NFC_OK         0   成功
 *     NFC_ERR_NOCARD -1  没有卡 / 操作失败
 *     NFC_ERR_PARAM  -2  参数错误
 ******************************************************************************/

#ifndef __NFC_H
#define __NFC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 参数
 */
#define NFC_CS_GPIO             GPIOB
#define NFC_CS_PIN              GPIO_Pin_12
#define NFC_BLOCK_SIZE          16

/* SPI 命令 */
#define NFC_CMD_ENABLE_REQ      0x10        /* 开/关 NFC */
#define NFC_CMD_ENABLE_RSP      0x90
#define NFC_CMD_STATE_GET       0x11        /* 读状态 */
#define NFC_CMD_STATE_PUT       0x91
#define NFC_CMD_CARD_FIND_REQ   0x12        /* 寻卡 */
#define NFC_CMD_CARD_FIND_RSP   0x92
#define NFC_CMD_BLOCK_READ_REQ  0x13        /* 读块 */
#define NFC_CMD_BLOCK_READ_RSP  0x93
#define NFC_CMD_BLOCK_WRITE_REQ 0x14        /* 写块 */
#define NFC_CMD_BLOCK_WRITE_RSP 0x94

/* 帧格式 */
#define NFC_PACKET_START        0x55
#define NFC_PACKET_HEAD_LEN     4
#define NFC_PACKET_MAX_LEN      32

/* 状态码 */
#define NFC_OK                  0
#define NFC_ERR_NOCARD          (-1)
#define NFC_ERR_PARAM           (-2)

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化 NFC 模块（SPI2 + 片选）
 * @return NFC_OK
 */
int nfc_init(void);

/**
 * @brief  开关 NFC 功能
 * @param  enable  1 打开，0 关闭
 * @return NFC_OK / NFC_ERR_NOCARD
 */
int nfc_enable(uint8_t enable);

/**
 * @brief  读取模块状态
 * @param  state  输出状态字节
 * @return NFC_OK / NFC_ERR_NOCARD
 */
int nfc_state_read(uint8_t *state);

/**
 * @brief  寻卡
 * @param  scan_mode  寻卡模式（一般填 0）
 * @return NFC_OK 找到卡，NFC_ERR_NOCARD 没有卡
 */
int nfc_card_find(uint8_t scan_mode);

/**
 * @brief  读一个数据块
 * @param  block  块号
 * @param  data   输出缓冲区（至少 16 字节）
 * @return NFC_OK / 负数错误码
 */
int nfc_block_read(uint8_t block, uint8_t *data);

/**
 * @brief  写一个数据块
 * @param  block  块号
 * @param  data   待写数据（16 字节）
 * @return NFC_OK / 负数错误码
 */
int nfc_block_write(uint8_t block, const uint8_t *data);

/**
 * @brief  SPI 单字节收发（调试用）
 */
uint8_t nfc_spi_transfer(uint8_t data);

/*********************************************************************
 * 旧接口兼容
 */
#define PCD_FIND_FAIL               0x0081
#define PCD_SELECT_FAIL             0x0082
#define PCD_NFC_DISABLED            0x0083
#define PCD_COLLISION_CHECK_FAIL    0x0084
#define PCD_INVALID_ADDR            0x0085

#define spi2_init()                 nfc_init()
#define nfc_state_set(en)           nfc_enable(en)
#define nfc_card_scan(mode)         ((uint8_t)(nfc_card_find(mode) == NFC_OK ? 0 : 1))
#define nfc_card_read(b, d)         ((uint8_t)(nfc_block_read((b), (d)) == NFC_OK ? 0 : 1))
#define nfc_card_write(b, d)        ((uint8_t)(nfc_block_write((b), (d)) == NFC_OK ? 0 : 1))

/*********************************************************************
 * 宝宝函数（一行判断有没有卡）
 *********************************************************************/

/**
 * @brief  有没有卡在感应区
 * @return 1 有卡，0 没卡
 * @note   例：if(nfc_card_present()) {...}
 */
uint8_t nfc_card_present(void);

#ifdef __cplusplus
}
#endif

#endif
