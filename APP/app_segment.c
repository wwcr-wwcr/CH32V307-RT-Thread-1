#include "app_segment.h"
#include "ch32v30x.h"
#include "app_ntc.h"
#include <rtthread.h>
#include <finsh.h>

/* 硬件接口。 */
#define SEG_GPIO          GPIOE
#define SEG_SCL_PIN       GPIO_Pin_0
#define SEG_SDA_PIN       GPIO_Pin_1

/* CH422G系统配置命令。 */
#define SEG_SYSTEM_CMD    0x48U

/* 按实际接线排列的四位数码管写命令。 */
#define SEG_DIGIT1_CMD    0x70U
#define SEG_DIGIT2_CMD    0x72U
#define SEG_DIGIT3_CMD    0x74U
#define SEG_DIGIT4_CMD    0x76U

/* 点亮小数点需要设置的位。 */
#define SEG_POINT         0x20U
#define SEG_BLANK    0x00U  /* 全部段熄灭，显示空白 */
#define SEG_MINUS    0x08U  /* 当前段码接线中的中间横段，显示负号 */

/* 数字0～9的段码，沿用原TASK01已经使用的表。 */
static const uint8_t segment_code[10] =
{
    0xD7,     /* 0 */
    0x14,     /* 1 */
    0xCD,     /* 2 */
    0x5D,     /* 3 */
    0x1E,     /* 4 */
    0x5B,     /* 5 */
    0xDB,     /* 6 */
    0x15,     /* 7 */
    0xDF,     /* 8 */
    0x5F      /* 9 */
};

/*
 * 作用：为通信电平变化留出时间。
 * 沿用原工程的循环延时，不代表精确的微秒数。
 */
static void segment_delay(void)
{
    volatile uint32_t i;

    for (i = 0; i < 120U; i++)
    {
        __asm volatile ("nop");
    }
}

/* 开始：SCL为高时，让SDA从高变低。 */
static void segment_start(void)
{
    GPIO_SetBits(SEG_GPIO, SEG_SCL_PIN | SEG_SDA_PIN);
    segment_delay();

    GPIO_ResetBits(SEG_GPIO, SEG_SDA_PIN);
    segment_delay();

    GPIO_ResetBits(SEG_GPIO, SEG_SCL_PIN);
}

/* 停止：SCL为高时，让SDA从低变高。 */
static void segment_stop(void)
{
    GPIO_ResetBits(SEG_GPIO, SEG_SCL_PIN | SEG_SDA_PIN);
    segment_delay();

    GPIO_SetBits(SEG_GPIO, SEG_SCL_PIN);
    segment_delay();

    GPIO_SetBits(SEG_GPIO, SEG_SDA_PIN);
    segment_delay();
}

/*
 * 作用：按最高位在前的顺序发送8位数据。
 * value：需要发送的一个字节。
 */
static void segment_write_byte(uint8_t value)
{
    uint8_t i;

    for (i = 0; i < 8U; i++)
    {
        /* 检查当前最高位，设置数据线。 */
        if ((value & 0x80U) != 0U)
        {
            GPIO_SetBits(SEG_GPIO, SEG_SDA_PIN);
        }
        else
        {
            GPIO_ResetBits(SEG_GPIO, SEG_SDA_PIN);
        }

        segment_delay();

        /* 数据准备好后，产生一个时钟脉冲。 */
        GPIO_SetBits(SEG_GPIO, SEG_SCL_PIN);
        segment_delay();

        GPIO_ResetBits(SEG_GPIO, SEG_SCL_PIN);

        /* 把下一位移到最高位，准备下一次发送。 */
        value <<= 1;
    }

    /*
     * 沿用原驱动：发送第9个时钟，不读取ACK。
     * 因此发送完成不等于已经确认芯片收到。
     */
    GPIO_SetBits(SEG_GPIO, SEG_SDA_PIN);
    segment_delay();

    GPIO_SetBits(SEG_GPIO, SEG_SCL_PIN);
    segment_delay();

    GPIO_ResetBits(SEG_GPIO, SEG_SCL_PIN);
    segment_delay();
}

/*
 * 作用：向CH422G发送一条写命令和一个数据字节。
 * command：系统配置命令或某一位的写命令。
 * data：配置值或数码管段码。
 */
static void segment_write(uint8_t command, uint8_t data)
{
    segment_start();

    segment_write_byte(command);
    segment_write_byte(data);

    segment_stop();
}

void app_segment_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);

    /* 预先设置空闲电平，再切换为输出。 */
    GPIO_SetBits(SEG_GPIO, SEG_SCL_PIN | SEG_SDA_PIN);

    gpio.GPIO_Pin = SEG_SCL_PIN | SEG_SDA_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(SEG_GPIO, &gpio);

    /* 沿用原TASK01的CH422G系统配置。 */
    segment_write(SEG_SYSTEM_CMD, 0x05U);

    /* 初始化完成后显示00.00。 */
    app_segment_display(0, 0);
}

int app_segment_display(uint8_t left, uint8_t right)
{
    uint8_t digit1;
    uint8_t digit2;
    uint8_t digit3;
    uint8_t digit4;

    /* 防止拆出的数字超过段码表下标范围。 */
    if ((left > 99U) || (right > 99U))
    {
        return -1;
    }

    /* 拆分两组数字的十位和个位。 */
    digit1 = left / 10U;
    digit2 = left % 10U;

    digit3 = right / 10U;
    digit4 = right % 10U;

    /* 查询段码，并写到对应显示位置。 */
    segment_write(SEG_DIGIT1_CMD, segment_code[digit1]);
    segment_write(SEG_DIGIT2_CMD, segment_code[digit2] | SEG_POINT);
    segment_write(SEG_DIGIT3_CMD, segment_code[digit3]);
    segment_write(SEG_DIGIT4_CMD, segment_code[digit4]);

    return 0;
}

int app_segment_display_integer(uint16_t value)
{
    if (value > 9999U) return -1;

    /* 四位全部写入，不附加SEG_POINT，因此不会显示中间小数点。 */
    segment_write(SEG_DIGIT1_CMD, segment_code[value / 1000U]);
    segment_write(SEG_DIGIT2_CMD, segment_code[(value / 100U) % 10U]);
    segment_write(SEG_DIGIT3_CMD, segment_code[(value / 10U) % 10U]);
    segment_write(SEG_DIGIT4_CMD, segment_code[value % 10U]);
    return 0;
}

int app_segment_display_temperature(int32_t temperature_x10)
{
    uint16_t magnitude;
    uint8_t display[4];

    /* 四位数码管需要为小数和负号预留位置。 */
    if ((temperature_x10 < -999) || (temperature_x10 > 9999))
    {
        return -1;
    }

    /* 先取绝对值，方便拆分数字；负号稍后单独处理。 */
    if (temperature_x10 < 0)
    {
        magnitude = (uint16_t)(-temperature_x10);
    }
    else
    {
        magnitude = (uint16_t)temperature_x10;
    }

    /* 将温度乘10后的整数拆成四位，再查段码表。 */
    display[0] = segment_code[magnitude / 1000U];
    display[1] = segment_code[(magnitude / 100U) % 10U];
    display[2] = segment_code[(magnitude / 10U) % 10U] | SEG_POINT;
    display[3] = segment_code[magnitude % 10U];

    /* 隐藏不需要的前导零，个位的0仍然保留。 */
    if (magnitude < 1000U)
    {
        display[0] = SEG_BLANK;
    }

    if (magnitude < 100U)
    {
        display[1] = SEG_BLANK;
    }

    /* 负号放在有效数字前面。 */
    if (temperature_x10 < 0)
    {
        if (magnitude >= 100U)
        {
            display[0] = SEG_MINUS;
        }
        else
        {
            display[1] = SEG_MINUS;
        }
    }

    /* 四位全部重新写入，清除上次留下的数字或小数点。 */
    segment_write(SEG_DIGIT1_CMD, display[0]);
    segment_write(SEG_DIGIT2_CMD, display[1]);
    segment_write(SEG_DIGIT3_CMD, display[2]);
    segment_write(SEG_DIGIT4_CMD, display[3]);

    return 0;
}

int app_segment_display_light(uint32_t lux_x10)
{
    uint16_t lux;
    uint8_t display[4];

    /* 9999.5lux会四舍五入为10000，超出四位显示范围。 */
    if (lux_x10 >= 99995UL)
    {
        return -1;
    }

    /* 照度乘10的整数，四舍五入为整数lux。 */
    lux = (uint16_t)((lux_x10 + 5U) / 10U);

    /* 拆分千、百、十、个位，并查询段码。 */
    display[0] = segment_code[lux / 1000U];
    display[1] = segment_code[(lux / 100U) % 10U];
    display[2] = segment_code[(lux / 10U) % 10U];
    display[3] = segment_code[lux % 10U];

    /* 隐藏前导零，个位始终保留。 */
    if (lux < 1000U)
    {
        display[0] = SEG_BLANK;
    }

    if (lux < 100U)
    {
        display[1] = SEG_BLANK;
    }

    if (lux < 10U)
    {
        display[2] = SEG_BLANK;
    }

    /* 四位全部重写，不附加SEG_POINT，清除原有小数点。 */
    segment_write(SEG_DIGIT1_CMD, display[0]);
    segment_write(SEG_DIGIT2_CMD, display[1]);
    segment_write(SEG_DIGIT3_CMD, display[2]);
    segment_write(SEG_DIGIT4_CMD, display[3]);

    return 0;
}

/* 串口用法：segment_test 1234；仅用于数码管独立测试。 */
static void segment_test(int argc, char **argv)
{
    const char *text;
    uint32_t value = 0U;

    if (argc != 2) goto usage;
    text = argv[1];
    if (*text == '\0') goto usage;

    while (*text != '\0')
    {
        if (*text < '0' || *text > '9') goto usage;
        value = value * 10U + (uint32_t)(*text - '0');
        if (value > 9999U) goto usage;
        text++;
    }

    app_segment_init();
    (void)app_segment_display_integer((uint16_t)value);
    return;

usage:
    rt_kprintf("Usage: segment_test <0-9999>\r\n");
}
MSH_CMD_EXPORT(segment_test, Test four digit segment display);
