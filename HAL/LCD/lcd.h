/******************************************************************************
 * 文件名   : lcd.h
 * 芯片     : CH32V307VCT6
 * 说明     : ST7796S 320x480 TFT 液晶驱动（FSMC 并口，8080 时序）
 *
 * 引脚说明:
 *   D0~D15 -> PD14/PD15/PD0/PD1/PE7~PE15   RS(寄存器/数据) -> PD12
 *   CS     -> PD11                          RST -> PD13
 *   BL(背光) -> PC4（高电平点亮）
 *
 * 使用示例:
 *     lcd_init();                           // 初始化并清屏
 *     lcd_clear(LCD_COLOR_BLACK);
 *     lcd_fill(10, 10, 100, 50, LCD_COLOR_RED);       // 左上角 100x50 的红块
 *     lcd_show_string(20, 80, "Hello", LCD_COLOR_WHITE, LCD_COLOR_BLACK, 16, 0);
 *     lcd_show_num(20, 100, 1234, 4, LCD_COLOR_WHITE, LCD_COLOR_BLACK, 16);
 *
 * 返回值约定: 0 = 成功，负数 = 失败（-1 参数错误）
 ******************************************************************************/

#ifndef __LCD_H
#define __LCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"
#include "stdlib.h"

/*********************************************************************
 * 屏幕参数
 */
#define LCD_PANEL_WIDTH         320
#define LCD_PANEL_HEIGHT        480
#define LCD_EXPECT_ID           0x7796      /* ST7796S 的 ID，读不到说明接线/时序有问题 */

/* 扫描方向（直接对应 MADCTL 寄存器的值，不要随意修改组合） */
typedef enum
{
    LCD_DIR_L2R_U2D = 0x00,     /* 从左到右，从上到下 */
    LCD_DIR_L2R_D2U = 0x80,     /* 从左到右，从下到上（180 度） */
    LCD_DIR_R2L_U2D = 0x40,     /* 从右到左，从上到下 */
    LCD_DIR_R2L_D2U = 0xC0,     /* 从右到左，从下到上 */
    LCD_DIR_U2D_L2R = 0x20,     /* 从上到下，从左到右（横屏） */
    LCD_DIR_U2D_R2L = 0x60,     /* 从上到下，从右到左 */
    LCD_DIR_D2U_L2R = 0xA0,     /* 从下到上，从左到右 */
    LCD_DIR_D2U_R2L = 0xE0      /* 从下到上，从右到左 */
} lcd_dir_t;

#define LCD_DFT_DIR             LCD_DIR_R2L_U2D    /* 默认竖屏 320x480 */

/*********************************************************************
 * FSMC 访问地址（LVGL 的 DMA 刷屏会直接用到 LCD->LCD_RAM）
 */
typedef struct
{
    vu16 LCD_REG;
    vu16 LCD_RAM;
} LCD_TypeDef;

#define LCD_BASE        ((u32)(0x60000000 | 0x0003FFFE))
#define LCD             ((LCD_TypeDef *)LCD_BASE)

/*********************************************************************
 * 颜色（RGB565）
 */
#define LCD_COLOR_WHITE     0xFFFF
#define LCD_COLOR_BLACK     0x0000
#define LCD_COLOR_BLUE      0x001F
#define LCD_COLOR_GREEN     0x07E0
#define LCD_COLOR_RED       0xF800
#define LCD_COLOR_YELLOW    0xFFE0
#define LCD_COLOR_MAGENTA   0xF81F
#define LCD_COLOR_CYAN      0x7FFF
#define LCD_COLOR_GRAY      0x8430
#define LCD_COLOR_LGRAY     0xC618
#define LCD_COLOR_ORANGE    0xFD20
#define LCD_COLOR_BROWN     0xBC40
#define LCD_COLOR_DARKBLUE  0x01CF
#define LCD_COLOR_LIGHTBLUE 0x7D7C

/*********************************************************************
 * 全局变量
 */
typedef struct
{
    u16 width;      /* 当前方向下的宽度 */
    u16 height;     /* 当前方向下的高度 */
    u16 id;         /* 读到的 LCD ID */
} _lcd_dev;

extern _lcd_dev lcddev;

/*********************************************************************
 * 底层寄存器 / 数据访问
 */

/**
 * @brief  写命令（寄存器地址）
 */
void lcd_write_cmd(uint16_t cmd);

/**
 * @brief  写 16 位数据
 */
void lcd_write_data(uint16_t data);

/**
 * @brief  连续写多个 16 位数据（刷屏用）
 */
void lcd_write_data_buf(const uint16_t *buf, uint32_t len);

/**
 * @brief  读 16 位数据
 */
uint16_t lcd_read_data(void);

/**
 * @brief  读寄存器
 */
uint16_t lcd_read_reg(uint16_t reg);

/*********************************************************************
 * 初始化与控制
 */

/**
 * @brief  初始化 LCD（FSMC + 面板寄存器 + 清屏）
 * @return 0 成功，-2 ID 读取异常（通常是接线或 FSMC 时序问题）
 */
int lcd_init(void);

/**
 * @brief  设置扫描方向（会自动更新 lcddev.width / height）
 * @param  dir  方向枚举
 * @return 0 成功
 */
int lcd_set_dir(lcd_dir_t dir);

/**
 * @brief  开显示
 */
void lcd_display_on(void);

/**
 * @brief  关显示
 */
void lcd_display_off(void);

/**
 * @brief  背光控制
 * @param  on  1 点亮，0 熄灭
 */
void lcd_backlight(uint8_t on);

/**
 * @brief  当前屏幕宽度
 */
uint16_t lcd_width(void);

/**
 * @brief  当前屏幕高度
 */
uint16_t lcd_height(void);

/**
 * @brief  读取到的 LCD ID
 */
uint16_t lcd_id(void);

/*********************************************************************
 * 显示区域与基本绘图
 */

/**
 * @brief  设置绘图窗口（含首尾坐标）
 */
void lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

/**
 * @brief  设置光标位置
 */
void lcd_set_cursor(uint16_t x, uint16_t y);

/**
 * @brief  用单色填满整屏
 */
void lcd_clear(uint16_t color);

/**
 * @brief  填充矩形区域（左上角坐标 + 宽高）
 */
void lcd_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief  填充矩形区域（首尾坐标，含端点，兼容旧写法）
 */
void lcd_fill_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/**
 * @brief  画点
 */
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief  读某点颜色
 */
uint16_t lcd_read_point(uint16_t x, uint16_t y);

/**
 * @brief  画线
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/**
 * @brief  画矩形边框（左上角坐标 + 宽高）
 */
void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief  画矩形边框（首尾坐标）
 */
void lcd_draw_rect_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/**
 * @brief  画圆（空心）
 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);

/*********************************************************************
 * 字符 / 汉字 / 图片
 */

/**
 * @brief  显示单个 ASCII 字符
 * @param  x,y    左上角坐标
 * @param  ch     字符
 * @param  fc     前景色
 * @param  bc     背景色
 * @param  size   字号：12 / 16 / 24 / 32
 * @param  mode   0 = 非叠加（填充背景色），1 = 叠加（只画字，保留背景）
 */
void lcd_show_char(uint16_t x, uint16_t y, char ch, uint16_t fc, uint16_t bc,
                   uint8_t size, uint8_t mode);

/**
 * @brief  显示字符串
 */
void lcd_show_string(uint16_t x, uint16_t y, const char *str, uint16_t fc, uint16_t bc,
                     uint8_t size, uint8_t mode);

/**
 * @brief  显示整数（自动位数，支持负数，最省事）
 * @note   例：lcd_show_int(10, 10, -123, WHITE, BLACK, 16);
 */
void lcd_show_int(uint16_t x, uint16_t y, int32_t num,
                  uint16_t fc, uint16_t bc, uint8_t size);

/**
 * @brief  显示十进制整数（右对齐，前导 0 用空格填充）
 * @param  len  显示位数
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                  uint16_t fc, uint16_t bc, uint8_t size);

/**
 * @brief  显示两位小数
 */
void lcd_show_float(uint16_t x, uint16_t y, float num, uint16_t fc, uint16_t bc, uint8_t size);

/**
 * @brief  显示汉字串（需字库里有该字）
 * @param  str   汉字串（GBK 编码）
 * @param  size  字号：12 / 16 / 24 / 32
 */
void lcd_show_chinese(uint16_t x, uint16_t y, const char *str, uint16_t fc, uint16_t bc,
                      uint8_t size, uint8_t mode);

/**
 * @brief  显示图片（RGB565 数组，按 16 位大端存放）
 */
void lcd_show_picture(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pic);

/*********************************************************************
 * 旧接口兼容（新代码建议使用上面的函数）
 */
#define LCD_WR_REG(c)                       lcd_write_cmd(c)
#define LCD_WR_DATA(d)                      lcd_write_data(d)
#define LCD_RD_DATA()                       lcd_read_data()
#define LCD_ReadReg(r)                      lcd_read_reg(r)
#define LCD_Init()                          lcd_init()
#define LCD_SetCursor(x, y)                 lcd_set_cursor((x), (y))
#define LCD_Address_Set(x1, y1, x2, y2)     lcd_set_window((x1), (y1), (x2), (y2))
#define LCD_DisplayOn()                     lcd_display_on()
#define LCD_DisplayOff()                    lcd_display_off()
#define LCD_ReadPoint(x, y)                 lcd_read_point((x), (y))
#define LCD_Clear(c)                        lcd_clear(c)
#define LCD_Fill(x1, y1, x2, y2, c)         lcd_fill_area((x1), (y1), (x2) - 1, (y2) - 1, (c))
#define LCD_DrawPoint(x, y, c)              lcd_draw_point((x), (y), (c))
#define LCD_DrawLine(a, b, c, d, col)       lcd_draw_line((a), (b), (c), (d), (col))
#define LCD_DrawRectangle(a, b, c, d, col)  lcd_draw_rect_area((a), (b), (c), (d), (col))
#define Draw_Circle(x, y, r, c)             lcd_draw_circle((x), (y), (r), (c))
#define LCD_ShowChar(x, y, n, f, b, s, m)   lcd_show_char((x), (y), (char)(n), (f), (b), (s), (m))
#define LCD_ShowString(x, y, p, f, b, s, m) lcd_show_string((x), (y), (const char *)(p), (f), (b), (s), (m))
#define LCD_ShowChinese(x, y, p, f, b, s, m) lcd_show_chinese((x), (y), (const char *)(p), (f), (b), (s), (m))
#define LCD_ShowIntNum(x, y, n, l, f, b, s) lcd_show_num((x), (y), (n), (l), (f), (b), (s))
#define LCD_ShowFloatNum1(x, y, n, l, f, b, s) lcd_show_float((x), (y), (n), (f), (b), (s))
#define LCD_ShowPicture(x, y, l, w, p)      lcd_show_picture((x), (y), (l), (w), (p))
#define Set_Dir(d)                          lcd_set_dir((lcd_dir_t)(d))

/* 常用颜色别名 */
#define WHITE   LCD_COLOR_WHITE
#define BLACK   LCD_COLOR_BLACK
#define BLUE    LCD_COLOR_BLUE
#define RED     LCD_COLOR_RED
#define GREEN   LCD_COLOR_GREEN
#define YELLOW  LCD_COLOR_YELLOW
#define MAGENTA LCD_COLOR_MAGENTA
#define CYAN    LCD_COLOR_CYAN
#define GRAY    LCD_COLOR_GRAY
#define LGRAY   LCD_COLOR_LGRAY
#define BROWN   LCD_COLOR_BROWN
#define BRED    LCD_COLOR_MAGENTA
#define GRED    0xFFE0
#define GBLUE   0x07FF
#define BRRED   0xFC07
#define DARKBLUE    LCD_COLOR_DARKBLUE
#define LIGHTBLUE   LCD_COLOR_LIGHTBLUE
#define GRAYBLUE    0x5458
#define LIGHTGREEN  0x841F
#define LGRAYBLUE   0xA651
#define LBBLUE      0x2B12

/*********************************************************************
 * 宝宝函数（一行搞定，不用管底层细节）
 *********************************************************************/

/**
 * @brief  在指定位置打印格式化字符串（最省事，和 printf 一样）
 * @param  x,y   左上角坐标
 * @param  fc    前景色
 * @param  bc    背景色
 * @param  size  字号 12/16/24/32
 * @param  fmt   格式串
 * @note   例：lcd_printf(10, 10, WHITE, BLACK, 16, "T=%d", 25);
 */
void lcd_printf(uint16_t x, uint16_t y, uint16_t fc, uint16_t bc, uint8_t size,
                const char *fmt, ...);

/**
 * @brief  显示一行白字黑底 16 号文字（连颜色都不用给）
 * @note   例：lcd_text(10, 10, "Hello");
 */
void lcd_text(uint16_t x, uint16_t y, const char *str);

/**
 * @brief  居中显示一行白字黑底 16 号文字
 * @param  y  这一行的纵坐标
 */
void lcd_text_center(uint16_t y, const char *str);

#ifdef __cplusplus
}
#endif

#endif
