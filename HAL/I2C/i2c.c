/******************************************************************************
 * 文件名   : i2c.c
 * 芯片     : CH32V307VCT6
 * 说明     : 硬件 I2C1 主机驱动（PB8=SCL / PB9=SDA），带超时与错误处理
 ******************************************************************************/

#include "i2c.h"
#include "rtthread.h"
#include "rthw.h"

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* 等待某个标志位到达指定状态，超时返回错误 */
static int i2c_wait_flag(uint32_t flag, FlagStatus state)
{
    uint32_t timeout = I2C_TIMEOUT_LOOP;

    while(I2C_GetFlagStatus(I2C_PORT, flag) != state)
    {
        if(--timeout == 0)
        {
            return I2C_ERR_TIMEOUT;
        }
    }

    return I2C_OK;
}

/* 等待某个事件，超时返回错误 */
static int i2c_wait_event(uint32_t event)
{
    uint32_t timeout = I2C_TIMEOUT_LOOP;

    while(I2C_CheckEvent(I2C_PORT, event) == NoREADY)
    {
        /* 从机无应答（地址错/器件没接）立即返回，不必等超时 */
        if(I2C_GetFlagStatus(I2C_PORT, I2C_FLAG_AF) != RESET)
        {
            I2C_ClearFlag(I2C_PORT, I2C_FLAG_AF);
            return I2C_ERR_NACK;
        }
        if(--timeout == 0)
        {
            return I2C_ERR_TIMEOUT;
        }
    }

    return I2C_OK;
}

/* 结束当前传输：产生 STOP 并释放总线 */
static void i2c_release(void)
{
    I2C_GenerateSTOP(I2C_PORT, ENABLE);
    I2C_AcknowledgeConfig(I2C_PORT, ENABLE);
}

/* 发送起始条件 + 7 位地址，失败时自动复位总线 */
static int i2c_start_addr(uint8_t dev_addr, uint8_t direction)
{
    int ret;

    I2C_GenerateSTART(I2C_PORT, ENABLE);
    ret = i2c_wait_event(I2C_EVENT_MASTER_MODE_SELECT);
    if(ret != I2C_OK)
    {
        i2c_release();
        return ret;
    }

    I2C_Send7bitAddress(I2C_PORT, (uint8_t)(dev_addr << 1), direction);

    if(direction == I2C_Direction_Transmitter)
    {
        ret = i2c_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED);
    }
    else
    {
        /* 接收模式：手动等待并清除 ADDR，便于在单字节读取前就设置 NACK */
        ret = i2c_wait_flag(I2C_FLAG_ADDR, SET);
        if(ret == I2C_OK)
        {
            (void)I2C_PORT->STAR1;
            (void)I2C_PORT->STAR2;
        }
    }

    if(ret != I2C_OK)
    {
        i2c_release();
    }

    return ret;
}

/* 接收 len 个字节（调用前地址阶段已完成） */
static int i2c_receive(uint8_t *buf, uint16_t len)
{
    int ret;

    if(len == 1)
    {
        /* 只有一个字节：先关应答再产生停止条件 */
        I2C_AcknowledgeConfig(I2C_PORT, DISABLE);
        I2C_GenerateSTOP(I2C_PORT, ENABLE);
    }

    for(uint16_t i = 0; i < len; i++)
    {
        ret = i2c_wait_flag(I2C_FLAG_RXNE, SET);
        if(ret != I2C_OK)
        {
            i2c_release();
            return ret;
        }

        /* 还剩两个字节时，说明当前是倒数第二个，先准备好 NACK + STOP */
        if((len - i) == 2)
        {
            I2C_AcknowledgeConfig(I2C_PORT, DISABLE);
            I2C_GenerateSTOP(I2C_PORT, ENABLE);
        }

        buf[i] = I2C_ReceiveData(I2C_PORT);
    }

    I2C_AcknowledgeConfig(I2C_PORT, ENABLE);

    return I2C_OK;
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  初始化 I2C1 主机
 */
int i2c_init(uint32_t speed_hz)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    I2C_InitTypeDef  I2C_InitStructure = {0};

    if(speed_hz != I2C_SPEED_100K && speed_hz != I2C_SPEED_400K)
    {
        /* 允许其它合法频率，但限制在 10k~400k 之间 */
        if(speed_hz < 10000UL || speed_hz > 400000UL)
        {
            return I2C_ERR_PARAM;
        }
    }

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE);
    GPIO_InitStructure.GPIO_Pin   = I2C_SCL_PIN | I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(I2C_GPIO, &GPIO_InitStructure);

    I2C_DeInit(I2C_PORT);

    I2C_InitStructure.I2C_ClockSpeed          = speed_hz;
    I2C_InitStructure.I2C_Mode                = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle           = I2C_DutyCycle_16_9;
    I2C_InitStructure.I2C_OwnAddress1         = 0x00;
    I2C_InitStructure.I2C_Ack                 = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C_PORT, &I2C_InitStructure);

    I2C_Cmd(I2C_PORT, ENABLE);
    I2C_AcknowledgeConfig(I2C_PORT, ENABLE);

    return I2C_OK;
}

/**
 * @brief  连续写
 */
int i2c_write(uint8_t dev_addr, const uint8_t *buf, uint16_t len)
{
    int ret;

    if(buf == NULL || len == 0)
    {
        return I2C_ERR_PARAM;
    }

    ret = i2c_start_addr(dev_addr, I2C_Direction_Transmitter);
    if(ret != I2C_OK)
    {
        return ret;
    }

    for(uint16_t i = 0; i < len; i++)
    {
        ret = i2c_wait_flag(I2C_FLAG_TXE, SET);
        if(ret != I2C_OK)
        {
            i2c_release();
            return ret;
        }
        I2C_SendData(I2C_PORT, buf[i]);
    }

    ret = i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED);
    i2c_release();

    return ret;
}

/**
 * @brief  连续读
 */
int i2c_read(uint8_t dev_addr, uint8_t *buf, uint16_t len)
{
    int ret;

    if(buf == NULL || len == 0)
    {
        return I2C_ERR_PARAM;
    }

    ret = i2c_start_addr(dev_addr, I2C_Direction_Receiver);
    if(ret != I2C_OK)
    {
        return ret;
    }

    return i2c_receive(buf, len);
}

/**
 * @brief  写寄存器
 */
int i2c_write_reg(uint8_t dev_addr, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    int ret;

    if(buf == NULL || len == 0)
    {
        return I2C_ERR_PARAM;
    }

    ret = i2c_start_addr(dev_addr, I2C_Direction_Transmitter);
    if(ret != I2C_OK)
    {
        return ret;
    }

    ret = i2c_wait_flag(I2C_FLAG_TXE, SET);
    if(ret != I2C_OK)
    {
        i2c_release();
        return ret;
    }
    I2C_SendData(I2C_PORT, reg);

    for(uint16_t i = 0; i < len; i++)
    {
        ret = i2c_wait_flag(I2C_FLAG_TXE, SET);
        if(ret != I2C_OK)
        {
            i2c_release();
            return ret;
        }
        I2C_SendData(I2C_PORT, buf[i]);
    }

    ret = i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED);
    i2c_release();

    return ret;
}

/**
 * @brief  读寄存器
 */
int i2c_read_reg(uint8_t dev_addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    int ret;

    if(buf == NULL || len == 0)
    {
        return I2C_ERR_PARAM;
    }

    /* 1. 先发寄存器地址 */
    ret = i2c_start_addr(dev_addr, I2C_Direction_Transmitter);
    if(ret != I2C_OK)
    {
        return ret;
    }

    ret = i2c_wait_flag(I2C_FLAG_TXE, SET);
    if(ret != I2C_OK)
    {
        i2c_release();
        return ret;
    }
    I2C_SendData(I2C_PORT, reg);

    ret = i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED);
    if(ret != I2C_OK)
    {
        i2c_release();
        return ret;
    }

    /* 2. 重新起始并读数据 */
    ret = i2c_start_addr(dev_addr, I2C_Direction_Receiver);
    if(ret != I2C_OK)
    {
        return ret;
    }

    return i2c_receive(buf, len);
}

/**
 * @brief  写单个寄存器
 */
int i2c_write_byte(uint8_t dev_addr, uint8_t reg, uint8_t value)
{
    return i2c_write_reg(dev_addr, reg, &value, 1);
}

/**
 * @brief  读单个寄存器
 */
int i2c_read_byte(uint8_t dev_addr, uint8_t reg, uint8_t *value)
{
    return i2c_read_reg(dev_addr, reg, value, 1);
}

/**
 * @brief  探测设备
 */
int i2c_probe(uint8_t dev_addr)
{
    int ret;

    ret = i2c_start_addr(dev_addr, I2C_Direction_Transmitter);
    i2c_release();

    return ret;
}

/**
 * @brief  总线恢复（从机把 SDA 拉低时手动发时钟解锁）
 */
int i2c_bus_recover(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    uint8_t i;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    /* 先切回普通 GPIO，由软件模拟时钟 */
    GPIO_InitStructure.GPIO_Pin   = I2C_SCL_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(I2C_GPIO, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(I2C_GPIO, &GPIO_InitStructure);

    if(GPIO_ReadInputDataBit(I2C_GPIO, I2C_SDA_PIN) == 0)      /* SDA 被拉低，需要解锁 */
    {
        GPIO_SetBits(I2C_GPIO, I2C_SCL_PIN);

        for(i = 0; i < 9; i++)
        {
            rt_hw_us_delay(10);
            GPIO_ResetBits(I2C_GPIO, I2C_SCL_PIN);
            rt_hw_us_delay(10);
            GPIO_SetBits(I2C_GPIO, I2C_SCL_PIN);

            if(GPIO_ReadInputDataBit(I2C_GPIO, I2C_SDA_PIN) != 0)
            {
                break;          /* 从机已经释放 SDA */
            }
        }

        if(GPIO_ReadInputDataBit(I2C_GPIO, I2C_SDA_PIN) == 0)
        {
            return I2C_ERR_TIMEOUT;     /* 无法恢复，可能硬件问题 */
        }

        /* 补一个 STOP 条件 */
        GPIO_InitStructure.GPIO_Pin  = I2C_SDA_PIN;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
        GPIO_Init(I2C_GPIO, &GPIO_InitStructure);

        GPIO_ResetBits(I2C_GPIO, I2C_SDA_PIN);
        rt_hw_us_delay(10);
        GPIO_SetBits(I2C_GPIO, I2C_SCL_PIN);
        rt_hw_us_delay(10);
        GPIO_SetBits(I2C_GPIO, I2C_SDA_PIN);
    }

    /* 恢复为 I2C 复用功能，重新初始化外设 */
    GPIO_InitStructure.GPIO_Pin  = I2C_SCL_PIN | I2C_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_Init(I2C_GPIO, &GPIO_InitStructure);

    return i2c_init(I2C_DEFAULT_SPEED);
}

/**
 * @brief  扫描 I2C 总线
 */
void i2c_scan(void)
{
    uint8_t found = 0;

    rt_kprintf("i2c: scanning 0x01~0x7F ...\r\n");

    for(uint8_t addr = 0x01; addr < 0x80; addr++)
    {
        if(i2c_probe(addr) == I2C_OK)
        {
            rt_kprintf("i2c: found 0x%02X\r\n", addr);
            found++;
        }
    }

    if(found == 0)
    {
        rt_kprintf("i2c: no device found\r\n");
    }
}
