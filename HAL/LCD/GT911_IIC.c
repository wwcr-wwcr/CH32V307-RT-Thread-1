/******************************************************************************
 * 文件名   : GT911_IIC.c
 * 芯片     : CH32V307VCT6
 * 说明     : GT911 触摸屏专用软件 I2C 实现
 ******************************************************************************/

#include "GT911_IIC.h"
#include "rthw.h"

/**************************************************************************************************
 *                                        参数
 **************************************************************************************************/
#define GT911_IIC_HALF_DELAY_US     5       /* 半个时钟周期的延时 */
#define GT911_IIC_ACK_TIMEOUT       250     /* 等待 ACK 的最大轮询次数 */

/**************************************************************************************************
 *                                        引脚方向控制
 **************************************************************************************************/
/**
 * @brief  SDA 切为输入
 */
void gt911_iic_sda_input(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.GPIO_Pin   = GT911_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GT911_SDA_GPIO, &GPIO_InitStructure);
}

/**
 * @brief  SDA 切为开漏输出
 */
void gt911_iic_sda_output(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.GPIO_Pin   = GT911_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GT911_SDA_GPIO, &GPIO_InitStructure);
}

/**
 * @brief  初始化 I2C 引脚
 */
int gt911_iic_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    /* SCL / RST / INT 开漏输出 */
    GPIO_InitStructure.GPIO_Pin   = GT911_SCL_PIN | GT911_RST_PIN | GT911_INT_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GT911_SCL_GPIO, &GPIO_InitStructure);

    gt911_iic_sda_output();

    GT911_SCL_SET();
    GT911_SDA_SET();
    GT911_INT_SET();
    GT911_RST_SET();

    return 0;
}

/**************************************************************************************************
 *                                        I2C 时序
 **************************************************************************************************/
/**
 * @brief  起始条件：SCL 高电平期间 SDA 由高变低
 */
void gt911_iic_start(void)
{
    gt911_iic_sda_output();
    GT911_SCL_SET();
    GT911_SDA_SET();
    rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
    GT911_SDA_CLR();
    rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
    GT911_SCL_CLR();
    rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
}

/**
 * @brief  停止条件：SCL 高电平期间 SDA 由低变高
 */
void gt911_iic_stop(void)
{
    gt911_iic_sda_output();
    GT911_SCL_CLR();
    GT911_SDA_CLR();
    rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
    GT911_SCL_SET();
    rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
    GT911_SDA_SET();
    rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
}

/**
 * @brief  发送一个字节（高位在前）
 */
void gt911_iic_send_byte(uint8_t data)
{
    GT911_SCL_CLR();
    gt911_iic_sda_output();

    for(uint8_t i = 0; i < 8; i++)
    {
        if(data & 0x80)
        {
            GT911_SDA_SET();
        }
        else
        {
            GT911_SDA_CLR();
        }
        data <<= 1;

        rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
        GT911_SCL_SET();
        rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
        GT911_SCL_CLR();
        rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
    }
}

/**
 * @brief  等待从机应答
 */
uint8_t gt911_iic_wait_ack(void)
{
    uint8_t wait = 0;

    gt911_iic_sda_input();
    GT911_SDA_SET();
    rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
    GT911_SCL_SET();
    rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);

    while(GT911_SDA_READ())
    {
        wait++;
        if(wait > GT911_IIC_ACK_TIMEOUT)
        {
            gt911_iic_stop();
            return 1;
        }
    }

    GT911_SCL_CLR();
    rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);

    return 0;
}

/**
 * @brief  接收一个字节
 * @param  ack  1 = 回 ACK（还要继续读），0 = 回 NACK（最后一个字节）
 */
uint8_t gt911_iic_read_byte(uint8_t ack)
{
    uint8_t data = 0;

    gt911_iic_sda_input();

    for(uint8_t i = 0; i < 8; i++)
    {
        GT911_SCL_CLR();
        rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
        GT911_SCL_SET();
        rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);

        data <<= 1;
        if(GT911_SDA_READ())
        {
            data++;
        }
    }

    /* 回 ACK 或 NACK */
    GT911_SCL_CLR();
    gt911_iic_sda_output();

    if(ack)
    {
        GT911_SDA_CLR();
    }
    else
    {
        GT911_SDA_SET();
    }

    rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
    GT911_SCL_SET();
    rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);
    GT911_SCL_CLR();
    rt_hw_us_delay(GT911_IIC_HALF_DELAY_US);

    return data;
}
