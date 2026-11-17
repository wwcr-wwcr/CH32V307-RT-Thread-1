/******************************************************************************
 * 文件名   : veml7700.h
 * 芯片     : CH32V307VCT6
 * 说明     : VEML7700 环境光传感器（硬件 I2C，7 位地址 0x10，PB8/PB9）
 *
 * 用法（最省事）:
 *     veml7700_init();                    // main 里已经 i2c_init 过了
 *     uint16_t lx = veml7700_lux_get();   // 直接拿 lux
 *
 * 想调刷新率 / 灵敏度:
 *     veml7700_set_it(VEML7700_IT_50MS);       // 20Hz，够跟手
 *     veml7700_set_gain(VEML7700_GAIN_1_4);    // 暗环境加增益
 *     float r = veml7700_resolution();         // 当前 lx/计数，别再写死系数
 *
 * 刷新率完全由积分时间 IT 决定：IT 多少毫秒，芯片就多少毫秒才更新一次，
 * 代码读得再快也只是拿到同一个旧值。想要 20Hz 就把 IT 设成 50ms。
 *
 * 返回值约定: 0 = 成功，非 0 = 失败
 ******************************************************************************/

#ifndef __VEML7700_H
#define __VEML7700_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v30x_conf.h"

/*********************************************************************
 * 地址与寄存器
 */
#define VEML7700_ADDRESS        0x10

#define VEML7700_REG_CONF       0x00    /* ALS_CONF：增益 / 积分时间 / 中断 / 开关 */
#define VEML7700_REG_WH         0x01    /* 高阈值 */
#define VEML7700_REG_WL         0x02    /* 低阈值 */
#define VEML7700_REG_POWER      0x03    /* 省电模式 */
#define VEML7700_REG_ALS        0x04    /* ALS 计数 */
#define VEML7700_REG_WHITE      0x05    /* 白光通道计数 */
#define VEML7700_REG_INT        0x06    /* 中断状态：bit15 低阈、bit14 高阈，读一次自动清 */

/* 中断状态位 */
#define VEML7700_IF_LOW         0x8000
#define VEML7700_IF_HIGH        0x4000

/*********************************************************************
 * ALS_CONF 位定义（这几个宏比 0x1080 好读得多）
 */
#define CONF_IT_MASK            0x03C0  /* bit9:6  积分时间 */
#define CONF_IT_SHIFT           6
#define CONF_GAIN_MASK          0x1800  /* bit12:11 增益 */
#define CONF_GAIN_SHIFT         11
#define CONF_PERS_MASK          0x0030  /* bit5:4  中断持续次数 */
#define CONF_INT_EN             0x0002  /* bit1    中断使能 */
#define CONF_SD                 0x0001  /* bit0    1 = 关断 */

/*********************************************************************
 * 增益与积分时间（枚举值就是寄存器里的位，可以直接移位写入）
 */
typedef enum
{
    VEML7700_GAIN_1   = 0x00,       /* X1   最细，量程最小 */
    VEML7700_GAIN_2   = 0x01,       /* X2   更细 */
    VEML7700_GAIN_1_8 = 0x02,       /* X1/8 量程最大 */
    VEML7700_GAIN_1_4 = 0x03        /* X1/4 折中（默认） */
} veml7700_gain_t;

typedef enum
{
    VEML7700_IT_25MS  = 0x0C,       /* 40Hz，最跟手，分辨率最粗 */
    VEML7700_IT_50MS  = 0x08,       /* 20Hz */
    VEML7700_IT_100MS = 0x00,       /* 10Hz（默认） */
    VEML7700_IT_200MS = 0x01,       /* 5Hz  */
    VEML7700_IT_400MS = 0x02,       /* 2.5Hz */
    VEML7700_IT_800MS = 0x03        /* 1.25Hz，最准 */
} veml7700_it_t;

/*********************************************************************
 * 一次性把这组数据读回来
 */
typedef struct
{
    uint16_t raw;                   /* ALS 原始计数 */
    uint16_t white;                 /* 白光通道计数 */
    float    lux;                   /* 照度，已做高照度修正 */
    float    res;                   /* 当前增益/积分时间下的分辨率 lx/计数 */
} veml7700_data_t;

/*********************************************************************
 * 初始化与自检
 */

/**
 * @brief  初始化：配置增益/积分时间，写两遍并回读校验
 * @return 0 成功；-1 芯片没应答或回读不一致
 * @note   调用前 main 里必须已经 i2c_init()
 */
int veml7700_init(void);

/**
 * @brief  探测设备
 * @return 0 在线；-1 不应答
 */
int veml7700_present(void);

/**
 * @brief  回读配置寄存器，看还在不在
 * @return 1 在线；0 掉线或还没 init
 */
uint8_t veml7700_is_online(void);

/*********************************************************************
 * 参数（改完立刻生效，会重写 ALS_CONF）
 */
void            veml7700_set_gain(veml7700_gain_t gain);
veml7700_gain_t veml7700_get_gain(void);
void            veml7700_set_it(veml7700_it_t it);
veml7700_it_t   veml7700_get_it(void);

/**
 * @brief  当前积分时间（毫秒），也就是芯片多久更新一次数据
 */
uint16_t veml7700_it_ms(void);

/**
 * @brief  当前配置下的分辨率 lx/计数
 * @note   不要再写死 0.1344 那种系数，随增益和积分时间变化
 */
float veml7700_resolution(void);

/*********************************************************************
 * 读数
 */
uint16_t veml7700_raw_get(void);            /* ALS 原始计数，失败返回 0 */
uint16_t veml7700_white(void);              /* 白光通道计数 */

/**
 * @brief  直接读上次转换结果，不等待（快，但数据可能是上一次的）
 */
float veml7700_lux_now(void);

/**
 * @brief  等一个积分周期再读，保证拿到的是新数据
 * @note   会阻塞 it_ms 毫秒，只能在任务里调，不能放中断
 */
float veml7700_lux(void);

/**
 * @brief  一次把 raw / white / lux / res 都读回来
 * @return 0 成功
 */
uint8_t veml7700_read(veml7700_data_t *d);

/**
 * @brief  兼容旧接口：直接返回 lux（取整）
 * @note   不等待，靠调用方的采样间隔保证数据新鲜；超过 65535 会夹住
 */
uint16_t veml7700_lux_get(void);

/*********************************************************************
 * 省事的小工具
 */

/**
 * @brief  按上次读数自动换挡（太亮缩短积分时间、太暗加长）
 * @return 1 = 改了配置（下次读要重新等一个周期）；0 = 没动
 */
uint8_t veml7700_auto_range(void);

/**
 * @brief  把照度映射成 0~max_level 的等级，用来画光柱
 *         1 / 3 / 10 / 30 / 100 / 300 / 1000 / 3000 / 10000 lx 分 9 段
 */
uint8_t veml7700_level(uint8_t max_level);

/*********************************************************************
 * 旧兼容名（新代码请直接用上面的）
 */
#define ALS_CONF            VEML7700_REG_CONF
#define ALS_WH              VEML7700_REG_WH
#define ALS_WL              VEML7700_REG_WL
#define ALS_POWER           VEML7700_REG_POWER
#define ALS_DATA            VEML7700_REG_ALS
#define ALS_WHITE           VEML7700_REG_WHITE
#define ALS_INT             VEML7700_REG_INT
#define veml7700_ambient_light_read()   veml7700_lux_get()

#ifdef __cplusplus
}
#endif

#endif
