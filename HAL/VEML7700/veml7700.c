/******************************************************************************
 * 文件名   : veml7700.c
 * 芯片     : CH32V307VCT6
 * 说明     : VEML7700 环境光传感器驱动（硬件 I2C，PB8/PB9）
 *
 * 和旧版的三点不同：
 *   1. 不再写死 0.1344 这个系数 —— 它只对"1/8 增益 + 400ms"成立，
 *      而且本身就是错的（正确值 0.1152），照抄会把读数抬高约 17%。
 *      现在按增益和积分时间查表算分辨率。
 *   2. 加了高照度四阶修正（> 1000 lx），太阳底下才不会离谱。
 *   3. 增益/积分时间用枚举，改刷新率就是一句话。
 ******************************************************************************/

#include "veml7700.h"
#include "i2c.h"
#include "rtthread.h"

/**************************************************************************************************
 *                                       内部常量
 **************************************************************************************************/
/* 影子寄存器：改参数时不用每次回读，省一次 I2C 事务 */
static uint16_t s_conf   = 0;
static uint8_t  s_online = 0;

/**************************************************************************************************
 *                                       寄存器读写（16 位，小端）
 **************************************************************************************************/
static int veml7700_write16(uint8_t reg, uint16_t value)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)(value >> 8);

    return (i2c_write_reg(VEML7700_ADDRESS, reg, buf, 2) == I2C_OK) ? 0 : -1;
}

static int veml7700_read16(uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];

    if(value == NULL)
    {
        return -1;
    }

    if(i2c_read_reg(VEML7700_ADDRESS, reg, buf, 2) != I2C_OK)
    {
        return -1;
    }

    *value = (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));

    return 0;
}

/**************************************************************************************************
 *                                       分辨率表
 **************************************************************************************************/
/**
 * @brief  查当前增益 + 积分时间对应的分辨率（lx/计数）
 * @note   数据来自 Vishay App Note 的表，行 = 积分时间，列 = 增益
 *         旧版那个 0.1344 是错的：1/8 增益 + 400ms 应该是 0.1152
 */
static float resolution_of(uint8_t gain, uint8_t it)
{
    static const float table[6][4] =
    {
        /*   GAIN2      GAIN1      GAIN1/4     GAIN1/8   */
        { 0.1152f,  0.2304f,  0.9216f,  1.8432f },   /* 25ms  */
        { 0.0576f,  0.1152f,  0.4608f,  0.9216f },   /* 50ms  */
        { 0.0288f,  0.0576f,  0.2304f,  0.4608f },   /* 100ms */
        { 0.0144f,  0.0288f,  0.1152f,  0.2304f },   /* 200ms */
        { 0.0072f,  0.0144f,  0.0576f,  0.1152f },   /* 400ms */
        { 0.0036f,  0.0072f,  0.0288f,  0.0576f }    /* 800ms */
    };

    uint8_t row;
    uint8_t col;

    switch(it)
    {
        case VEML7700_IT_25MS:  row = 0; break;
        case VEML7700_IT_50MS:  row = 1; break;
        case VEML7700_IT_100MS: row = 2; break;
        case VEML7700_IT_200MS: row = 3; break;
        case VEML7700_IT_400MS: row = 4; break;
        case VEML7700_IT_800MS: row = 5; break;
        default:                row = 2; break;
    }

    switch(gain)
    {
        case VEML7700_GAIN_2:   col = 0; break;
        case VEML7700_GAIN_1:   col = 1; break;
        case VEML7700_GAIN_1_4: col = 2; break;
        default:                col = 3; break;     /* VEML7700_GAIN_1_8 */
    }

    return table[row][col];
}

static uint8_t it_code(void)
{
    return (uint8_t)((s_conf & CONF_IT_MASK) >> CONF_IT_SHIFT);
}

static uint8_t gain_code(void)
{
    return (uint8_t)((s_conf & CONF_GAIN_MASK) >> CONF_GAIN_SHIFT);
}

/**************************************************************************************************
 *                                       初始化与自检
 **************************************************************************************************/
int veml7700_init(void)
{
    uint16_t readback = 0;
    uint8_t  i;

    /* 默认：1/4 增益 + 100ms 积分
     * 分辨率 0.2304 lx/计数，量程到 1.5 万 lx：
     * 室内（几十 ~ 一千 lx）够细，拿到太阳底下也不会一上来就饱和。
     * 要更跟手就换 50ms，要更细就 GAIN_1，要更大量程就 GAIN_1_8。 */
    s_conf = (uint16_t)(((uint16_t)VEML7700_GAIN_1_4 << CONF_GAIN_SHIFT) |
                        ((uint16_t)VEML7700_IT_100MS << CONF_IT_SHIFT));

    /* 上电偶尔会漏一帧，写两遍 + 回读校验 */
    for(i = 0; i < 2u; i++)
    {
        if(veml7700_write16(VEML7700_REG_CONF, s_conf) != 0)
        {
            s_online = 0;
            return -1;
        }

        (void)veml7700_write16(VEML7700_REG_POWER, 0x0000);   /* 省电模式关掉 */

        rt_thread_mdelay(5);                                  /* 等它完成一次上电复位 */

        if((veml7700_read16(VEML7700_REG_CONF, &readback) == 0) && (readback == s_conf))
        {
            s_online = 1;
            return 0;
        }
    }

    s_online = 0;
    return -1;
}

int veml7700_present(void)
{
    return i2c_probe(VEML7700_ADDRESS);
}

uint8_t veml7700_is_online(void)
{
    uint16_t readback = 0;

    if(s_conf == 0u)
    {
        return 0;                               /* 还没 init 过 */
    }

    return (veml7700_read16(VEML7700_REG_CONF, &readback) == 0 &&
            readback == s_conf) ? 1u : 0u;
}

/**************************************************************************************************
 *                                       参数
 **************************************************************************************************/
void veml7700_set_gain(veml7700_gain_t gain)
{
    s_conf = (uint16_t)((s_conf & ~CONF_GAIN_MASK) |
                        ((uint16_t)((uint8_t)gain & 0x03u) << CONF_GAIN_SHIFT));

    (void)veml7700_write16(VEML7700_REG_CONF, s_conf);
}

veml7700_gain_t veml7700_get_gain(void)
{
    return (veml7700_gain_t)gain_code();
}

void veml7700_set_it(veml7700_it_t it)
{
    s_conf = (uint16_t)((s_conf & ~CONF_IT_MASK) |
                        ((uint16_t)((uint8_t)it & 0x0Fu) << CONF_IT_SHIFT));

    (void)veml7700_write16(VEML7700_REG_CONF, s_conf);
}

veml7700_it_t veml7700_get_it(void)
{
    return (veml7700_it_t)it_code();
}

uint16_t veml7700_it_ms(void)
{
    switch(it_code())
    {
        case VEML7700_IT_25MS:  return 25;
        case VEML7700_IT_50MS:  return 50;
        case VEML7700_IT_100MS: return 100;
        case VEML7700_IT_200MS: return 200;
        case VEML7700_IT_400MS: return 400;
        case VEML7700_IT_800MS: return 800;
        default:                return 100;
    }
}

float veml7700_resolution(void)
{
    return resolution_of(gain_code(), it_code());
}

/**************************************************************************************************
 *                                       读数
 **************************************************************************************************/
uint16_t veml7700_raw_get(void)
{
    uint16_t raw = 0;

    if(veml7700_read16(VEML7700_REG_ALS, &raw) != 0)
    {
        return 0;
    }

    return raw;
}

uint16_t veml7700_white(void)
{
    uint16_t white = 0;

    if(veml7700_read16(VEML7700_REG_WHITE, &white) != 0)
    {
        return 0;
    }

    return white;
}

float veml7700_lux_now(void)
{
    float    lux;
    uint16_t raw = 0;

    if(veml7700_read16(VEML7700_REG_ALS, &raw) != 0)
    {
        return 0.0f;
    }

    lux = (float)raw * veml7700_resolution();

    /* 超过 1000 lx 手册要求做四阶修正（不引 math.h，直接乘） */
    if(lux > 1000.0f)
    {
        float l2 = lux * lux;
        float l3 = l2 * lux;
        float l4 = l3 * lux;

        lux = 6.0135e-13f * l4 - 9.3924e-9f * l3 + 8.1488e-5f * l2 + 1.0023f * lux;
    }

    return lux;
}

float veml7700_lux(void)
{
    rt_thread_mdelay(veml7700_it_ms());         /* 等一个积分周期，保证读的是新数据 */

    return veml7700_lux_now();
}

uint8_t veml7700_read(veml7700_data_t *d)
{
    uint16_t raw   = 0;
    uint16_t white = 0;
    float    res;
    float    lux;

    if(d == NULL)
    {
        return 1;
    }

    if(veml7700_read16(VEML7700_REG_ALS, &raw) != 0)
    {
        return 1;
    }

    (void)veml7700_read16(VEML7700_REG_WHITE, &white);

    res = veml7700_resolution();
    lux = (float)raw * res;

    if(lux > 1000.0f)
    {
        float l2 = lux * lux;
        float l3 = l2 * lux;
        float l4 = l3 * lux;

        lux = 6.0135e-13f * l4 - 9.3924e-9f * l3 + 8.1488e-5f * l2 + 1.0023f * lux;
    }

    d->raw   = raw;
    d->white = white;
    d->res   = res;
    d->lux   = lux;

    return 0;
}

uint16_t veml7700_lux_get(void)
{
    float lux = veml7700_lux_now();

    if(lux < 0.0f)
    {
        return 0;
    }
    if(lux > 65535.0f)
    {
        return 65535;
    }

    return (uint16_t)(lux + 0.5f);
}

/**************************************************************************************************
 *                                       小工具
 **************************************************************************************************/
uint8_t veml7700_auto_range(void)
{
    /* 灵敏度从低到高：1/8 -> 1/4 -> 1 -> 2 */
    static const uint8_t gain_order[4] =
    {
        VEML7700_GAIN_1_8, VEML7700_GAIN_1_4, VEML7700_GAIN_1, VEML7700_GAIN_2
    };
    static const uint8_t it_order[6] =
    {
        VEML7700_IT_25MS, VEML7700_IT_50MS, VEML7700_IT_100MS,
        VEML7700_IT_200MS, VEML7700_IT_400MS, VEML7700_IT_800MS
    };

    uint16_t raw = veml7700_raw_get();
    uint8_t  gi  = 0;
    uint8_t  ii  = 2;
    uint8_t  i;

    for(i = 0; i < 4u; i++)
    {
        if(gain_order[i] == gain_code())
        {
            gi = i;
        }
    }
    for(i = 0; i < 6u; i++)
    {
        if(it_order[i] == it_code())
        {
            ii = i;
        }
    }

    if(raw > 40000u)                        /* 太亮，降灵敏度 */
    {
        if(ii > 0u)
        {
            veml7700_set_it((veml7700_it_t)it_order[ii - 1u]);
            return 1;
        }
        if(gi > 0u)
        {
            veml7700_set_gain((veml7700_gain_t)gain_order[gi - 1u]);
            return 1;
        }
    }
    else if(raw < 200u)                     /* 太暗，加灵敏度 */
    {
        if(ii < 5u)
        {
            veml7700_set_it((veml7700_it_t)it_order[ii + 1u]);
            return 1;
        }
        if(gi < 3u)
        {
            veml7700_set_gain((veml7700_gain_t)gain_order[gi + 1u]);
            return 1;
        }
    }

    return 0;
}

uint8_t veml7700_level(uint8_t max_level)
{
    /* 1 lx ~ 10000 lx 分 9 段，够画一个"亮不亮"的柱子 */
    static const float th[9] =
    {
        1.0f, 3.0f, 10.0f, 30.0f, 100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f
    };

    float   lux = veml7700_lux_now();
    uint8_t lv  = 0;
    uint8_t i;

    if(max_level == 0u)
    {
        return 0;
    }

    for(i = 0; i < 9u; i++)
    {
        if(lux >= th[i])
        {
            lv = (uint8_t)(i + 1u);
        }
    }

    if(lv > max_level)
    {
        lv = max_level;
    }

    return lv;
}
