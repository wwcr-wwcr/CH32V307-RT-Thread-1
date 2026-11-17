/******************************************************************************
 * 文件名   : GT911.h
 * 芯片     : CH32V307VCT6
 * 说明     : GT911 电容触摸屏驱动（最多 5 点，软件 I2C）
 *
 * 使用示例:
 *     gt911_init();                          // 初始化并打印芯片 ID
 *     gt911_touch_t touch;
 *     if(gt911_touch_read(&touch) > 0)       // 有触摸
 *     {
 *         printf("x=%d y=%d\n", touch.point[0].x, touch.point[0].y);
 *     }
 *
 * 返回值约定: 0 / 正数 = 成功，负数 = 失败（-1 无应答，-2 参数错误）
 ******************************************************************************/

#ifndef _GT911_H_
#define _GT911_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"
#include "GT911_IIC.h"

/*********************************************************************
 * 参数
 */
#define GT911_I2C_ADDR_WRITE     0xBA       /* 8 位写地址（7 位地址 0x5D << 1） */
#define GT911_I2C_ADDR_READ      0xBB       /* 8 位读地址 */

#define GT911_REG_PRODUCT_ID     0x8140     /* 产品 ID，正常读到 "911" */
#define GT911_REG_CONFIG         0x8047     /* 配置表起始地址 */
#define GT911_REG_STATUS         0x814E     /* bit7=数据就绪，低 4 位=触摸点数 */
#define GT911_REG_POINT1         0x8150     /* 第一个触摸点数据 */
#define GT911_POINT_SIZE         8          /* 每个触摸点占 8 字节 */

#define GT911_TOUCH_MAX_NUM      5
#define GT911_SCREEN_WIDTH       320
#define GT911_SCREEN_HEIGHT      480

/* 触摸中断开关：
 * KEY1(PE2) 和 GT911 的 INT(PD2) 共用 EXTI_Line2，同一时刻只能有一个用中断。
 * 现在按键走中断、触摸走轮询，所以默认 0。
 * 如果要改用触摸中断，需要先把 KEY1 换到别的引脚（或关掉 KEY1 的 EXTI）。 */
#define GT911_USE_TOUCH_INT      0

/*********************************************************************
 * 类型定义
 */
typedef struct
{
    uint16_t x;         /* X 坐标 */
    uint16_t y;         /* Y 坐标 */
    uint16_t size;      /* 触摸面积 */
} gt911_point_t;

typedef struct
{
    uint8_t      count;                             /* 触摸点数 */
    gt911_point_t point[GT911_TOUCH_MAX_NUM];       /* 各点坐标 */
} gt911_touch_t;

/* 兼容旧代码的数据结构 */
typedef struct
{
    uint16_t PointX;
    uint16_t PointY;
    uint16_t PointS;
} xy_coordinate_t;

typedef struct
{
    uint8_t         TouchState;
    uint8_t         TouchNumber;
    xy_coordinate_t Touch_xy[GT911_TOUCH_MAX_NUM];
} Touch_data_t;

/*********************************************************************
 * 全局变量
 */
extern Touch_data_t touchData;      /* 最近一次读取的触摸数据（兼容旧代码） */

/*********************************************************************
 * 函数
 */

/**
 * @brief  初始化 GT911
 * @return 0 成功，-1 芯片无应答（检查供电/复位/地址）
 */
int gt911_init(void);

/**
 * @brief  读寄存器（支持连续多个字节）
 * @return 0 成功，-1 无应答
 */
int gt911_read_regs(uint16_t reg, uint8_t *buf, uint16_t len);

/**
 * @brief  写寄存器
 * @return 0 成功，-1 无应答
 */
int gt911_write_regs(uint16_t reg, const uint8_t *buf, uint16_t len);

/**
 * @brief  读取触摸数据（主动轮询，不依赖 INT 引脚）
 * @param  touch  输出
 * @return 触摸点数（0 表示没有触摸），负数表示通信失败
 */
int gt911_touch_read(gt911_touch_t *touch);

/**
 * @brief  读取当前触摸点数
 * @return 触摸点数
 */
uint8_t gt911_touch_count(void);

/**
 * @brief  开/关触摸中断（INT 引脚下降沿/上升沿）
 * @param  enable  1 打开，0 关闭
 * @return 0 成功
 */
int gt911_int_enable(uint8_t enable);

/*********************************************************************
 * 旧接口兼容
 */
#define GT911_ID_ADDRESS        GT911_REG_PRODUCT_ID
#define GT911_CFGS_REG          GT911_REG_CONFIG
#define GT911_BUFFER_STATUS     GT911_REG_STATUS
#define GT911_BUFFER_START      GT911_REG_POINT1
#define GT911_BUFFER_TPD1       GT911_REG_POINT1
#define GT911_IIC_READ_ADDRESS  GT911_I2C_ADDR_READ
#define GT911_IIC_WRITE_ADDRESS GT911_I2C_ADDR_WRITE

#define GT911_read_register(r, b, l)        gt911_read_regs((r), (b), (l))
#define GT911_write_register(r, b, l)       ((uint8_t)gt911_write_regs((r), (b), (l)))

/**
 * @brief  兼容旧接口：读取触摸数据到 touchData
 */
void GT911_touch_scanf(Touch_data_t *touch);

/*********************************************************************
 * 宝宝函数（读取触摸只需要一个函数）
 *********************************************************************/

/**
 * @brief  有没有人摸屏幕
 * @return 1 有触摸，0 没有
 */
uint8_t gt911_touched(void);

/**
 * @brief  读取触摸坐标（最省事，一次搞定）
 * @param  x  输出 X 坐标
 * @param  y  输出 Y 坐标
 * @return 1 有触摸，0 没有
 * @note   例：uint16_t x, y; if(gt911_get_point(&x, &y)) {...}
 */
uint8_t gt911_get_point(uint16_t *x, uint16_t *y);

/**
 * @brief  上一次 gt911_get_point() 读到的 X 坐标
 */
uint16_t gt911_x(void);

/**
 * @brief  上一次 gt911_get_point() 读到的 Y 坐标
 */
uint16_t gt911_y(void);

#ifdef __cplusplus
}
#endif

#endif /* end of GT911.h */
