/******************************************************************************
 * 文件名   : nfc.c
 * 芯片     : CH32V307VCT6
 * 说明     : NFC 读卡模块驱动（SPI2）
 ******************************************************************************/

#include "nfc.h"
#include "string.h"
#include "rtthread.h"
#include "stdio.h"

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* SPI 单字节收发 */
uint8_t nfc_spi_transfer(uint8_t data)
{
    while(SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET)
    {
        /* 等待发送缓冲区空 */
    }

    SPI_I2S_SendData(SPI2, data);

    while(SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_RXNE) == RESET)
    {
        /* 等待接收完成 */
    }

    return SPI_I2S_ReceiveData(SPI2);
}

/* 收发一串数据 */
static void nfc_spi_transfer_buf(uint8_t *buf, uint16_t len)
{
    for(uint16_t i = 0; i < len; i++)
    {
        buf[i] = nfc_spi_transfer(buf[i]);
    }
}

/* 帧校验：命令字之后所有字节累加取低 8 位 */
static uint8_t nfc_checksum(const uint8_t *buf, uint16_t len)
{
    uint8_t sum = 0;

    for(uint16_t i = 0; i < len; i++)
    {
        sum = (uint8_t)(sum + buf[i]);
    }

    return sum;
}

/* 发送命令并收回响应
 * tx：要发送的完整帧（不含帧头 0x55，帧头由本函数补）
 * rx：响应数据（从命令字开始）
 */
static int nfc_transaction(uint8_t cmd, const uint8_t *payload, uint8_t payload_len,
                           uint8_t *rx, uint8_t rx_len, uint32_t wait_ms)
{
    uint8_t tx[NFC_PACKET_MAX_LEN];
    uint8_t len = (uint8_t)(payload_len + NFC_PACKET_HEAD_LEN);

    tx[0] = NFC_PACKET_START;
    tx[1] = cmd;
    tx[2] = payload_len;
    if(payload_len && payload != NULL)
    {
        memcpy(&tx[3], payload, payload_len);
    }
    tx[3 + payload_len] = nfc_checksum(&tx[1], (uint16_t)(payload_len + 2));

    /* 片选拉低后先发命令，延时等待模块处理，再在同一帧内读回响应 */
    GPIO_ResetBits(NFC_CS_GPIO, NFC_CS_PIN);
    nfc_spi_transfer_buf(tx, len);

    rt_thread_mdelay(wait_ms);

    memset(rx, 0xFF, (size_t)rx_len);
    nfc_spi_transfer_buf(rx, (uint16_t)rx_len);
    GPIO_SetBits(NFC_CS_GPIO, NFC_CS_PIN);

    /* 响应头校验：0x55 + 命令高位置 1 */
    if(rx[0] != NFC_PACKET_START || rx[1] != (uint8_t)(cmd | 0x80))
    {
        return NFC_ERR_NOCARD;
    }

    return NFC_OK;
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化 NFC 模块
 */
int nfc_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef  SPI_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

    /* 片选：推挽输出，默认拉高 */
    GPIO_InitStructure.GPIO_Pin   = NFC_CS_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(NFC_CS_GPIO, &GPIO_InitStructure);
    GPIO_SetBits(NFC_CS_GPIO, NFC_CS_PIN);

    /* SCK:PB13  MOSI:PB15  MISO:PB14 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_13 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(NFC_CS_GPIO, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_14;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(NFC_CS_GPIO, &GPIO_InitStructure);

    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;
    SPI_Init(SPI2, &SPI_InitStructure);

    SPI_Cmd(SPI2, ENABLE);

    return NFC_OK;
}

/**
 * @brief  开关 NFC
 */
int nfc_enable(uint8_t enable)
{
    uint8_t payload = enable;
    uint8_t rx[5];

    return nfc_transaction(NFC_CMD_ENABLE_REQ, &payload, 1, rx, sizeof(rx), 10);
}

/**
 * @brief  读取模块状态
 */
int nfc_state_read(uint8_t *state)
{
    uint8_t rx[5];

    if(state == NULL)
    {
        return NFC_ERR_PARAM;
    }

    if(nfc_transaction(NFC_CMD_STATE_GET, NULL, 0, rx, sizeof(rx), 10) != NFC_OK)
    {
        return NFC_ERR_NOCARD;
    }

    *state = rx[3];

    return NFC_OK;
}

/**
 * @brief  寻卡
 */
int nfc_card_find(uint8_t scan_mode)
{
    uint8_t payload = scan_mode;
    uint8_t rx[6];

    if(nfc_transaction(NFC_CMD_CARD_FIND_REQ, &payload, 1, rx, sizeof(rx), 100) != NFC_OK)
    {
        return NFC_ERR_NOCARD;
    }

    /* 正常响应：0x92 02 00 00 94 */
    if(rx[2] == 0x02 && rx[3] == 0x00 && rx[4] == 0x00)
    {
        return NFC_OK;
    }

    return NFC_ERR_NOCARD;
}

/**
 * @brief  读一个数据块
 */
int nfc_block_read(uint8_t block, uint8_t *data)
{
    uint8_t rx[NFC_BLOCK_SIZE + 5];

    if(data == NULL)
    {
        return NFC_ERR_PARAM;
    }

    if(nfc_transaction(NFC_CMD_BLOCK_READ_REQ, &block, 1, rx, sizeof(rx), 100) != NFC_OK)
    {
        return NFC_ERR_NOCARD;
    }

    if(rx[2] == 0x02 && rx[3] == 0x00 && rx[4] == 0x00)
    {
        memcpy(data, &rx[5], NFC_BLOCK_SIZE);
        return NFC_OK;
    }

    return NFC_ERR_NOCARD;
}

/**
 * @brief  写一个数据块
 */
int nfc_block_write(uint8_t block, const uint8_t *data)
{
    uint8_t tx[NFC_BLOCK_SIZE + 1];
    uint8_t rx[6];

    if(data == NULL)
    {
        return NFC_ERR_PARAM;
    }

    tx[0] = block;
    memcpy(&tx[1], data, NFC_BLOCK_SIZE);

    if(nfc_transaction(NFC_CMD_BLOCK_WRITE_REQ, tx, sizeof(tx), rx, sizeof(rx), 100) != NFC_OK)
    {
        return NFC_ERR_NOCARD;
    }

    if(rx[2] == 0x02 && rx[3] == 0x00 && rx[4] == 0x00)
    {
        return NFC_OK;
    }

    return NFC_ERR_NOCARD;
}

/**************************************************************************************************
 *                                        宝宝函数
 **************************************************************************************************/
/**
 * @brief  有没有卡
 */
uint8_t nfc_card_present(void)
{
    return (nfc_card_find(0) == NFC_OK) ? 1 : 0;
}
