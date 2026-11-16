#include "lv_port_disp.h"
#include "lvgl.h"
#include <rtthread.h>

/* FSMC Bank1: PD12/A17 selects command or data. The ESAD2025 board
 * connects LCD_CS to PD11, so CS is held low as a GPIO. PD7/NE1 is
 * left free for the separate touch-clock signal. */
typedef struct { volatile uint16_t reg, ram; } lcd_bus_t;
#define LCD ((lcd_bus_t *)0x6003FFFEUL)
#define LCD_DMA_CHANNEL DMA2_Channel2
#define LCD_DMA_TIMEOUT_MS 30
#define LCD_BUF_PIXELS (LCD_HOR_RES * LCD_BUF_LINES)

#if LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP
#error "FSMC LCD requires native RGB565 halfwords"
#endif
#if LCD_BUF_PIXELS > 65535
#error "LCD buffer exceeds the DMA transfer counter"
#endif

static struct rt_mutex lcd_lock;
static rt_bool_t lcd_ready = RT_FALSE;
static volatile rt_bool_t lcd_diag_active = RT_FALSE;
/* 仅由 LVGL 线程访问。DMA 传输结束前保留像素缓冲区及 LCD 总线锁。 */
static struct {
    lv_disp_drv_t *drv;
    const lv_color_t *pixels;
    lv_area_t area;
    uint16_t count;
    rt_tick_t started;
} lcd_transfer;
static rt_bool_t lcd_dma_enabled = RT_TRUE;
static void cmd(uint8_t v) { LCD->reg = v; }
static void data(uint8_t v) { LCD->ram = v; }

static void bus_init(void)
{
    GPIO_InitTypeDef g = {0};
    FSMC_NORSRAMTimingInitTypeDef t = {0};
    FSMC_NORSRAMInitTypeDef f = {0};
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_FSMC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE, ENABLE);
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Mode = GPIO_Mode_AF_PP;
    /* D0-D3, RD, WR, D13-D15, DC/A17. PD7 is not LCD CS here. */
    g.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_4 | GPIO_Pin_5 |
                 GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 |
                 GPIO_Pin_12 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_Init(GPIOD, &g);
    /* D4-D12 */
    g.GPIO_Pin = GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 |
                 GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 |
                 GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_Init(GPIOE, &g);
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Pin = LCD_RST_PIN | LCD_CS_PIN;
    GPIO_Init(LCD_RST_PORT, &g);
    GPIO_SetBits(LCD_RST_PORT, LCD_RST_PIN);
    GPIO_ResetBits(LCD_CS_PORT, LCD_CS_PIN);
    g.GPIO_Pin = LCD_BL_PIN;
    GPIO_Init(LCD_BL_PORT, &g);
    GPIO_ResetBits(LCD_BL_PORT, LCD_BL_PIN);

    /* Deliberately conservative first-boot timing, in HCLK cycles. */
    t.FSMC_AddressSetupTime = 8;
    t.FSMC_AddressHoldTime = 1;
    t.FSMC_DataSetupTime = 24;
    t.FSMC_BusTurnAroundDuration = 1;
    t.FSMC_CLKDivision = 1;
    t.FSMC_AccessMode = FSMC_AccessMode_A;
    f.FSMC_Bank = FSMC_Bank1_NORSRAM1;
    f.FSMC_DataAddressMux = FSMC_DataAddressMux_Disable;
    f.FSMC_MemoryType = FSMC_MemoryType_SRAM;
    f.FSMC_MemoryDataWidth = FSMC_MemoryDataWidth_16b;
    f.FSMC_BurstAccessMode = FSMC_BurstAccessMode_Disable;
    f.FSMC_AsynchronousWait = FSMC_AsynchronousWait_Disable;
    f.FSMC_WriteOperation = FSMC_WriteOperation_Enable;
    f.FSMC_ExtendedMode = FSMC_ExtendedMode_Disable;
    f.FSMC_WaitSignal = FSMC_WaitSignal_Disable;
    f.FSMC_WriteBurst = FSMC_WriteBurst_Disable;
    f.FSMC_ReadWriteTimingStruct = &t;
    f.FSMC_WriteTimingStruct = &t;
    FSMC_NORSRAMInit(&f);
    FSMC_NORSRAMCmd(FSMC_Bank1_NORSRAM1, ENABLE);
}

static void window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    cmd(0x2A);
    data(x1 >> 8); data(x1); data(x2 >> 8); data(x2);
    cmd(0x2B);
    data(y1 >> 8); data(y1); data(y2 >> 8); data(y2);
    cmd(0x2C);
}

typedef struct { uint8_t op, count, arg[14]; } init_cmd_t;
/* ST7796 sequence adapted from the supplied Base/V3F parallel LCD. */
static const init_cmd_t init_seq[] = {
    {0xF0,1,{0xC3}}, {0xF0,1,{0x96}},
    {0x36,1,{0xE8}}, /* Landscape, BGR. For opposite rotation try 0x28. */
    {0x3A,1,{0x05}}, /* RGB565 on a 16-bit bus */
    {0xE8,8,{0x40,0x82,0x07,0x18,0x27,0x0A,0xB6,0x33}},
    {0xC5,1,{0x27}}, {0xC2,1,{0xA7}},
    {0xE0,14,{0xF0,0x01,0x06,0x0F,0x12,0x1D,0x36,
               0x54,0x44,0x0C,0x18,0x16,0x13,0x15}},
    {0xE1,14,{0xF0,0x01,0x05,0x0A,0x0B,0x07,0x32,
               0x44,0x44,0x0C,0x18,0x17,0x13,0x16}},
    {0xF0,1,{0x3C}}, {0xF0,1,{0x69}}, {0x29,0,{0}}
};

static void panel_init(void)
{
    unsigned i, j;
    rt_thread_mdelay(10);
    GPIO_ResetBits(LCD_RST_PORT, LCD_RST_PIN);
    rt_thread_mdelay(20);
    GPIO_SetBits(LCD_RST_PORT, LCD_RST_PIN);
    rt_thread_mdelay(120);
    cmd(0x11);
    rt_thread_mdelay(120);
    for (i = 0; i < sizeof(init_seq)/sizeof(init_seq[0]); i++)
    {
        cmd(init_seq[i].op);
        for (j = 0; j < init_seq[i].count; j++) data(init_seq[i].arg[j]);
    }
    rt_thread_mdelay(50);
    GPIO_SetBits(LCD_BL_PORT, LCD_BL_PIN);
}

void lcd_fill_screen(uint16_t color)
{
    uint32_t n = LCD_HOR_RES * LCD_VER_RES;
    if (!lcd_ready) return;
    if (rt_mutex_take(&lcd_lock, RT_WAITING_FOREVER) != RT_EOK) return;
    window(0, 0, LCD_HOR_RES-1, LCD_VER_RES-1);
    while (n--) LCD->ram = color;
    rt_mutex_release(&lcd_lock);
}

int lcd_diagnostic_active(void)
{
    return lcd_diag_active;
}

/* x controls the eight color bars; the final grid tests address continuity. */
static void diagnostic_pattern(rt_bool_t grid)
{
    static const uint16_t bars[8] = {
        0xF800, 0x07E0, 0x001F, 0xFFE0,
        0xF81F, 0x07FF, 0xFFFF, 0x0000
    };
    uint16_t x, y, color;

    if (rt_mutex_take(&lcd_lock, RT_WAITING_FOREVER) != RT_EOK) return;
    window(0, 0, LCD_HOR_RES - 1, LCD_VER_RES - 1);
    for (y = 0; y < LCD_VER_RES; y++)
    {
        for (x = 0; x < LCD_HOR_RES; x++)
        {
            if (grid)
            {
                color = (x == 0 || y == 0 ||
                         x == LCD_HOR_RES - 1 || y == LCD_VER_RES - 1 ||
                         x % 40 == 0 || y % 40 == 0) ? 0xFFFF : 0x0000;
            }
            else
            {
                color = bars[(uint32_t)x * 8 / LCD_HOR_RES];
            }
            LCD->ram = color;
        }
    }
    rt_mutex_release(&lcd_lock);
}

int lcd_run_diagnostic(void)
{
    static const uint16_t colors[] = {
        0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000
    };
    static const char *names[] = {
        "RED", "GREEN", "BLUE", "WHITE", "BLACK"
    };
    unsigned i;

    if (!lcd_ready)
    {
        rt_kprintf("lcd_test: display is not initialized yet\r\n");
        return -RT_ERROR;
    }
    lcd_diag_active = RT_TRUE;
    rt_thread_mdelay(20); /* Let the LVGL thread finish its current flush. */
    for (i = 0; i < sizeof(colors) / sizeof(colors[0]); i++)
    {
        rt_kprintf("lcd_test: %s\r\n", names[i]);
        lcd_fill_screen(colors[i]);
        rt_thread_mdelay(1200);
    }
    rt_kprintf("lcd_test: COLOR BARS\r\n");
    diagnostic_pattern(RT_FALSE);
    rt_thread_mdelay(2500);
    rt_kprintf("lcd_test: GRID AND BORDER\r\n");
    diagnostic_pattern(RT_TRUE);
    rt_thread_mdelay(2500);
    lcd_diag_active = RT_FALSE;
    rt_kprintf("lcd_test: done, returning to LVGL\r\n");
    return RT_EOK;
}

static void write_pixels(const lv_area_t *area, const lv_color_t *pixels, uint32_t count)
{
    window(area->x1, area->y1, area->x2, area->y2);
    while (count--) LCD->ram = (pixels++)->full;
}

static void lcd_dma_init(void)
{
    DMA_InitTypeDef config = {0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
    DMA_DeInit(LCD_DMA_CHANNEL);
    config.DMA_PeripheralBaseAddr = (uint32_t)(uintptr_t)&LCD->ram;
    config.DMA_BufferSize = 1; /* 启动每次刷新前会写入实际像素数。 */
    config.DMA_DIR = DMA_DIR_PeripheralDST;
    config.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    config.DMA_MemoryInc = DMA_MemoryInc_Enable;
    config.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    config.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    config.DMA_Mode = DMA_Mode_Normal;
    config.DMA_Priority = DMA_Priority_Medium;
    config.DMA_M2M = DMA_M2M_Enable;
    DMA_Init(LCD_DMA_CHANNEL, &config);
}

static rt_bool_t finish_flush(void)
{
    lv_disp_drv_t *drv = lcd_transfer.drv;
    rt_bool_t failed;

    if (drv == NULL) return RT_TRUE;
    failed = DMA_GetFlagStatus(DMA2_FLAG_TE2) != RESET;
    if (!failed && DMA_GetFlagStatus(DMA2_FLAG_TC2) == RESET)
    {
        if ((rt_tick_t)(rt_tick_get() - lcd_transfer.started) <
            rt_tick_from_millisecond(LCD_DMA_TIMEOUT_MS))
            return RT_FALSE;
        failed = RT_TRUE;
    }

    DMA_Cmd(LCD_DMA_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA2_FLAG_GL2);
    if (failed)
    {
        lcd_dma_enabled = RT_FALSE;
        /* LCD 写入位置可能已前移，重新设置窗口并补写整个区域。 */
        write_pixels(&lcd_transfer.area, lcd_transfer.pixels, lcd_transfer.count);
        rt_kprintf("[LVGL] LCD DMA error/timeout; using CPU flush\r\n");
    }
    lcd_transfer.drv = NULL;
    rt_mutex_release(&lcd_lock);
    lv_disp_flush_ready(drv);
    return RT_TRUE;
}

void lv_port_disp_wait_pending(void)
{
    while (!finish_flush()) rt_thread_mdelay(1);
}

static void disp_wait(lv_disp_drv_t *drv)
{
    (void)drv;
    lv_port_disp_wait_pending();
}

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors)
{
    uint32_t count = (uint32_t)lv_area_get_width(area) * lv_area_get_height(area);

    if (lcd_diag_active || rt_mutex_take(&lcd_lock, RT_WAITING_FOREVER) != RT_EOK)
    {
        lv_disp_flush_ready(drv);
        return;
    }
    if (!lcd_diag_active && lcd_dma_enabled && count > 0 && count <= LCD_BUF_PIXELS)
    {
        window(area->x1, area->y1, area->x2, area->y2);
        lcd_transfer.drv = drv;
        lcd_transfer.area = *area;
        lcd_transfer.pixels = colors;
        lcd_transfer.count = (uint16_t)count;
        lcd_transfer.started = rt_tick_get();

        /* 源端逐个读取 RGB565，目标地址固定为 FSMC LCD 数据口。 */
        LCD_DMA_CHANNEL->MADDR = (uint32_t)(uintptr_t)colors;
        DMA_SetCurrDataCounter(LCD_DMA_CHANNEL, lcd_transfer.count);
        __asm volatile ("fence iorw, iorw" ::: "memory");
        DMA_Cmd(LCD_DMA_CHANNEL, ENABLE);
        return; /* LVGL 此时可以绘制另一块缓冲区。 */
    }
    if (!lcd_diag_active) write_pixels(area, colors, count);
    rt_mutex_release(&lcd_lock);
    lv_disp_flush_ready(drv);
}

void lv_port_disp_init(void)
{
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf_a[LCD_BUF_PIXELS] __attribute__((aligned(4)));
    static lv_color_t buf_b[LCD_BUF_PIXELS] __attribute__((aligned(4)));
    static lv_disp_drv_t drv;
    if (rt_mutex_init(&lcd_lock, "lcd", RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        rt_kprintf("[LVGL] LCD mutex failed\r\n");
        return;
    }
    bus_init();
    lcd_dma_init();
    panel_init();
    lcd_ready = RT_TRUE;
    lv_disp_draw_buf_init(&draw_buf, buf_a, buf_b, LCD_BUF_PIXELS);
    lv_disp_drv_init(&drv);
    drv.hor_res = LCD_HOR_RES;
    drv.ver_res = LCD_VER_RES;
    drv.flush_cb = disp_flush;
    drv.wait_cb = disp_wait;
    drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&drv);
    rt_kprintf("[LVGL] ST7796 FSMC %dx%d, DMA2 CH2, 2 x %u bytes\r\n",
               LCD_HOR_RES, LCD_VER_RES, (unsigned)sizeof(buf_a));
}
