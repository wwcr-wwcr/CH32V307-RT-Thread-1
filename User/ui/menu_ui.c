/******************************************************************************
 * 文件名   : menu_ui.c
 * 说明     : 选择页界面：一屏一个 TASK n，左右切换
 *
 * 触摸支持：
 *     左右两个箭头按钮 + 卡片本身都可以点。回调只做"合成按键事件"，
 *     和实体按键 K1/K2/K3 走完全相同的路径，菜单逻辑一行都不用改。
 ******************************************************************************/

#include "menu_ui.h"
#include "common.h"
#include "anime.h"
#include <stdio.h>

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
static void (*s_cb_prev)(void)  = NULL;
static void (*s_cb_next)(void)  = NULL;
static void (*s_cb_enter)(void) = NULL;

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* 按下反馈统一走 anime.c，本文件不出现任何 lv_anim_* */
static void on_press(lv_event_t *e) { anime_press(lv_event_get_target(e)); }
static void on_prev(lv_event_t *e)  { (void)e; if(s_cb_prev  != NULL) { s_cb_prev();  } }
static void on_next(lv_event_t *e)  { (void)e; if(s_cb_next  != NULL) { s_cb_next();  } }
static void on_enter(lv_event_t *e) { (void)e; if(s_cb_enter != NULL) { s_cb_enter(); } }

/* 造一个方形触摸按钮：深色卡片 + 边框 + 居中箭头 */
static lv_obj_t *make_btn(lv_obj_t *parent, lv_coord_t x, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = ui_panel(parent, x, 335, 56, 56, UI_C_CARD);
    lv_obj_t *lbl;

    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, UI_C_BORDER, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn, on_press, LV_EVENT_PRESSED, NULL);

    lbl = ui_text(btn, UI_FONT_HUGE, UI_C_TEXT);
    ui_text_set(lbl, text);
    ui_text_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return btn;
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
void menu_ui_set_tap_cb(void (*on_prev_cb)(void), void (*on_next_cb)(void), void (*on_enter_cb)(void))
{
    s_cb_prev  = on_prev_cb;
    s_cb_next  = on_next_cb;
    s_cb_enter = on_enter_cb;
}

void menu_ui_render(uint8_t index, uint8_t total, const char *name)
{
    static lv_obj_t *s_page    = NULL;
    static lv_obj_t *s_pagebar = NULL;
    static lv_obj_t *s_card    = NULL;
    static lv_obj_t *s_task    = NULL;
    static lv_obj_t *s_name    = NULL;
    static uint8_t   s_last    = 0xFF;

    char   buf[24];
    int8_t dir;

    /* 1. 首次进入：搭界面 + 播进入动画 */
    if(s_card == NULL)
    {
        lv_obj_t *root;

        s_page = ui_page_create();
        root = s_page;

        s_pagebar = ui_title_bar(root, "SELECT", "1/1");

        /* 卡片：点一下 = 进入当前任务 */
        s_card = ui_panel(root, 30, 165, (lv_coord_t)(UI_W - 60), 150, UI_C_CARD);
        lv_obj_set_style_border_width(s_card, 2, 0);
        lv_obj_set_style_border_color(s_card, UI_C_BORDER, 0);
        lv_obj_add_flag(s_card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_card, on_enter, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(s_card, on_press, LV_EVENT_PRESSED, NULL);

        s_task = ui_text(s_card, UI_FONT_HUGE, UI_C_TEXT);
        ui_text_align(s_task, LV_ALIGN_TOP_MID, 0, 26);

        s_name = ui_text(s_card, UI_FONT_BIG, UI_C_ACCENT);
        ui_text_align(s_name, LV_ALIGN_BOTTOM_MID, 0, -26);

        /* 左右箭头：点一下 = 上一个 / 下一个 */
        make_btn(root, 24, "<", on_prev);
        make_btn(root, (lv_coord_t)(UI_W - 24 - 56), ">", on_next);

        ui_hint_bar(root, "TAP CARD TO ENTER");
    }

    /* 从别的页面切回来：重新激活并重播进入动画 */
    if(!ui_page_is_active(s_page))
    {
        ui_page_load(s_page);
        anime_page_in(s_card);
    }

    /* 2. 没变化就不动，省一次重绘 */
    if(index == s_last)
    {
        return;
    }

    /* 3. 更新文字 */
    snprintf(buf, sizeof(buf), "TASK %d", index + 1);
    ui_text_set(s_task, buf);
    ui_text_set(s_name, name);

    snprintf(buf, sizeof(buf), "%d/%d", index + 1, total);
    ui_text_set(s_pagebar, buf);

    /* 4. 不是第一次的话，播切换动画（方向按索引变化判断） */
    if(s_last != 0xFF)
    {
        dir = (index > s_last) ? 1 : -1;
        anime_page_switch(s_card, dir);
    }

    s_last = index;
}