/**
 * @file lvgl_app.c
 * LVGL 应用层：初始化 + 主循环线程 + 示例界面
 *
 * 系统启动流程：
 *   rt_components_init() -> INIT_APP_EXPORT -> lvgl_app_init()
 *        -> 创建 "lvgl" 线程 -> lv_init() -> 显示初始化 -> 建界面 -> 主循环
 */

#include "lvgl_app.h"
#include "lv_port_disp.h"
#include "lvgl.h"
#include "../UI/task_menu.h"

#include <rtthread.h>

/* ESAD2025 P1: PD2=INT, PD3=RST, PD7=SCL, PD6=SDA.
 * The touch bus is separate from the LCD FSMC data bus. */
#define TP_PORT        GPIOD
#define TP_INT_PIN     GPIO_Pin_2
#define TP_RST_PIN     GPIO_Pin_3
#define TP_SDA_PIN     GPIO_Pin_6
#define TP_SCL_PIN     GPIO_Pin_7
#define GT911_ADDR     0x5D

static rt_bool_t touch_ready = RT_FALSE;
static rt_bool_t touch_pressed = RT_FALSE;
static lv_point_t touch_point = {0, 0};
static rt_uint16_t touch_raw_x = 0, touch_raw_y = 0;
static rt_tick_t touch_last_tick = 0;

/* A local delay avoids Delay_Us(), which reprograms RT-Thread's SysTick. */
static void touch_i2c_delay(void)
{
    volatile unsigned int n;
    for (n = 0; n < 100; n++) { }
}

static void touch_sda_high(void) { TP_PORT->BSHR = TP_SDA_PIN; }
static void touch_sda_low(void)  { TP_PORT->BCR  = TP_SDA_PIN; }
static void touch_scl_low(void)  { TP_PORT->BCR  = TP_SCL_PIN; }

/* Release SCL and allow a bounded amount of clock stretching. */
static rt_bool_t touch_scl_high(void)
{
    unsigned int n;
    TP_PORT->BSHR = TP_SCL_PIN;
    for (n = 0; n < 1000; n++)
    {
        if (TP_PORT->INDR & TP_SCL_PIN) return RT_TRUE;
        touch_i2c_delay();
    }
    return RT_FALSE;
}

static rt_bool_t touch_i2c_start(void)
{
    touch_sda_high();
    if (!touch_scl_high()) return RT_FALSE;
    touch_i2c_delay();
    if (!(TP_PORT->INDR & TP_SDA_PIN)) return RT_FALSE;
    touch_sda_low();
    touch_i2c_delay();
    touch_scl_low();
    return RT_TRUE;
}

static void touch_i2c_stop(void)
{
    touch_sda_low();
    touch_i2c_delay();
    (void)touch_scl_high();
    touch_i2c_delay();
    touch_sda_high();
    touch_i2c_delay();
}

static rt_bool_t touch_i2c_write_byte(rt_uint8_t value)
{
    unsigned int bit;
    rt_bool_t ack;
    for (bit = 0; bit < 8; bit++)
    {
        if (value & 0x80) touch_sda_high();
        else              touch_sda_low();
        touch_i2c_delay();
        if (!touch_scl_high()) return RT_FALSE;
        touch_i2c_delay();
        touch_scl_low();
        value <<= 1;
    }
    touch_sda_high();                 /* Release SDA for the slave's ACK. */
    touch_i2c_delay();
    if (!touch_scl_high()) return RT_FALSE;
    touch_i2c_delay();
    ack = (TP_PORT->INDR & TP_SDA_PIN) == 0;
    touch_scl_low();
    return ack;
}

static rt_uint8_t touch_i2c_read_byte(rt_bool_t ack)
{
    rt_uint8_t value = 0;
    unsigned int bit;
    touch_sda_high();
    for (bit = 0; bit < 8; bit++)
    {
        touch_i2c_delay();
        (void)touch_scl_high();
        touch_i2c_delay();
        value = (rt_uint8_t)((value << 1) | ((TP_PORT->INDR & TP_SDA_PIN) != 0));
        touch_scl_low();
    }
    if (ack) touch_sda_low();
    else     touch_sda_high();
    touch_i2c_delay();
    (void)touch_scl_high();
    touch_i2c_delay();
    touch_scl_low();
    touch_sda_high();
    return value;
}

/* GT911 register addresses are sent high byte first, then low byte. */
static rt_bool_t gt911_read(rt_uint16_t reg, rt_uint8_t *buf, unsigned int len)
{
    unsigned int i;
    rt_bool_t ok = RT_FALSE;
    if (!touch_i2c_start()) goto done;
    if (!touch_i2c_write_byte((GT911_ADDR << 1) | 0)) goto done;
    if (!touch_i2c_write_byte((rt_uint8_t)(reg >> 8))) goto done;
    if (!touch_i2c_write_byte((rt_uint8_t)reg)) goto done;
    if (!touch_i2c_start()) goto done;
    if (!touch_i2c_write_byte((GT911_ADDR << 1) | 1)) goto done;
    for (i = 0; i < len; i++) buf[i] = touch_i2c_read_byte(i + 1 < len);
    ok = RT_TRUE;
done:
    touch_i2c_stop();
    return ok;
}

static rt_bool_t gt911_write_u8(rt_uint16_t reg, rt_uint8_t value)
{
    rt_bool_t ok = RT_FALSE;
    if (!touch_i2c_start()) goto done;
    if (!touch_i2c_write_byte(GT911_ADDR << 1)) goto done;
    if (!touch_i2c_write_byte((rt_uint8_t)(reg >> 8))) goto done;
    if (!touch_i2c_write_byte((rt_uint8_t)reg)) goto done;
    if (!touch_i2c_write_byte(value)) goto done;
    ok = RT_TRUE;
done:
    touch_i2c_stop();
    return ok;
}

static void gt911_lvgl_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    rt_uint8_t status;
    rt_uint8_t xy[4];
    rt_uint16_t raw_x, raw_y;
    (void)drv;

    if (!gt911_read(0x814E, &status, 1))
    {
        touch_pressed = RT_FALSE;
    }
    else if (status & 0x80)
    {
        if ((status & 0x0F) >= 1 && (status & 0x0F) <= 5 &&
            gt911_read(0x8150, xy, sizeof(xy)))
        {
            raw_x = (rt_uint16_t)(xy[0] | (xy[1] << 8));
            raw_y = (rt_uint16_t)(xy[2] | (xy[3] << 8));
            touch_raw_x = raw_x;
            touch_raw_y = raw_y;
            /* LCD MADCTL=0xE8: portrait touch axes rotate into landscape. */
            if (raw_x < LCD_VER_RES && raw_y < LCD_HOR_RES)
            {
                touch_point.x = LCD_HOR_RES - 1 - raw_y;
                touch_point.y = raw_x;
                touch_pressed = RT_TRUE;
                touch_last_tick = rt_tick_get();
            }
            else touch_pressed = RT_FALSE;
        }
        else touch_pressed = RT_FALSE;
        (void)gt911_write_u8(0x814E, 0); /* Acknowledge the frame. */
    }
    else if ((rt_tick_get() - touch_last_tick) > RT_TICK_PER_SECOND / 5)
    {
        touch_pressed = RT_FALSE;
    }

    data->point = touch_point;
    data->state = touch_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

static int lv_port_indev_init(void)
{
    GPIO_InitTypeDef gpio;
    rt_uint8_t id[4];
    lv_indev_drv_t drv;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    gpio.GPIO_Pin = TP_SDA_PIN | TP_SCL_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TP_PORT, &gpio);
    touch_sda_high();
    TP_PORT->BSHR = TP_SCL_PIN;

    gpio.GPIO_Pin = TP_INT_PIN | TP_RST_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(TP_PORT, &gpio);
    TP_PORT->BCR = TP_INT_PIN | TP_RST_PIN; /* INT low selects I2C address 0x5D. */
    rt_thread_mdelay(20);
    TP_PORT->BSHR = TP_RST_PIN;
    rt_thread_mdelay(5);
    gpio.GPIO_Pin = TP_INT_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(TP_PORT, &gpio);
    rt_thread_mdelay(60);

    if (!(TP_PORT->INDR & TP_SDA_PIN) || !(TP_PORT->INDR & TP_SCL_PIN))
    {
        rt_kprintf("[GT911] I2C lines not high: check SDA/SCL and 3.3V pull-ups\r\n");
        return -RT_ERROR;
    }
    if (!gt911_read(0x8140, id, sizeof(id)))
    {
        rt_kprintf("[GT911] no response at I2C 7-bit address 0x5D\r\n");
        return -RT_ERROR;
    }
    rt_kprintf("[GT911] product ID: %c%c%c%c\r\n", id[0], id[1], id[2], id[3]);
    if (id[0] != '9' || id[1] != '1' || id[2] != '1')
    {
        rt_kprintf("[GT911] unexpected controller; touch input disabled\r\n");
        return -RT_ERROR;
    }
    if (!gt911_write_u8(0x814E, 0))
    {
        rt_kprintf("[GT911] cannot clear touch status\r\n");
        return -RT_ERROR;
    }

    lv_indev_drv_init(&drv);
    drv.type = LV_INDEV_TYPE_POINTER;
    drv.read_cb = gt911_lvgl_read;
    if (lv_indev_drv_register(&drv) == NULL)
    {
        rt_kprintf("[GT911] LVGL input registration failed\r\n");
        return -RT_ERROR;
    }
    touch_ready = RT_TRUE;
    rt_kprintf("[GT911] LVGL touch input ready\r\n");
    return RT_EOK;
}

/*==================================================================
 * 线程参数
 *================================================================*/
#define LVGL_THREAD_STACK   3072    /* 栈：LVGL 渲染调用较深，至少 3KB */
#define LVGL_THREAD_PRIO    10      /* 优先级（数值越小越高） */
#define LVGL_THREAD_TICK    10      /* 时间片 */

void lvgl_demo_create(void)
{
    task_menu_create();
}

/*==================================================================
 * LVGL 主循环线程
 *================================================================*/
static void lvgl_console_log(const char *message)
{
    rt_kprintf("%s", message);
}

static void lvgl_thread_entry(void *param)
{
    rt_bool_t was_diagnostic = RT_FALSE;
    (void)param;

    lv_log_register_print_cb(lvgl_console_log);
    lv_init();                  /* 1. LVGL 内核初始化 */
    lv_port_disp_init();        /* 2. 显示设备（FSMC 16 位并口） */
    (void)lv_port_indev_init(); /* 3. GT911 触摸；失败时界面仍可显示 */
    lvgl_demo_create();         /* 4. 建界面 */


    

    rt_kprintf("[LVGL] init done, entering main loop\r\n");

    while (1)
    {
        if (lcd_diagnostic_active())
        {
            was_diagnostic = RT_TRUE;
            rt_thread_mdelay(5);
            continue;
        }
        if (was_diagnostic)
        {
            lv_obj_invalidate(lv_scr_act()); /* Redraw the UI after the test. */
            was_diagnostic = RT_FALSE;
        }
        lv_timer_handler();     /* ★ LVGL 主循环：处理定时器 + 重绘脏区域 */
        lv_port_disp_wait_pending();
        rt_thread_mdelay(5);    /* 5ms 一次（LVGL 官方建议 1~10ms） */
    }
}

/*==================================================================
 * 入口
 *================================================================*/
int lvgl_app_init(void)
{
    rt_thread_t tid;

    tid = rt_thread_create("lvgl", lvgl_thread_entry, RT_NULL,
                           LVGL_THREAD_STACK, LVGL_THREAD_PRIO, LVGL_THREAD_TICK);
    if (tid == RT_NULL)
    {
        rt_kprintf("[LVGL] ERROR: thread create failed! heap too small?\r\n");
        return -RT_ERROR;
    }

    rt_thread_startup(tid);
    rt_kprintf("[LVGL] thread started\r\n");
    return RT_EOK;
}
/* 自动启动：系统初始化时就会调用（不需要在 main 里手动调） */
INIT_APP_EXPORT(lvgl_app_init);

/*==================================================================
 * 硬件自检命令：msh 里敲 lcd_test
 * 纯色 -> 色条 -> 网格；检查像素、颜色与边缘。
 * 本命令只检查显示；触摸可用下方 touch_test 查看。
 *================================================================*/
static int lcd_test(int argc, char **argv)
{
    (void)argc; (void)argv;
    return lcd_run_diagnostic();
}
MSH_CMD_EXPORT(lcd_test, LCD solids colorbars grid);

/* 查看触摸初始化和最近一次坐标；不在 shell 线程中访问 I2C/LVGL。 */
static int touch_test(int argc, char **argv)
{
    (void)argc; (void)argv;
    rt_kprintf("[GT911] ready=%d pressed=%d raw=(%d,%d) screen=(%d,%d)\r\n",
               touch_ready, touch_pressed, touch_raw_x, touch_raw_y,
               touch_point.x, touch_point.y);
    return touch_ready ? RT_EOK : -RT_ERROR;
}
MSH_CMD_EXPORT(touch_test, Show GT911 touch state and last point);
