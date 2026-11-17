/******************************************************************************
 * 文件名   : boot_ui.c
 * 说明     : 开机启动屏幕
 ******************************************************************************/

#include "boot_ui.h"
#include "common.h"
#include "anime.h"

/**************************************************************************************************
 *                                        参数
 **************************************************************************************************/
#define BOOT_TOTAL_MS   1250
#define BOOT_TICK_MS    50
#define BOOT_BAR_MS     900

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
static lv_obj_t   *s_scr   = NULL;
static lv_timer_t *s_timer = NULL;
static uint32_t    s_elapsed = 0;
static uint8_t     s_done    = 0;

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
static void tick_cb(lv_timer_t *t)
{
    (void)t;

    s_elapsed += BOOT_TICK_MS;

    if(s_elapsed >= BOOT_TOTAL_MS)
    {
        s_done = 1;
    }
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
void boot_ui_show(void)
{
    lv_obj_t *logo;
    lv_obj_t *lbl;
    lv_obj_t *bar;

    if(s_scr != NULL)
    {
        return;
    }

    /* 1. splash 屏幕：糖果渐变底 */
    s_scr = lv_obj_create(lv_scr_act());        /* 覆盖层，不是独立 screen */
    lv_obj_set_style_bg_color(s_scr, UI_C_BG, 0);
    lv_obj_set_style_bg_grad_color(s_scr, UI_C_BG_BOTTOM, 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_set_pos(s_scr, 0, 0);
    lv_obj_set_size(s_scr, UI_W, UI_H);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);   /* 启动期间挡住触摸 */

    /* 2. 圆形 logo：草莓粉 -> 薄荷绿 竖向渐变 */
    logo = lv_obj_create(s_scr);
    lv_obj_set_size(logo, 104, 104);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -56);
    lv_obj_set_style_radius(logo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(logo, UI_C_TITLE, 0);
    lv_obj_set_style_bg_grad_color(logo, UI_C_ACCENT, 0);
    lv_obj_set_style_bg_grad_dir(logo, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(logo, 0, 0);
    lv_obj_set_style_pad_all(logo, 0, 0);
    lv_obj_clear_flag(logo, LV_OBJ_FLAG_SCROLLABLE);

    lbl = ui_text(logo, UI_FONT_HUGE, lv_color_hex(0xFFFFFF));
    ui_text_set(lbl, "Z");
    ui_text_align(lbl, LV_ALIGN_CENTER, 0, 0);

    /* 3. 标题：淡入 + 上浮 */
    lbl = ui_text(s_scr, UI_FONT_BIG, UI_C_TEXT);
    ui_text_set(lbl, "HELLO WORLD");
    ui_text_align(lbl, LV_ALIGN_CENTER, 0, 36);
    anime_fade(lbl, 0, 255, 420, 160);
    anime_move_y(lbl, 12, 0, 420, 160);

    /* 4. 副标题：再晚一点淡入，形成错落 */
    lbl = ui_text(s_scr, UI_FONT_SMALL, UI_C_TEXT_DIM);
    ui_text_set(lbl, "made in china");
    ui_text_align(lbl, LV_ALIGN_CENTER, 0, 64);
    anime_fade(lbl, 0, 255, 420, 280);

    /* 5. 进度条：从 0 填到 100 */
    bar = lv_bar_create(s_scr);
    lv_obj_set_size(bar, 150, 6);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 106);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, UI_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, UI_C_TITLE, LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(bar, BOOT_BAR_MS, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_bar_set_value(bar, 100, LV_ANIM_ON);

    /* 6. logo 弹出（最后调用，保证盖在其它元素之上） */
    anime_pop_in(logo, 40);

    s_elapsed = 0;
    s_done    = 0;
    s_timer   = lv_timer_create(tick_cb, BOOT_TICK_MS, NULL);
}

uint8_t boot_ui_done(void)
{
    return s_done;
}

void boot_ui_close(void)
{
    if(s_timer != NULL)
    {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }

    if(s_scr != NULL)
    {
        lv_obj_del(s_scr);
        s_scr = NULL;
    }
}