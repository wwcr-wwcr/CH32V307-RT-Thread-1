/******************************************************************************
 * 文件名   : ch422.c
 * 芯片     : CH32V307VCT6
 * 说明     : CH422G 数码管驱动（软件 I2C）
 *
 * 说明: 上层只管调 ch422_show_number() / ch422_show_digit()，
 *       段码、位选、命令全部在这里拼好。
 ******************************************************************************/

#include "ch422.h"
#include "rthw.h"

/**************************************************************************************************
 *                                        参数
 **************************************************************************************************/
#define CH422_HALF_DELAY_US     2

/**************************************************************************************************
 *                                        全局变量
 **************************************************************************************************/
/*                       0     1     2     3     4     5     6     7     8     9   小数点  空白 */
const uint8_t bcdDecode[12] = {0xD7, 0x14, 0xCD, 0x5D, 0x1E, 0x5B, 0xDB, 0x15, 0xDF, 0x5F, 0x20, 0x00};

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* 起始条件 */
static void ch422_i2c_start(void)
{
    ch422_sda_output();
    CH422_SCL_1();
    CH422_SDA_1();
    rt_hw_us_delay(CH422_HALF_DELAY_US);
    CH422_SDA_0();
    rt_hw_us_delay(CH422_HALF_DELAY_US);
    CH422_SCL_0();
}

/* 停止条件 */
static void ch422_i2c_stop(void)
{
    ch422_sda_output();
    CH422_SDA_0();
    rt_hw_us_delay(CH422_HALF_DELAY_US);
    CH422_SCL_1();
    rt_hw_us_delay(CH422_HALF_DELAY_US);
    CH422_SDA_1();
    rt_hw_us_delay(CH422_HALF_DELAY_US);
}

/* 发送一个字节 */
static void ch422_i2c_send_byte(uint8_t data)
{
    ch422_sda_output();

    for(uint8_t i = 0; i < 8; i++)
    {
        if(data & 0x80)
        {
            CH422_SDA_1();
        }
        else
        {
            CH422_SDA_0();
        }
        rt_hw_us_delay(CH422_HALF_DELAY_US);
        CH422_SCL_1();
        data <<= 1;
        rt_hw_us_delay(CH422_HALF_DELAY_US);
        CH422_SCL_0();
    }

    /* 第 9 个时钟：CH422 不需要判 ACK，给一个时钟即可 */
    CH422_SDA_1();
    rt_hw_us_delay(CH422_HALF_DELAY_US);
    CH422_SCL_1();
    rt_hw_us_delay(CH422_HALF_DELAY_US);
    CH422_SCL_0();
}

/* 接收一个字节 */
static uint8_t ch422_i2c_read_byte(void)
{
    uint8_t data = 0;

    CH422_SDA_1();
    ch422_sda_input();

    for(uint8_t i = 0; i < 8; i++)
    {
        rt_hw_us_delay(CH422_HALF_DELAY_US);
        CH422_SCL_1();
        rt_hw_us_delay(CH422_HALF_DELAY_US);
        data <<= 1;
        if(CH422_READ_SDA())
        {
            data++;
        }
        CH422_SCL_0();
    }

    ch422_sda_output();
    CH422_SDA_1();
    rt_hw_us_delay(CH422_HALF_DELAY_US);
    CH422_SCL_1();
    rt_hw_us_delay(CH422_HALF_DELAY_US);
    CH422_SCL_0();

    return data;
}

/* 位号转位选地址：0->0x70 1->0x72 2->0x74 3->0x76 */
static uint8_t ch422_pos_addr(uint8_t pos)
{
    return (uint8_t)(CH422_ADDRESS_1 + (pos * 2));
}

/* 段码按共阴/共阳做转换 */
static uint8_t ch422_seg_convert(uint8_t seg)
{
#if (CH422_SEG_INVERT)
    return (uint8_t)(~seg);
#else
    return seg;
#endif
}

/**************************************************************************************************
 *                                        宝宝函数
 **************************************************************************************************/
/**
 * @brief  初始化数码管
 */
int ch422_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);

    /* SCL 由主机单向驱动，用推挽输出；SDA 需要读回，用开漏输出 */
    GPIO_InitStructure.GPIO_Pin   = CH422_SCL_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    GPIO_ResetBits(GPIOE, CH422_SCL_PIN | CH422_SDA_PIN);
    ch422_sda_output();

    CH422_SCL_1();
    CH422_SDA_1();

    /* 系统配置：IO 输出 + 动态自动扫描 */
    if(ch422_write_bytes(CH422_SYS_CMD, CH422_SYS_DEFAULT) != 0)
    {
        return -1;
    }

    /* 上电先清屏，避免显示随机段 */
    return ch422_clear();
}

/**
 * @brief  显示一个 0~9999 的整数
 */
int ch422_show_number(uint16_t value)
{
    uint16_t divisor = 1;
    uint16_t max_value;
    uint8_t  pos;

    for(pos = 1; pos < CH422_DIGIT_COUNT; pos++)
    {
        divisor *= 10;
    }
    max_value = (uint16_t)(divisor * 10 - 1);

    if(value > max_value)
    {
        value = max_value;          /* 超出位数直接饱和，避免显示乱码 */
    }

    for(pos = 0; pos < CH422_DIGIT_COUNT; pos++)
    {
        uint8_t digit = (uint8_t)((value / divisor) % 10);

        /* 前导零不显示，但最后一位的 0 要显示 */
        if((value / divisor) == 0 && pos < (CH422_DIGIT_COUNT - 1))
        {
            (void)ch422_show_seg(pos, CH422_SEG_BLANK);
        }
        else
        {
            (void)ch422_show_digit(pos, digit);
        }

        divisor /= 10;
    }

    return 0;
}

/**
 * @brief  显示带小数点的数
 */
int ch422_show_number_dp(uint16_t value, uint8_t dp_index)
{
    uint16_t divisor = 1;
    uint16_t max_value;
    uint8_t  pos;

    if(dp_index < 1 || dp_index > CH422_DIGIT_COUNT)
    {
        return -1;
    }

    for(pos = 1; pos < CH422_DIGIT_COUNT; pos++)
    {
        divisor *= 10;
    }
    max_value = (uint16_t)(divisor * 10 - 1);

    if(value > max_value)
    {
        value = max_value;
    }

    for(pos = 0; pos < CH422_DIGIT_COUNT; pos++)
    {
        uint8_t digit = (uint8_t)((value / divisor) % 10);
        uint8_t dp_on = (pos == (uint8_t)(dp_index - 1)) ? 1 : 0;

        if((value / divisor) == 0 && pos < (CH422_DIGIT_COUNT - 1))
        {
            /* 前导零熄灭，但小数点所在的那一位要保留小数点 */
            (void)ch422_show_seg(pos, dp_on ? CH422_SEG_DP : CH422_SEG_BLANK);
        }
        else
        {
            (void)ch422_show_digit_dp(pos, digit, dp_on);
        }

        divisor /= 10;
    }

    return 0;
}

/**
 * @brief  显示带符号整数
 */
int ch422_show_int(int16_t value)
{
    uint16_t divisor = 100;         /* 负号占一位，剩下 3 位数字 */
    int32_t  mag;
    uint8_t  pos;

    if(value >= 0)
    {
        return ch422_show_number((uint16_t)value);
    }

    mag = -(int32_t)value;
    if(mag > 999)
    {
        mag = 999;                  /* 负号占一位，最多显示 -999 */
    }

    (void)ch422_show_char(0, '-');

    for(pos = 1; pos < CH422_DIGIT_COUNT; pos++)
    {
        uint8_t digit = (uint8_t)((mag / divisor) % 10);

        if((mag / divisor) == 0 && pos < (CH422_DIGIT_COUNT - 1))
        {
            (void)ch422_show_seg(pos, CH422_SEG_BLANK);
        }
        else
        {
            (void)ch422_show_digit(pos, digit);
        }

        divisor /= 10;
    }

    return 0;
}

/**
 * @brief  某一位显示数字
 */
int ch422_show_digit(uint8_t pos, uint8_t digit)
{
    if(digit > 9)
    {
        return -1;
    }

    return ch422_show_seg(pos, ch422_bcd(digit));
}

/**
 * @brief  某一位显示数字 + 小数点
 */
int ch422_show_digit_dp(uint8_t pos, uint8_t digit, uint8_t dp_on)
{
    uint8_t seg;

    if(digit > 9)
    {
        return -1;
    }

    seg = ch422_bcd(digit);
    if(dp_on)
    {
        seg |= CH422_SEG_DP;
    }

    return ch422_show_seg(pos, seg);
}

/**
 * @brief  某一位显示字符
 */
int ch422_show_char(uint8_t pos, char ch)
{
    uint8_t seg;

    if(ch >= '0' && ch <= '9')
    {
        seg = ch422_bcd((uint8_t)(ch - '0'));
    }
    else if(ch == '-')
    {
        seg = CH422_SEG_MINUS;
    }
    else if(ch == ' ')
    {
        seg = CH422_SEG_BLANK;
    }
    else
    {
        return -1;
    }

    return ch422_show_seg(pos, seg);
}

/**
 * @brief  四位全灭
 */
int ch422_clear(void)
{
    for(uint8_t pos = 0; pos < CH422_DIGIT_COUNT; pos++)
    {
        if(ch422_show_seg(pos, CH422_SEG_BLANK) != 0)
        {
            return -1;
        }
    }

    return 0;
}

/**
 * @brief  四位全亮（自检）
 */
int ch422_all_on(void)
{
    for(uint8_t pos = 0; pos < CH422_DIGIT_COUNT; pos++)
    {
        if(ch422_show_seg(pos, CH422_SEG_ALL) != 0)
        {
            return -1;
        }
    }

    return 0;
}

/**************************************************************************************************
 *                                        高级接口
 **************************************************************************************************/
/**
 * @brief  某一位直接写段码
 */
int ch422_show_seg(uint8_t pos, uint8_t seg)
{
    if(pos >= CH422_DIGIT_COUNT)
    {
        return -1;
    }

    return ch422_write_bytes(ch422_pos_addr(pos), ch422_seg_convert(seg));
}

/**
 * @brief  数字转段码
 */
uint8_t ch422_bcd(uint8_t digit)
{
    if(digit > 9)
    {
        return bcdDecode[11];       /* 空白 */
    }

    return bcdDecode[digit];
}

/**
 * @brief  通用两字节写
 */
int ch422_write_bytes(uint8_t cmd, uint8_t data)
{
    ch422_i2c_start();
    ch422_i2c_send_byte(cmd);
    ch422_i2c_send_byte(data);
    ch422_i2c_stop();

    return 0;
}

/**
 * @brief  系统配置
 */
int ch422_system_config(uint8_t config)
{
    return ch422_write_bytes(CH422_SYS_CMD, config);
}

/**
 * @brief  读 IO 状态
 */
uint8_t ch422_read_io(void)
{
    uint8_t data;

    ch422_i2c_start();
    ch422_i2c_send_byte(CH422_RD_IO_CMD);
    data = ch422_i2c_read_byte();
    ch422_i2c_stop();

    return data;
}

/**
 * @brief  SDA 切为输入
 */
void ch422_sda_input(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.GPIO_Pin   = CH422_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(CH422_SDA_GPIO, &GPIO_InitStructure);
}

/**
 * @brief  SDA 切为开漏输出
 */
void ch422_sda_output(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.GPIO_Pin   = CH422_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(CH422_SDA_GPIO, &GPIO_InitStructure);
}
