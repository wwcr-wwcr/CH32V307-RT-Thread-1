#include "app_light.h"
#include "ch32v30x.h"
#include <rtthread.h>
#include "rthw.h"

#define LIGHT_I2C           I2C1
#define LIGHT_ADDRESS       0x10U
#define LIGHT_TIMEOUT_MS    100

void app_light_bus_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    I2C_InitTypeDef i2c = {0};

    /* GPIOB和引脚重映射需要APB2时钟。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

    /* I2C1外设位于APB1。 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    /* 将I2C1映射到PB8=SCL、PB9=SDA。 */
    GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE);

    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    gpio.GPIO_Mode = GPIO_Mode_AF_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    I2C_DeInit(LIGHT_I2C);

    i2c.I2C_ClockSpeed = 100000U;//通信速度
    i2c.I2C_Mode = I2C_Mode_I2C;//选择协议模式
    i2c.I2C_DutyCycle = I2C_DutyCycle_2;//快速模式的时钟高低时间比例(SCL低电平时间 : 高电平时间 = 2 : 1)
    i2c.I2C_OwnAddress1 = 0x00U;//单片机的地址
    i2c.I2C_Ack = I2C_Ack_Enable;//允许本机发送应答
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;//本机地址的位数

    I2C_Init(LIGHT_I2C, &i2c);
    I2C_Cmd(LIGHT_I2C, ENABLE);
}

/*
 * 作用：等待I2C硬件完成指定阶段。
 * event：需要等待的事件。
 * 返回：0成功，-1表示无应答或超时。
 */
static int app_light_wait_event(uint32_t event)
{
    rt_tick_t start = rt_tick_get();

    while (I2C_CheckEvent(LIGHT_I2C, event) != READY)
    {
        /* AF表示应答失败，例如地址发送后没有收到ACK。 */
        if (I2C_GetFlagStatus(LIGHT_I2C, I2C_FLAG_AF) != RESET)
        {
            return -1;
        }

        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(LIGHT_TIMEOUT_MS))
        {
            return -1;
        }

        rt_thread_mdelay(1);
    }

    return 0;
}

int app_light_probe(void)
{
    /* 已经有通信占用总线时，本次不启动。 */
    if (I2C_GetFlagStatus(LIGHT_I2C, I2C_FLAG_BUSY) != RESET)
    {
        return -1;
    }

    /* 清除上一次可能留下的应答失败标志。 */
    I2C_ClearFlag(LIGHT_I2C, I2C_FLAG_AF);

    /* 第一步：请求硬件产生开始信号。 */
    I2C_GenerateSTART(LIGHT_I2C, ENABLE);

    if (app_light_wait_event(I2C_EVENT_MASTER_MODE_SELECT) != 0)
    {
        I2C_GenerateSTOP(LIGHT_I2C, ENABLE);
        I2C_ClearFlag(LIGHT_I2C, I2C_FLAG_AF);
        return -1;
    }

    /* 第二步：发送设备地址，选择写方向。 */
    I2C_Send7bitAddress(LIGHT_I2C, LIGHT_ADDRESS << 1U, I2C_Direction_Transmitter);

    /* 等待地址被应答，并进入主机发送状态。 */
    if (app_light_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) != 0)
    {
        I2C_GenerateSTOP(LIGHT_I2C, ENABLE);
        I2C_ClearFlag(LIGHT_I2C, I2C_FLAG_AF);
        return -1;
    }

    /* 本次只检测地址，不发送寄存器数据。 */
    I2C_GenerateSTOP(LIGHT_I2C, ENABLE);

    return 0;
}

/*
 * 作用：向VEML7700的一个寄存器写入16位数据。
 * reg：寄存器地址。
 * value：需要写入的16位配置值。
 * 返回：0成功，-1表示总线忙、无应答或超时。
 */
static int app_light_write_register(uint8_t reg, uint16_t value)
{
    uint8_t bytes[3];
    uint8_t i;
    rt_tick_t start;

    /* VEML7700的数据顺序：低字节在前，高字节在后。 */
    bytes[0] = reg;
    bytes[1] = (uint8_t)(value & 0xFFU);
    bytes[2] = (uint8_t)(value >> 8U);

    /* 等待前一次STOP完成、总线恢复空闲。 */
    start = rt_tick_get();

    while (I2C_GetFlagStatus(LIGHT_I2C, I2C_FLAG_BUSY) != RESET)
    {
        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(LIGHT_TIMEOUT_MS))
        {
            return -1;
        }

        rt_thread_mdelay(1);
    }

    I2C_ClearFlag(LIGHT_I2C, I2C_FLAG_AF);

    /* 发出开始信号。 */
    I2C_GenerateSTART(LIGHT_I2C, ENABLE);

    if (app_light_wait_event(I2C_EVENT_MASTER_MODE_SELECT) != 0)
    {
        I2C_GenerateSTOP(LIGHT_I2C, ENABLE);
        I2C_ClearFlag(LIGHT_I2C, I2C_FLAG_AF);
        return -1;
    }

    /* 选择VEML7700，进入写方向。 */
    I2C_Send7bitAddress(LIGHT_I2C, LIGHT_ADDRESS << 1U, I2C_Direction_Transmitter);

    if (app_light_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) != 0)
    {
        I2C_GenerateSTOP(LIGHT_I2C, ENABLE);
        I2C_ClearFlag(LIGHT_I2C, I2C_FLAG_AF);
        return -1;
    }

    /* 依次发送：寄存器地址、低字节、高字节。 */
    for (i = 0; i < 3U; i++)
    {
        I2C_SendData(LIGHT_I2C, bytes[i]);

        /* 等待本字节发送完成并收到应答。 */
        if (app_light_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED) != 0)
        {
            I2C_GenerateSTOP(LIGHT_I2C, ENABLE);
            I2C_ClearFlag(LIGHT_I2C, I2C_FLAG_AF);
            return -1;
        }
    }

    I2C_GenerateSTOP(LIGHT_I2C, ENABLE);

    return 0;
}

int app_light_init(void)
{
    app_light_bus_init();

    /*
     * 沿用参考工程：
     * 光照增益1/8、积分时间400ms、关闭中断、开启测量。
     */
    if (app_light_write_register(0x00U, 0x1080U) != 0)
    {
        return -1;
    }

    /* 关闭省电模式。 */
    if (app_light_write_register(0x03U, 0x0000U) != 0)
    {
        return -1;
    }

    return 0;
}

/*
 * 等待一个标志位达到指定状态。
 * flag：需要检查的标志。
 * expected：期望SET还是RESET。
 */
static int app_light_wait_flag(uint32_t flag, FlagStatus expected)
{
    rt_tick_t start = rt_tick_get();//rt_tick_t 是 RT-Thread 用于保存系统节拍数的类型

    while (I2C_GetFlagStatus(LIGHT_I2C, flag) != expected)
    {
        if (I2C_GetFlagStatus(LIGHT_I2C, I2C_FLAG_AF) != RESET)   /* AF 在这里表示 Acknowledge Failure，应答失败。 */
        {
            return -1;
        }

        if ((rt_tick_t)(rt_tick_get() - start) >= rt_tick_from_millisecond(LIGHT_TIMEOUT_MS))
        {
            return -1;
        }

        rt_thread_mdelay(1);
    }

    return 0;
}

int app_light_read_raw(uint16_t *raw)
{
    uint8_t low;
    uint8_t high;
    volatile uint32_t dummy;
    rt_base_t level;

    if (raw == 0)
    {
        return -1;
    }

    /* 等待前一次通信结束。 */
    if (app_light_wait_flag(I2C_FLAG_BUSY, RESET) != 0)
    {
        return -1;
    }

    I2C_ClearFlag(LIGHT_I2C, I2C_FLAG_AF);

    /* 准备两字节接收：NACK作用于下一字节。 */
    I2C_AcknowledgeConfig(LIGHT_I2C, ENABLE);
    I2C_NACKPositionConfig(LIGHT_I2C, I2C_NACKPosition_Next);

    /* 第一阶段：写入要读取的寄存器地址。 */
    I2C_GenerateSTART(LIGHT_I2C, ENABLE);

    if (app_light_wait_event(I2C_EVENT_MASTER_MODE_SELECT) != 0)
    {
        goto failed;
    }

    I2C_Send7bitAddress(LIGHT_I2C, LIGHT_ADDRESS << 1U, I2C_Direction_Transmitter);

    if (app_light_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) != 0)
    {
        goto failed;
    }

    /* 选择光照数据寄存器0x04。 */
    I2C_SendData(LIGHT_I2C, 0x04U);

    if (app_light_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED) != 0)
    {
        goto failed;
    }

    /* 第二阶段：不发STOP，直接重复START切换到读方向。 */
    I2C_GenerateSTART(LIGHT_I2C, ENABLE);

    if (app_light_wait_event(I2C_EVENT_MASTER_MODE_SELECT) != 0)
    {
        goto failed;
    }

    I2C_Send7bitAddress(LIGHT_I2C, LIGHT_ADDRESS << 1U, I2C_Direction_Receiver);

    /* 这里只等ADDR，不通过CheckEvent提前清除它。 */
    if (app_light_wait_flag(I2C_FLAG_ADDR, SET) != 0)
    {
        goto failed;
    }

    /*
     * 两字节接收的关键顺序：
     * 清除ADDR，然后关闭ACK，让最后一个字节以NACK结束。
     * 短暂屏蔽中断，避免这几个操作之间被打断。
     */
    level = rt_hw_interrupt_disable();

    dummy = LIGHT_I2C->STAR1;
    dummy = LIGHT_I2C->STAR2;
    I2C_AcknowledgeConfig(LIGHT_I2C, DISABLE);

    rt_hw_interrupt_enable(level);
    (void)dummy;

    /* 等待两个字节分别进入数据寄存器和移位寄存器。 */
    if (app_light_wait_flag(I2C_FLAG_BTF, SET) != 0)
    {
        goto failed;
    }

    /* 两字节已经到达，结束通信并依次取出。 */
    level = rt_hw_interrupt_disable();

    I2C_GenerateSTOP(LIGHT_I2C, ENABLE);
    low = I2C_ReceiveData(LIGHT_I2C);
    high = I2C_ReceiveData(LIGHT_I2C);

    rt_hw_interrupt_enable(level);

    /* 恢复默认接收设置，供后续通信使用。 */
    I2C_NACKPositionConfig(LIGHT_I2C, I2C_NACKPosition_Current);
    I2C_AcknowledgeConfig(LIGHT_I2C, ENABLE);

    /* VEML7700先发送低字节，再发送高字节。 */
    *raw = (uint16_t)low | ((uint16_t)high << 8U);

    return 0;

failed:
    /* 任一阶段失败，都在这里统一结束本次通信。 */
    I2C_GenerateSTOP(LIGHT_I2C, ENABLE);
    I2C_ClearFlag(LIGHT_I2C, I2C_FLAG_AF);
    I2C_NACKPositionConfig(LIGHT_I2C, I2C_NACKPosition_Current);
    I2C_AcknowledgeConfig(LIGHT_I2C, ENABLE);

    return -1;
}

int app_light_get_lux_x10(uint32_t *lux_x10)
{
    uint16_t raw;

    if (lux_x10 == 0)
    {
        return -1;
    }

    /* 复用已经验收的原始值读取函数。 */
    if (app_light_read_raw(&raw) != 0)
    {
        return -1;
    }

    /* lux × 10 = raw × 1.344，使用整数运算并四舍五入。 */
    *lux_x10 = ((uint32_t)raw * 1344UL + 500UL) / 1000UL;

    return 0;
}