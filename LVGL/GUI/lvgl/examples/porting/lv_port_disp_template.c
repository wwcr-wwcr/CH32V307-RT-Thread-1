/**
 * @file lv_port_disp.c
 * @brief LVGL 显示接口（ST7796S 320x480，FSMC 并口）
 *
 * 刷屏方式二选一（改下面的 LCD_DISP_USE_DMA）：
 *
 *   0 = 同步刷：CPU 循环写 LCD->LCD_RAM，简单但会阻塞 UI 线程
 *   1 = DMA 刷：DMA2_Channel1 + 双缓冲，
 *              - 内存 -> FSMC 数据寄存器，M2M 模式，源地址递增、目地址固定
 *              - 传输完成中断里才调 lv_disp_flush_ready()，所以不会撕裂
 *              - CPU 在搬运期间可以去跑别的线程/动画，UI 更跟手
 *
 * 通道选择：DAC 任务用的是 DMA2_Channel4，这里用 Channel1，互不影响；
 *           ADC 用的是 DMA1_Channel1，也在另一组控制器上。
 *
 * 内存：双缓冲 2 x 320x10 x 2B = 12.8KB
 */

#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp_template.h"
#include "../../lvgl.h"
#include "lcd.h"
#include "rtthread.h"
#include "rthw.h"

/*********************
 *      DEFINES
 *********************/
#define MY_DISP_HOR_RES     320
#define MY_DISP_VER_RES     480
#define MY_DISP_BUF_LINES   10                              /* 局部缓冲行数 */
#define MY_DISP_BUF_SIZE    (MY_DISP_HOR_RES * MY_DISP_BUF_LINES)

#define LCD_DISP_USE_DMA    1                               /* 1 = DMA 刷屏，0 = CPU 同步刷 */
#define LCD_DMA_CH          DMA2_Channel1                   /* 空闲通道，和 DAC 的 Ch4 不冲突 */

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;

#if LCD_DISP_USE_DMA
/* 双缓冲：LVGL 往 A 画的时候，DMA 正在把 B 送出去 */
static lv_color_t s_buf1[MY_DISP_BUF_SIZE];
static lv_color_t s_buf2[MY_DISP_BUF_SIZE];
#else
static lv_color_t s_buf[MY_DISP_BUF_SIZE];
#endif

/**********************
 *   STATIC FUNCTIONS
 **********************/
#if LCD_DISP_USE_DMA
/* DMA 传输完成：这一块真正刷完了，告诉 LVGL 可以继续 */
void DMA2_Channel1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA2_Channel1_IRQHandler(void)
{
    GET_INT_SP();
    rt_interrupt_enter();

    if(DMA_GetITStatus(DMA2_IT_TC1) != RESET)
    {
        DMA_ClearITPendingBit(DMA2_IT_TC1);
        DMA_Cmd(LCD_DMA_CH, DISABLE);
        lv_disp_flush_ready(&s_disp_drv);
    }

    rt_interrupt_leave();
    FREE_INT_SP();
}

/* DMA2_Channel1：M2M 模式，PADDR=源(递增) -> MADDR=LCD 数据寄存器(固定) */
static void lcd_dma_init(void)
{
    DMA_InitTypeDef DMA_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);

    DMA_DeInit(LCD_DMA_CH);

    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)s_buf1;      /* flush 时再改 */
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)&LCD->LCD_RAM;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC; /* M2M：CPAR 是源 */
    DMA_InitStructure.DMA_BufferSize         = MY_DISP_BUF_SIZE;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Enable;   /* 源递增 */
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Disable;      /* 屏地址固定 */
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_Low;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Enable;
    DMA_Init(LCD_DMA_CH, &DMA_InitStructure);

    DMA_ITConfig(LCD_DMA_CH, DMA_IT_TC, ENABLE);
    DMA_ClearITPendingBit(DMA2_IT_TC1);

    NVIC_InitStructure.NVIC_IRQChannel                   = DMA2_Channel1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}
#endif /* LCD_DISP_USE_DMA */

/**
 * @brief  把 LVGL 画好的一块刷到屏上
 */
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t total = (uint32_t)(area->x2 - area->x1 + 1) * (uint32_t)(area->y2 - area->y1 + 1);

    (void)disp_drv;

    /* 1. 设绘图窗口（CPU 写 FSMC，命令先于像素到达，硬件保证顺序） */
    lcd_set_window((uint16_t)area->x1, (uint16_t)area->y1,
                   (uint16_t)area->x2, (uint16_t)area->y2);

#if LCD_DISP_USE_DMA
    /* 2. 起 DMA，传完在中断里 flush_ready */
    LCD_DMA_CH->PADDR = (uint32_t)color_p;      /* 源：LVGL 的这块缓冲 */
    DMA_SetCurrDataCounter(LCD_DMA_CH, (uint16_t)total);
    DMA_Cmd(LCD_DMA_CH, ENABLE);
#else
    /* 2. CPU 同步写 */
    lcd_write_data_buf((const uint16_t *)color_p, total);
    lv_disp_flush_ready(disp_drv);
#endif
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void lv_port_disp_init(void)
{
    if(lcd_init() != 0)
    {
        rt_kprintf("lv_port: lcd init failed\r\n");
    }

    lv_init();

#if LCD_DISP_USE_DMA
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, MY_DISP_BUF_SIZE);
    lcd_dma_init();
#else
    lv_disp_draw_buf_init(&s_draw_buf, s_buf, NULL, MY_DISP_BUF_SIZE);
#endif

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = MY_DISP_HOR_RES;
    s_disp_drv.ver_res  = MY_DISP_VER_RES;
    s_disp_drv.flush_cb = disp_flush;
    s_disp_drv.draw_buf = &s_draw_buf;

    lv_disp_drv_register(&s_disp_drv);
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif