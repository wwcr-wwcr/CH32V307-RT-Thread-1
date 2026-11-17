/******************************************************************************
 * 文件名   : ch422.h
 * 芯片     : CH32V307VCT6
 * 说明     : CH422G 数码管驱动（PE0=SCL，PE1=SDA，4 位共阴数码管）
 *
 * ============================ 直接抄这三行 ============================
 *     ch422_init();                  // 上电初始化一次
 *     ch422_show_number(9999);       // 显示 9999，就这一行
 *     ch422_show_number_dp(1234, 2); // 显示 12.34（小数点在第 2 位后）
 * ======================================================================
 *
 * 其它常用:
 *     ch422_show_digit(0, 5);        // 第 1 位显示 5
 *     ch422_show_char(0, '-');       // 第 1 位显示负号
 *     ch422_clear();                 // 四位全灭
 *     ch422_all_on();                // 全亮 8.8.8.8.，用来确认数码管没坏
 *
 * 返回值约定: 0 = 成功，-1 = 参数错误（位数超出、数字不是 0~9）
 ******************************************************************************/

#ifndef _CH422_H_
#define _CH422_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 引脚
 */
#define CH422_SCL_GPIO      GPIOE
#define CH422_SDA_GPIO      GPIOE
#define CH422_SCL_PIN       GPIO_Pin_0      /* PE0 */
#define CH422_SDA_PIN       GPIO_Pin_1      /* PE1 */

/* 电平与方向 */
#define CH422_SCL_0()       (GPIO_ResetBits(CH422_SCL_GPIO, CH422_SCL_PIN))
#define CH422_SCL_1()       (GPIO_SetBits(CH422_SCL_GPIO, CH422_SCL_PIN))
#define CH422_SDA_0()       (GPIO_ResetBits(CH422_SDA_GPIO, CH422_SDA_PIN))
#define CH422_SDA_1()       (GPIO_SetBits(CH422_SDA_GPIO, CH422_SDA_PIN))
#define CH422_READ_SDA()    (GPIO_ReadInputDataBit(CH422_SDA_GPIO, CH422_SDA_PIN))

/*********************************************************************
 * 数码管参数（一般不用改；如果是共阳数码管把 CH422_SEG_INVERT 改成 1）
 */
#define CH422_DIGIT_COUNT   4               /* 数码管位数 */
#define CH422_SEG_INVERT    0               /* 0 = 共阴（段码为 1 点亮），1 = 共阳 */
#define CH422_SEG_DP        0x20            /* 小数点对应的段 */
#define CH422_SEG_MINUS     0x08            /* 负号对应的段 */
#define CH422_SEG_BLANK     0x00            /* 不点亮 */
#define CH422_SEG_ALL       0xFF            /* 全亮 */

/*********************************************************************
 * 底层协议常量（高级接口用，普通使用不需要关心）
 */
#define CH422_I2C_ADDR1     0x40
#define CH422_I2C_MASK      0x3E

#define CH422_SYS_CMD       0x48    /* 系统配置命令 */
#define CH422_RD_IO_CMD     0x4D    /* 读 IO 状态 */

#define BIT_OD_EN           0x10    /* DIG 输出类型：0 推挽，1 开漏 */
#define BIT_A_SCAN          0x04    /* 动态自动扫描 */
#define BIT_IO_OE           0x01    /* IO 输出使能 */

#define CH422_ADDRESS_1     0x70    /* 第 1 位 */
#define CH422_ADDRESS_2     0x72    /* 第 2 位 */
#define CH422_ADDRESS_3     0x74    /* 第 3 位 */
#define CH422_ADDRESS_4     0x76    /* 第 4 位 */

#define CH422_SYS_DEFAULT   (BIT_IO_OE | BIT_A_SCAN)

/*********************************************************************
 * 全局变量
 */
extern const uint8_t bcdDecode[12];     /* 0~9、小数点、空白 的段码 */

/*********************************************************************
 * 宝宝函数（照抄就能用，不用管命令和数据）
 *********************************************************************/

/**
 * @brief  初始化数码管（上电调用一次即可，之后直接显示）
 * @return 0 成功
 */
int ch422_init(void);

/**
 * @brief  显示一个 0~9999 的整数（前导 0 自动熄灭）
 * @param  value  要显示的数
 * @return 0 成功
 * @note   例：ch422_show_number(9999);
 */
int ch422_show_number(uint16_t value);

/**
 * @brief  显示带小数点的数
 * @param  value     要显示的数（去掉小数点后的整数，例如 12.34 传 1234）
 * @param  dp_index  小数点在第几位之后（1~4，例如 12.34 传 2）
 * @return 0 成功，-1 参数错误
 */
int ch422_show_number_dp(uint16_t value, uint8_t dp_index);

/**
 * @brief  显示带符号整数（-999 ~ 9999）
 * @param  value  要显示的数，负数会占用最左边一位显示负号
 * @return 0 成功
 * @note   例：ch422_show_int(-25); 显示 -25
 */
int ch422_show_int(int16_t value);

/**
 * @brief  某一位显示一个数字
 * @param  pos    第几位，0 = 最左边，3 = 最右边
 * @param  digit  0~9
 * @return 0 成功，-1 参数错误
 */
int ch422_show_digit(uint8_t pos, uint8_t digit);

/**
 * @brief  某一位显示数字并带小数点
 * @param  pos     第几位
 * @param  digit   0~9
 * @param  dp_on   1 点亮小数点，0 不亮
 * @return 0 成功，-1 参数错误
 */
int ch422_show_digit_dp(uint8_t pos, uint8_t digit, uint8_t dp_on);

/**
 * @brief  某一位显示字符
 * @param  pos  第几位
 * @param  ch   '0'~'9'、'-'（负号）、' '（熄灭）
 * @return 0 成功，-1 参数错误（该字符不支持）
 */
int ch422_show_char(uint8_t pos, char ch);

/**
 * @brief  四位全部熄灭
 * @return 0 成功
 */
int ch422_clear(void);

/**
 * @brief  四位全部点亮（8.8.8.8.，用于自检）
 * @return 0 成功
 */
int ch422_all_on(void);

/*********************************************************************
 * 高级接口（需要自定义段码 / 命令时再用）
 *********************************************************************/

/**
 * @brief  数码管某一位直接写段码
 * @param  pos  第几位，0~3
 * @param  seg  段码（bit0=A bit1=B bit2=F bit3=G bit4=C bit5=DP bit6=D bit7=E）
 * @return 0 成功，-1 参数错误
 */
int ch422_show_seg(uint8_t pos, uint8_t seg);

/**
 * @brief  数字转段码
 * @param  digit  0~9，其它返回空白
 * @return 段码
 */
uint8_t ch422_bcd(uint8_t digit);

/**
 * @brief  通用两字节写（命令 + 数据）
 * @return 0 成功
 */
int ch422_write_bytes(uint8_t cmd, uint8_t data);

/**
 * @brief  系统配置（BIT_IO_OE / BIT_A_SCAN / BIT_OD_EN 组合）
 * @return 0 成功
 */
int ch422_system_config(uint8_t config);

/**
 * @brief  读 IO 当前状态
 * @return IO 状态字节
 */
uint8_t ch422_read_io(void);

/**
 * @brief  SDA 切为输入
 */
void ch422_sda_input(void);

/**
 * @brief  SDA 切为开漏输出
 */
void ch422_sda_output(void);

/*********************************************************************
 * 旧接口兼容
 */
#define CH422_SDA_IN()      ch422_sda_input()
#define CH422_SDA_OUT()     ch422_sda_output()
#define ch422_write_two_bytes(a, b)     ch422_write_bytes((a), (b))

#ifdef __cplusplus
}
#endif

#endif
