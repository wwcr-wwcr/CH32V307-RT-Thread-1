/******************************************************************************
 * 文件名   : task_example_ui.c
 * 说明     : EXAMPLE 页面 —— 麦克风 / 环境光的"时间坐标轴"曲线
 *
 * 版式（320x480 竖屏）：
 *     标题栏       MANUAL / 6.4s
 *     三张数据卡   MIC（0~4095） LUX（lux） 7SEG（数码管倒数）
 *     曲线区       两条线共用一块图，横轴是时间（-6.4s ~ 0）
 *     横轴刻度     -6.4s -4.8s -3.2s -1.6s 0s（4 等分，跟网格对齐）
 *     图例 + 状态  MIC / LUX 颜色对照，K1 状态和音乐/节拍开关
 *     三个按钮     PAUSE / MUSIC / BEAT（触摸，等价于 K1/K2/K3）
 *
 * 64 个点 × 100ms = 6.4 秒窗口，采样节拍在 task_example.c 里定。
 * 所有控件都来自 ui/common.h，本文件不 new 任何 lv_ 对象。
 ******************************************************************************/

#include "task_example_ui.h"
#include "common.h"
#include "anime.h"
#include <stdio.h>
/**************************************************************************************************
 *                                        控件编号
 **************************************************************************************************/
/* 触摸按钮的编号，task_example.c 的 on_set() 按这个号分派 */
#define UI_ID_PAUSE         1
#define UI_ID_MUSIC         2
#define UI_ID_BEAT          3

/* 曲线两条线的颜色，图例和曲线共用 */
#define UI_C_MIC_LINE       0x00A0C8
#define UI_C_LUX_LINE       0xFFA300

/**************************************************************************************************
 *                                       页面状态
 **************************************************************************************************/
static lv_obj_t  *s_page = NULL;
static lv_obj_t  *s_mic  = NULL;        /* 卡片里的数值 label */
static lv_obj_t  *s_lux  = NULL;
static lv_obj_t  *s_cnt  = NULL;
static lv_obj_t  *s_hint = NULL;        /* 底部状态条 */
static ui_plot_t *s_plot = NULL;

static uint8_t    s_seq  = 0xFF;        /* 上次画进去的采样序号（0xFF 保证第一帧必画） */

/* 横轴刻度：4 等分，和 ui_plot_axis(4, 4, ...) 的竖网格线对齐。
 * 位置用"-70px / +70px"这种相对页面中线的偏移，不依赖字宽。 */
static const char *const s_tick_txt[5] =
{
    "-6.4s", "-4.8s", "-3.2s", "-1.6s", "0s"
};

static const lv_coord_t s_tick_x[5] = { -140, -70, 0, 70, 140 };

/**************************************************************************************************
 *                                        建页
 **************************************************************************************************/
static void page_build(void)
{
    uint8_t i;

    s_page = ui_page_create();

    ui_title_bar(s_page, "MIC / LUX", "6.4s");

    /* 一行三张卡：麦克风、照度、数码管当前值 */
    s_mic = ui_card(s_page,  12, 64, 96, 88, "MIC",  "--");
    s_lux = ui_card(s_page, 112, 64, 96, 88, "LUX",  "--");
    s_cnt = ui_card(s_page, 212, 64, 96, 88, "7SEG", "9999");

    anime_pop_in(s_mic, 0);
    anime_pop_in(s_lux, 60);
    anime_pop_in(s_cnt, 120);

    /* 曲线：两条线量程各自独立，MIC 用 ADC 满量程，LUX 用 0~1000 */
    s_plot = ui_plot_create(s_page, 20, 162, 280, 146);
    ui_plot_axis(s_plot, 4, 4, 0xE8DEE6);
    ui_plot_line(s_plot, 0, &(ui_plot_line_t){
            .color = UI_C_MIC_LINE, .width = 2,
            .style = UI_LINE_SOLID,
            .vmin  = 0, .vmax = 4095 });
    ui_plot_line(s_plot, 1, &(ui_plot_line_t){
            .color = UI_C_LUX_LINE, .width = 2,
            .style = UI_LINE_SOLID,
            .vmin  = 0, .vmax = 1000 });

    /* 横轴刻度（时间） */
    for(i = 0; i < 5; i++)
    {
        lv_obj_t *t = ui_text(s_page, UI_FONT_SMALL, UI_C_TEXT_DIM);

        ui_text_set(t, s_tick_txt[i]);
        ui_text_align(t, LV_ALIGN_TOP_MID, s_tick_x[i], 312);
    }

    /* 图例：左边麦克风，右边照度 */
    {
        lv_obj_t *lg = ui_text(s_page, UI_FONT_SMALL, lv_color_hex(UI_C_MIC_LINE));

        ui_text_set(lg, "MIC 0-4095");
        ui_text_align(lg, LV_ALIGN_TOP_LEFT, 24, 334);

        lg = ui_text(s_page, UI_FONT_SMALL, lv_color_hex(UI_C_LUX_LINE));
        ui_text_set(lg, "LUX 0-1000");
        ui_text_align(lg, LV_ALIGN_TOP_RIGHT, -24, 334);
    }

    /* 触摸按钮，和 K1/K2/K3 一一对应 */
    (void)ui_button(s_page,  14, 364, 92, 46, "PAUSE", UI_ID_PAUSE);
    (void)ui_button(s_page, 114, 364, 92, 46, "MUSIC", UI_ID_MUSIC);
    (void)ui_button(s_page, 214, 364, 92, 46, "BEAT",  UI_ID_BEAT);

    s_hint = ui_hint_bar(s_page, "RUN  MUS ON  BEA ON");
}

/**************************************************************************************************
 *                                        对外接口
 **************************************************************************************************/
/**
 * @brief  每帧渲染（UI 线程，10ms 一次）
 * @note   seq 变了才往曲线里塞点，10ms 的 render 不会把同一份数据画 10 遍
 */
void example_ui_render(uint16_t mic, uint16_t lux, uint16_t count,
                       uint8_t run, uint8_t music, uint8_t beat, uint8_t seq)
{
    char buf[32];

    if(s_page == NULL)
    {
        page_build();
    }

    if(!ui_page_is_active(s_page))
    {
        ui_page_load(s_page);
    }

    /* ---- 数值卡 ---- */
    ui_text_num(s_mic, (int32_t)mic,   NULL);
    ui_text_num(s_lux, (int32_t)lux,   NULL);
    ui_text_num(s_cnt, (int32_t)count, NULL);

    /* ---- 曲线：新采样才推一个点 ---- */
    if(seq != s_seq)
    {
        s_seq = seq;

        ui_plot_push(s_plot, 0, (int32_t)mic);
        ui_plot_push(s_plot, 1, (int32_t)((lux > 1000u) ? 1000u : lux));   /* 超量程夹住，免得顶平 */
        ui_plot_refresh(s_plot);
    }

    /* ---- 底部状态条 ---- */
    snprintf(buf, sizeof(buf), "%s  MUS %s  BEA %s",
             (run   != 0u) ? "RUN"   : "PAUSE",
             (music != 0u) ? "ON"    : "OFF",
             (beat  != 0u) ? "ON"    : "OFF");
    ui_text_set(s_hint, buf);
}

/**
 * @brief  任务退出时销毁页面
 * @note   plot 必须先删，它挂在页面上，页面先走会留下野指针
 */
void example_ui_destroy(void)
{
    ui_plot_delete(s_plot);

    ui_page_destroy(&s_page);

    s_mic  = NULL;
    s_lux  = NULL;
    s_cnt  = NULL;
    s_hint = NULL;
    s_plot = NULL;
    s_seq  = 0xFF;
}
