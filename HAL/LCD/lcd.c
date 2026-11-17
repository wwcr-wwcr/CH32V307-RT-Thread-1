/******************************************************************************
 * 文件名   : lcd.c
 * 芯片     : CH32V307VCT6
 * 说明     : ST7796S 320x480 TFT 液晶驱动（FSMC 8080 并口）
 ******************************************************************************/

#include "lcd.h"
#include "lcdfont.h"
#include "rtthread.h"
#include "stdio.h"
#include "string.h"
#include "stdarg.h"

/**************************************************************************************************
 *                                        全局变量
 **************************************************************************************************/
_lcd_dev lcddev;

static uint8_t s_lcd_ready = 0;

/**************************************************************************************************
 *                                        底层读写
 **************************************************************************************************/
/**
 * @brief  写命令
 */
void lcd_write_cmd(uint16_t cmd)
{
    LCD->LCD_REG = cmd;
}

/**
 * @brief  写数据
 */
void lcd_write_data(uint16_t data)
{
    LCD->LCD_RAM = data;
}

/**
 * @brief  连续写数据
 */
void lcd_write_data_buf(const uint16_t *buf, uint32_t len)
{
    if(buf == NULL)
    {
        return;
    }

    while(len--)
    {
        LCD->LCD_RAM = *buf++;
    }
}

/**
 * @brief  读数据
 */
uint16_t lcd_read_data(void)
{
    vu16 ram = LCD->LCD_RAM;        /* 用 volatile 变量接一下，防止被优化掉 */

    return ram;
}

/**
 * @brief  读寄存器
 */
uint16_t lcd_read_reg(uint16_t reg)
{
    lcd_write_cmd(reg);

    return lcd_read_data();
}

/**************************************************************************************************
 *                                        初始化相关
 **************************************************************************************************/
/* FSMC + GPIO 配置（16 位并口，A18 作为 RS） */
static void lcd_port_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    FSMC_NORSRAMInitTypeDef FSMC_NORSRAMInitStructure = {0};
    FSMC_NORSRAMTimingInitTypeDef readWriteTiming = {0};
    FSMC_NORSRAMTimingInitTypeDef writeTiming = {0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_FSMC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE, ENABLE);

    /* PC4 背光控制 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    GPIO_ResetBits(GPIOC, GPIO_Pin_4);                  /* 先关背光，避免上电花屏 */

    /* PD0/PD1/PD4/PD5/PD8~PD10/PD14/PD15 复用为 FSMC 数据/控制线 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_4 | GPIO_Pin_5 |
                                    GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    /* PE7~PE15 复用为 FSMC 数据线 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 |
                                    GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    /* RS:PD12 复用输出 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    /* CS:PD11，RST:PD13 普通推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_11 | GPIO_Pin_13;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOD, &GPIO_InitStructure);
    GPIO_ResetBits(GPIOD, GPIO_Pin_11);                 /* 片选常有效 */
    GPIO_SetBits(GPIOD, GPIO_Pin_13);                   /* 释放复位 */

    /* FSMC 时序：读慢写快，写周期 4 个 HCLK */
    readWriteTiming.FSMC_AddressSetupTime      = 0x01;
    readWriteTiming.FSMC_AddressHoldTime       = 0x00;
    readWriteTiming.FSMC_DataSetupTime         = 0x0F;
    readWriteTiming.FSMC_BusTurnAroundDuration = 0x00;
    readWriteTiming.FSMC_CLKDivision           = 0x00;
    readWriteTiming.FSMC_DataLatency           = 0x00;
    readWriteTiming.FSMC_AccessMode            = FSMC_AccessMode_A;

    writeTiming.FSMC_AddressSetupTime      = 0x00;
    writeTiming.FSMC_AddressHoldTime       = 0x00;
    writeTiming.FSMC_DataSetupTime         = 0x03;
    writeTiming.FSMC_BusTurnAroundDuration = 0x00;
    writeTiming.FSMC_CLKDivision           = 0x00;
    writeTiming.FSMC_DataLatency           = 0x00;
    writeTiming.FSMC_AccessMode            = FSMC_AccessMode_A;

    FSMC_NORSRAMInitStructure.FSMC_Bank                  = FSMC_Bank1_NORSRAM1;
    FSMC_NORSRAMInitStructure.FSMC_DataAddressMux        = FSMC_DataAddressMux_Disable;
    FSMC_NORSRAMInitStructure.FSMC_MemoryType            = FSMC_MemoryType_SRAM;
    FSMC_NORSRAMInitStructure.FSMC_MemoryDataWidth       = FSMC_MemoryDataWidth_16b;
    FSMC_NORSRAMInitStructure.FSMC_BurstAccessMode       = FSMC_BurstAccessMode_Disable;
    FSMC_NORSRAMInitStructure.FSMC_WaitSignalPolarity    = FSMC_WaitSignalPolarity_Low;
    FSMC_NORSRAMInitStructure.FSMC_AsynchronousWait      = FSMC_AsynchronousWait_Disable;
    FSMC_NORSRAMInitStructure.FSMC_WaitSignalActive      = FSMC_WaitSignalActive_BeforeWaitState;
    FSMC_NORSRAMInitStructure.FSMC_WriteOperation        = FSMC_WriteOperation_Enable;
    FSMC_NORSRAMInitStructure.FSMC_WaitSignal            = FSMC_WaitSignal_Disable;
    FSMC_NORSRAMInitStructure.FSMC_ExtendedMode          = FSMC_ExtendedMode_Enable;
    FSMC_NORSRAMInitStructure.FSMC_WriteBurst            = FSMC_WriteBurst_Disable;
    FSMC_NORSRAMInitStructure.FSMC_ReadWriteTimingStruct = &readWriteTiming;
    FSMC_NORSRAMInitStructure.FSMC_WriteTimingStruct     = &writeTiming;

    FSMC_NORSRAMInit(&FSMC_NORSRAMInitStructure);
    FSMC_NORSRAMCmd(FSMC_Bank1_NORSRAM1, ENABLE);
}

/* 硬件复位 */
static void lcd_reset(void)
{
    GPIO_ResetBits(GPIOD, GPIO_Pin_13);
    rt_thread_mdelay(10);
    GPIO_SetBits(GPIOD, GPIO_Pin_13);
    rt_thread_mdelay(120);
}

/* 读 ID（ST7796S 的 0xD3 指令） */
static uint16_t lcd_read_id(void)
{
    uint16_t id;

    lcd_write_cmd(0xD3);
    (void)lcd_read_data();              /* dummy read */
    (void)lcd_read_data();
    id  = (uint16_t)(lcd_read_data() << 8);
    id |= lcd_read_data();

    return id;
}

/* 面板初始化序列（ST7796S，320x480） */
static void lcd_panel_init(void)
{
    lcd_write_cmd(0x11);                                /* Sleep out */
    rt_thread_mdelay(120);

    lcd_write_cmd(0xF0);                                /* 解锁扩展命令 */
    lcd_write_data(0xC3);
    lcd_write_cmd(0xF0);
    lcd_write_data(0x96);

    lcd_set_dir(LCD_DFT_DIR);                           /* 0x36：扫描方向 + BGR */

    lcd_write_cmd(0x3A);                                /* 像素格式：16bit RGB565 */
    lcd_write_data(0x05);

    lcd_write_cmd(0xE8);                                /* 刷新率 */
    lcd_write_data(0x40);
    lcd_write_data(0x82);
    lcd_write_data(0x07);
    lcd_write_data(0x18);
    lcd_write_data(0x27);
    lcd_write_data(0x0A);
    lcd_write_data(0xB6);
    lcd_write_data(0x33);

    lcd_write_cmd(0xC5);                                /* VCOM */
    lcd_write_data(0x27);

    lcd_write_cmd(0xC2);                                /* 电源控制 */
    lcd_write_data(0xA7);

    lcd_write_cmd(0xE0);                                /* 正极性伽马 */
    lcd_write_data(0xF0);
    lcd_write_data(0x01);
    lcd_write_data(0x06);
    lcd_write_data(0x0F);
    lcd_write_data(0x12);
    lcd_write_data(0x1D);
    lcd_write_data(0x36);
    lcd_write_data(0x54);
    lcd_write_data(0x44);
    lcd_write_data(0x0C);
    lcd_write_data(0x18);
    lcd_write_data(0x16);
    lcd_write_data(0x13);
    lcd_write_data(0x15);

    lcd_write_cmd(0xE1);                                /* 负极性伽马 */
    lcd_write_data(0xF0);
    lcd_write_data(0x01);
    lcd_write_data(0x05);
    lcd_write_data(0x0A);
    lcd_write_data(0x0B);
    lcd_write_data(0x07);
    lcd_write_data(0x32);
    lcd_write_data(0x44);
    lcd_write_data(0x44);
    lcd_write_data(0x0C);
    lcd_write_data(0x18);
    lcd_write_data(0x17);
    lcd_write_data(0x13);
    lcd_write_data(0x16);

    lcd_write_cmd(0xF0);                                /* 重新锁定扩展命令 */
    lcd_write_data(0x3C);
    lcd_write_cmd(0xF0);
    lcd_write_data(0x69);

    lcd_write_cmd(0x29);                                /* 开显示 */
}

/**
 * @brief  初始化 LCD
 */
int lcd_init(void)
{
    lcd_port_init();
    lcd_reset();

    rt_thread_mdelay(50);
    lcd_set_dir(LCD_DFT_DIR);

    lcddev.id = lcd_read_id();
    rt_kprintf("lcd: id = 0x%04X\r\n", lcddev.id);

    lcd_panel_init();

    lcd_backlight(1);
    lcd_clear(LCD_COLOR_BLACK);

    s_lcd_ready = 1;

    /* ID 读不到通常是接线、片选或 FSMC 时序问题，这里给出提示但不阻塞 */
    if(lcddev.id == 0x0000 || lcddev.id == 0xFFFF)
    {
        rt_kprintf("lcd: id read failed, check wiring / FSMC timing\r\n");
        return -2;
    }
    if(lcddev.id != LCD_EXPECT_ID)
    {
        rt_kprintf("lcd: expect id 0x%04X, got 0x%04X\r\n", LCD_EXPECT_ID, lcddev.id);
    }

    return 0;
}

/**
 * @brief  设置扫描方向
 */
int lcd_set_dir(lcd_dir_t dir)
{
    /* 横屏还是竖屏：MADCTL 的 bit5/bit6 决定行列交换 */
    if(((dir >> 4) % 4) != 0)
    {
        lcddev.width  = LCD_PANEL_HEIGHT;   /* 480 */
        lcddev.height = LCD_PANEL_WIDTH;    /* 320 */
    }
    else
    {
        lcddev.width  = LCD_PANEL_WIDTH;    /* 320 */
        lcddev.height = LCD_PANEL_HEIGHT;   /* 480 */
    }

    if(s_lcd_ready || dir == LCD_DFT_DIR)
    {
        lcd_write_cmd(0x36);                        /* MADCTL：0x08 = BGR 顺序 */
        lcd_write_data((uint16_t)(0x08 | (uint8_t)dir));
    }

    return 0;
}

/**
 * @brief  开显示
 */
void lcd_display_on(void)
{
    lcd_write_cmd(0x29);
}

/**
 * @brief  关显示
 */
void lcd_display_off(void)
{
    lcd_write_cmd(0x28);
}

/**
 * @brief  背光控制
 */
void lcd_backlight(uint8_t on)
{
    if(on)
    {
        GPIO_SetBits(GPIOC, GPIO_Pin_4);
    }
    else
    {
        GPIO_ResetBits(GPIOC, GPIO_Pin_4);
    }
}

/**
 * @brief  当前宽度
 */
uint16_t lcd_width(void)
{
    return lcddev.width;
}

/**
 * @brief  当前高度
 */
uint16_t lcd_height(void)
{
    return lcddev.height;
}

/**
 * @brief  LCD ID
 */
uint16_t lcd_id(void)
{
    return lcddev.id;
}

/**************************************************************************************************
 *                                        窗口与绘图
 **************************************************************************************************/
/**
 * @brief  设置绘图窗口
 */
void lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    lcd_write_cmd(0x2A);                    /* 列地址 */
    lcd_write_data((uint16_t)(x1 >> 8));
    lcd_write_data((uint16_t)(x1 & 0xFF));
    lcd_write_data((uint16_t)(x2 >> 8));
    lcd_write_data((uint16_t)(x2 & 0xFF));

    lcd_write_cmd(0x2B);                    /* 行地址 */
    lcd_write_data((uint16_t)(y1 >> 8));
    lcd_write_data((uint16_t)(y1 & 0xFF));
    lcd_write_data((uint16_t)(y2 >> 8));
    lcd_write_data((uint16_t)(y2 & 0xFF));

    lcd_write_cmd(0x2C);                    /* 开始写显存 */
}

/**
 * @brief  设置光标
 */
void lcd_set_cursor(uint16_t x, uint16_t y)
{
    lcd_write_cmd(0x2A);
    lcd_write_data((uint16_t)(x >> 8));
    lcd_write_data((uint16_t)(x & 0xFF));

    lcd_write_cmd(0x2B);
    lcd_write_data((uint16_t)(y >> 8));
    lcd_write_data((uint16_t)(y & 0xFF));
}

/**
 * @brief  整屏填充
 */
void lcd_clear(uint16_t color)
{
    lcd_fill_area(0, 0, (uint16_t)(lcddev.width - 1), (uint16_t)(lcddev.height - 1), color);
}

/**
 * @brief  填充矩形（左上角 + 宽高）
 */
void lcd_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if(w == 0 || h == 0)
    {
        return;
    }

    lcd_fill_area(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1), color);
}

/**
 * @brief  填充矩形（首尾坐标）
 */
void lcd_fill_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint32_t total;

    if(x1 >= lcddev.width || y1 >= lcddev.height)
    {
        return;
    }
    if(x2 >= lcddev.width)
    {
        x2 = (uint16_t)(lcddev.width - 1);
    }
    if(y2 >= lcddev.height)
    {
        y2 = (uint16_t)(lcddev.height - 1);
    }
    if(x2 < x1 || y2 < y1)
    {
        return;
    }

    total = (uint32_t)(x2 - x1 + 1) * (uint32_t)(y2 - y1 + 1);

    lcd_set_window(x1, y1, x2, y2);
    while(total--)
    {
        lcd_write_data(color);
    }
}

/**
 * @brief  画点
 */
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_set_window(x, y, x, y);
    lcd_write_data(color);
}

/**
 * @brief  读点
 */
uint16_t lcd_read_point(uint16_t x, uint16_t y)
{
    uint16_t rgb;

    lcd_set_cursor(x, y);
    lcd_write_cmd(0x2E);

    (void)lcd_read_data();          /* dummy read */
    rgb = lcd_read_data();

    return rgb;
}

/**
 * @brief  画线（Bresenham）
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    int16_t dx = (int16_t)(x2 - x1);
    int16_t dy = (int16_t)(y2 - y1);
    int16_t sx = (dx >= 0) ? 1 : -1;
    int16_t sy = (dy >= 0) ? 1 : -1;
    int16_t err;
    int16_t x = (int16_t)x1;
    int16_t y = (int16_t)y1;

    if(dx < 0) { dx = -dx; }
    if(dy < 0) { dy = -dy; }

    err = (int16_t)(dx - dy);

    while(1)
    {
        lcd_draw_point((uint16_t)x, (uint16_t)y, color);

        if(x == (int16_t)x2 && y == (int16_t)y2)
        {
            break;
        }

        if((err << 1) > -dy)
        {
            err -= dy;
            x += sx;
        }
        if((err << 1) < dx)
        {
            err += dx;
            y += sy;
        }
    }
}

/**
 * @brief  画矩形边框（左上角 + 宽高）
 */
void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if(w == 0 || h == 0)
    {
        return;
    }

    lcd_draw_rect_area(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1), color);
}

/**
 * @brief  画矩形边框（首尾坐标）
 */
void lcd_draw_rect_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    lcd_draw_line(x1, y1, x2, y1, color);
    lcd_draw_line(x1, y1, x1, y2, color);
    lcd_draw_line(x1, y2, x2, y2, color);
    lcd_draw_line(x2, y1, x2, y2, color);
}

/**
 * @brief  画圆（空心）
 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    int16_t a = 0;
    int16_t b = (int16_t)r;
    int16_t d = (int16_t)(3 - 2 * r);

    while(a <= b)
    {
        lcd_draw_point((uint16_t)(x0 + b), (uint16_t)(y0 + a), color);
        lcd_draw_point((uint16_t)(x0 - b), (uint16_t)(y0 + a), color);
        lcd_draw_point((uint16_t)(x0 + b), (uint16_t)(y0 - a), color);
        lcd_draw_point((uint16_t)(x0 - b), (uint16_t)(y0 - a), color);
        lcd_draw_point((uint16_t)(x0 + a), (uint16_t)(y0 + b), color);
        lcd_draw_point((uint16_t)(x0 - a), (uint16_t)(y0 + b), color);
        lcd_draw_point((uint16_t)(x0 + a), (uint16_t)(y0 - b), color);
        lcd_draw_point((uint16_t)(x0 - a), (uint16_t)(y0 - b), color);

        if(d < 0)
        {
            d = (int16_t)(d + 4 * a + 6);
        }
        else
        {
            d = (int16_t)(d + 4 * (a - b) + 10);
            b--;
        }
        a++;
    }
}

/**************************************************************************************************
 *                                        字符显示
 **************************************************************************************************/
/* 取 ASCII 字模表：返回表首地址，同时给出宽、高和单字字节数 */
static const uint8_t *lcd_ascii_font(uint8_t size, uint8_t *w, uint8_t *h, uint16_t *bytes)
{
    switch(size)
    {
    case 12: *w = 6;  *h = 12; *bytes = 12; return (const uint8_t *)ascii_1206;
    case 16: *w = 8;  *h = 16; *bytes = 16; return (const uint8_t *)ascii_1608;
    case 24: *w = 12; *h = 24; *bytes = 48; return (const uint8_t *)ascii_2412;
    case 32: *w = 16; *h = 32; *bytes = 64; return (const uint8_t *)ascii_3216;
    default: return NULL;
    }
}

/**
 * @brief  显示单个 ASCII 字符
 */
void lcd_show_char(uint16_t x, uint16_t y, char ch, uint16_t fc, uint16_t bc,
                   uint8_t size, uint8_t mode)
{
    const uint8_t *font;
    uint8_t  w = 0, h = 0;
    uint16_t bytes = 0;
    uint16_t x0 = x;
    uint8_t  m = 0;
    uint8_t  temp;

    font = lcd_ascii_font(size, &w, &h, &bytes);
    if(font == NULL)
    {
        return;
    }

    if(ch < ' ' || ch > '~')
    {
        ch = ' ';
    }

    /* 字符在字库中的偏移：每个字符 bytes 个字节 */
    font += (uint32_t)(ch - ' ') * bytes;

    lcd_set_window(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));

    for(uint16_t i = 0; i < bytes; i++)
    {
        temp = font[i];

        for(uint8_t t = 0; t < 8; t++)
        {
            if(mode == 0)                       /* 非叠加：背景也一起写 */
            {
                lcd_write_data((temp & (0x01 << t)) ? fc : bc);
                m++;
                if(m % w == 0)
                {
                    m = 0;
                    break;
                }
            }
            else                                /* 叠加：只画前景点 */
            {
                if(temp & (0x01 << t))
                {
                    lcd_draw_point(x, y, fc);
                }
                x++;
                if((x - x0) == w)
                {
                    x = x0;
                    y++;
                    break;
                }
            }
        }
    }
}

/**
 * @brief  显示字符串
 */
void lcd_show_string(uint16_t x, uint16_t y, const char *str, uint16_t fc, uint16_t bc,
                     uint8_t size, uint8_t mode)
{
    if(str == NULL)
    {
        return;
    }

    while(*str != '\0')
    {
        lcd_show_char(x, y, *str, fc, bc, size, mode);
        x = (uint16_t)(x + size / 2);
        str++;
    }
}

/**
 * @brief  显示整数（自动位数，支持负数）
 */
void lcd_show_int(uint16_t x, uint16_t y, int32_t num,
                  uint16_t fc, uint16_t bc, uint8_t size)
{
    char buf[12];

    snprintf(buf, sizeof(buf), "%ld", (long)num);

    lcd_show_string(x, y, buf, fc, bc, size, 0);
}

/**
 * @brief  显示整数（指定位数）
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                  uint16_t fc, uint16_t bc, uint8_t size)
{
    uint8_t  sizex = (uint8_t)(size / 2);
    uint8_t  enshow = 0;
    uint32_t divisor = 1;

    if(len == 0)
    {
        return;
    }

    for(uint8_t t = 0; t < (len - 1); t++)
    {
        divisor *= 10;
    }

    for(uint8_t t = 0; t < len; t++)
    {
        uint8_t digit = (uint8_t)((num / divisor) % 10);

        if(enshow == 0 && t < (len - 1))
        {
            if(digit == 0)
            {
                lcd_show_char((uint16_t)(x + t * sizex), y, ' ', fc, bc, size, 0);
                divisor /= 10;
                continue;
            }
            enshow = 1;
        }

        lcd_show_char((uint16_t)(x + t * sizex), y, (char)('0' + digit), fc, bc, size, 0);
        divisor /= 10;
    }
}

/**
 * @brief  显示两位小数
 */
void lcd_show_float(uint16_t x, uint16_t y, float num, uint16_t fc, uint16_t bc, uint8_t size)
{
    char buf[16];
    int  int_part;
    int  frac_part;

    if(num < 0)
    {
        int_part  = (int)(num * 100.0f - 0.5f) / 100;
        frac_part = -(int)(num * 100.0f - 0.5f) % 100;
    }
    else
    {
        int_part  = (int)(num * 100.0f + 0.5f) / 100;
        frac_part = (int)(num * 100.0f + 0.5f) % 100;
    }

    /* 组装成字符串，省去逐位计算的麻烦 */
    snprintf(buf, sizeof(buf), "%d.%02d", int_part, frac_part);

    lcd_show_string(x, y, buf, fc, bc, size, 0);
}

/**************************************************************************************************
 *                                        汉字显示
 **************************************************************************************************/
typedef struct
{
    const uint8_t *base;        /* 字模表首地址 */
    uint16_t elem_size;         /* 单个元素字节数 */
    uint16_t count;             /* 汉字个数 */
    uint8_t  msk_bytes;         /* 每个字的点阵字节数 */
} lcd_gb_font_t;

/* 按字号取汉字字模表 */
static int lcd_gb_font_get(uint8_t size, lcd_gb_font_t *f)
{
    switch(size)
    {
    case 12:
        f->base      = (const uint8_t *)tfont12;
        f->elem_size = sizeof(typFNT_GB12);
        f->count     = (uint16_t)(sizeof(tfont12) / sizeof(typFNT_GB12));
        f->msk_bytes = 24;
        return 0;

    case 16:
        f->base      = (const uint8_t *)tfont16;
        f->elem_size = sizeof(typFNT_GB16);
        f->count     = (uint16_t)(sizeof(tfont16) / sizeof(typFNT_GB16));
        f->msk_bytes = 32;
        return 0;

    case 24:
        f->base      = (const uint8_t *)tfont24;
        f->elem_size = sizeof(typFNT_GB24);
        f->count     = (uint16_t)(sizeof(tfont24) / sizeof(typFNT_GB24));
        f->msk_bytes = 72;
        return 0;

    case 32:
        f->base      = (const uint8_t *)tfont32;
        f->elem_size = sizeof(typFNT_GB32);
        f->count     = (uint16_t)(sizeof(tfont32) / sizeof(typFNT_GB32));
        f->msk_bytes = 128;
        return 0;

    default:
        return -1;
    }
}

/**
 * @brief  显示单个汉字（GBK 两字节编码）
 */
static void lcd_show_one_chinese(uint16_t x, uint16_t y, const uint8_t *code,
                                 uint16_t fc, uint16_t bc, uint8_t size, uint8_t mode)
{
    lcd_gb_font_t font;
    const uint8_t *msk = NULL;
    uint16_t x0 = x;
    uint8_t  m = 0;

    if(lcd_gb_font_get(size, &font) != 0)
    {
        return;
    }

    for(uint16_t k = 0; k < font.count; k++)
    {
        const uint8_t *p = font.base + (uint32_t)k * font.elem_size;

        if(p[0] == code[0] && p[1] == code[1])
        {
            msk = p + 2;            /* 前 2 字节是编码，后面是点阵 */
            break;
        }
    }

    if(msk == NULL)                 /* 字库里没有这个字 */
    {
        lcd_fill_area(x, y, (uint16_t)(x + size - 1), (uint16_t)(y + size - 1), bc);
        return;
    }

    lcd_set_window(x, y, (uint16_t)(x + size - 1), (uint16_t)(y + size - 1));

    for(uint8_t i = 0; i < font.msk_bytes; i++)
    {
        uint8_t temp = msk[i];

        for(uint8_t j = 0; j < 8; j++)
        {
            if(mode == 0)
            {
                lcd_write_data((temp & (0x01 << j)) ? fc : bc);
                m++;
                if(m % size == 0)
                {
                    m = 0;
                    break;
                }
            }
            else
            {
                if(temp & (0x01 << j))
                {
                    lcd_draw_point(x, y, fc);
                }
                x++;
                if((x - x0) == size)
                {
                    x = x0;
                    y++;
                    break;
                }
            }
        }
    }
}

/**
 * @brief  显示汉字串
 */
void lcd_show_chinese(uint16_t x, uint16_t y, const char *str, uint16_t fc, uint16_t bc,
                      uint8_t size, uint8_t mode)
{
    const uint8_t *p = (const uint8_t *)str;

    if(str == NULL || (size != 12 && size != 16 && size != 24 && size != 32))
    {
        return;
    }

    while(*p != 0)
    {
        lcd_show_one_chinese(x, y, p, fc, bc, size, mode);
        p += 2;
        x = (uint16_t)(x + size);
    }
}

/**************************************************************************************************
 *                                        图片显示
 **************************************************************************************************/
/**
 * @brief  显示 RGB565 图片
 */
void lcd_show_picture(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pic)
{
    uint32_t total;

    if(pic == NULL || w == 0 || h == 0)
    {
        return;
    }

    total = (uint32_t)w * (uint32_t)h;

    lcd_set_window(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));

    for(uint32_t i = 0; i < total; i++)
    {
        uint16_t color = (uint16_t)((pic[i * 2] << 8) | pic[i * 2 + 1]);
        lcd_write_data(color);
    }
}

/**
 * @brief  格式化打印
 */
void lcd_printf(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint8_t size,
                const char *fmt, ...)
{
    char buf[64];
    va_list args;

    if(fmt == NULL)
    {
        return;
    }

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    lcd_show_string(x, y, buf, fc, bc, size, 0);
}

/**
 * @brief  白字黑底 16 号文字
 */
void lcd_text(uint16_t x, uint16_t y, const char *str)
{
    lcd_show_string(x, y, str, LCD_COLOR_WHITE, LCD_COLOR_BLACK, 16, 0);
}

/**
 * @brief  居中文字
 */
void lcd_text_center(uint16_t y, const char *str)
{
    uint16_t len;
    uint16_t width;

    if(str == NULL)
    {
        return;
    }

    len   = (uint16_t)strlen(str);
    width = (uint16_t)(len * 8);            /* 16 号字每字符宽 8 像素 */

    if(width >= lcddev.width)
    {
        lcd_text(0, y, str);
        return;
    }

    lcd_text((uint16_t)((lcddev.width - width) / 2), y, str);
}
