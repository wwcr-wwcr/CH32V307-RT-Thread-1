/******************************************************************************
 * 文件名   : anime.c
 * 说明     : 动画实现（所有 lv_anim 细节都收在这里）
 *
 * 设计约束（重要）：
 *   1. 只用 LVGL 自带的 path 回调，不自己写曲线——
 *      自定义 path 一旦返回越界值，对象会停在奇怪的状态（例如永久透明）。
 *   2. 任何动画都不从 opa = 0 起步。最暗到 120（约 47%），
 *      即使动画被截断、或被别的事件打断，对象依然是看得见的。
 *   3. 不用 transform_zoom：它要给对象申请一整块图层缓冲，
 *      这块板子的 LVGL 池装不下，分配失败会直接死在 assert 里。
 ******************************************************************************/

#include "anime.h"

/**************************************************************************************************
 *                                        手感参数
 **************************************************************************************************/
#define T_PAGE_IN           360         /* 页面进入 */
#define T_PAGE_SWITCH       300         /* 左右切换 */
#define T_POP               380         /* 弹出 */
#define T_PRESS_BACK        220         /* 按压回弹 */
#define T_FLASH             260         /* 数值闪烁 */

#define SHIFT_IN            20          /* 进入时纵向位移 */
#define SHIFT_SWITCH        40          /* 切换时横向位移 */
#define POP_SHIFT           10          /* 弹出时纵向位移 */

#define OPA_MIN             120         /* 动画最暗程度：永远看得见 */
#define OPA_PRESS           150         /* 按下压暗到 */

#define T_SHAKE             90          /* 摆动单程时长 */
#define T_PULSE             520         /* 呼吸的半周期 */
#define T_FADE_OUT          300         /* 默认淡出时长 */
#define T_BG_FLASH          420         /* 背景色渐入 */
#define T_GROW              380         /* 展开（宽/高） */
#define T_LIST              320         /* 列表单项入场 */
#define T_LIST_STEP         60          /* 相邻两项的启动间隔 */
#define LIST_SHIFT          10          /* 列表入场时的初始位移 */
#define SHAKE_SHIFT         5           /* 摆动幅度 */

/**************************************************************************************************
 *                                        内部回调
 **************************************************************************************************/
static void cb_opa(void *o, int32_t v)
{
    if(v < OPA_MIN) { v = OPA_MIN; }
    if(v > 255)     { v = 255; }

    lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
}

static void cb_trans_x(void *o, int32_t v)
{
    lv_obj_set_style_translate_x((lv_obj_t *)o, (lv_coord_t)v, 0);
}

static void cb_trans_y(void *o, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)o, (lv_coord_t)v, 0);
}

static void anim_start(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to,
                       uint32_t ms, uint32_t delay, lv_anim_path_cb_t path)
{
    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
void anime_page_in(lv_obj_t *obj)
{
    if(obj == NULL)
    {
        return;
    }

    /* 只做位移，不碰透明度：进入效果靠"从下方 20px 滑上来"就够了 */
    lv_obj_set_style_translate_y(obj, SHIFT_IN, 0);

    anim_start(obj, cb_trans_y, SHIFT_IN, 0, T_PAGE_IN, 0, lv_anim_path_ease_out);
}

void anime_page_switch(lv_obj_t *obj, int8_t dir)
{
    if(obj == NULL)
    {
        return;
    }

    anim_start(obj, cb_trans_x, dir * SHIFT_SWITCH, 0, T_PAGE_SWITCH, 0, lv_anim_path_ease_out);
}

void anime_pop_in(lv_obj_t *obj, uint32_t delay_ms)
{
    if(obj == NULL)
    {
        return;
    }

    /* 从 120 起淡入 + 上浮一点，带轻微过冲；最暗也是 47% 可见 */
    lv_obj_set_style_opa(obj, OPA_MIN, 0);
    lv_obj_set_style_translate_y(obj, POP_SHIFT, 0);

    anim_start(obj, cb_trans_y, POP_SHIFT, 0, T_POP, delay_ms, lv_anim_path_overshoot);
    anim_start(obj, cb_opa,     OPA_MIN,   255, T_POP, delay_ms, lv_anim_path_ease_out);
}

void anime_press(lv_obj_t *obj)
{
    if(obj == NULL)
    {
        return;
    }

    /* 不回写 opa：直接从一个偏暗值弹回 255，避免"按下瞬间被设成全透明"这类意外 */
    anim_start(obj, cb_opa, OPA_PRESS, 255, T_PRESS_BACK, 0, lv_anim_path_ease_out);
}

void anime_value_flash(lv_obj_t *obj)
{
    if(obj == NULL)
    {
        return;
    }

    anim_start(obj, cb_opa, OPA_PRESS, 255, T_FLASH, 0, lv_anim_path_ease_out);
}

void anime_fade(lv_obj_t *obj, uint8_t from, uint8_t to, uint32_t ms, uint32_t delay_ms)
{
    if(obj == NULL)
    {
        return;
    }

    if(from < OPA_MIN) { from = OPA_MIN; }
    if(to   < OPA_MIN) { to   = OPA_MIN; }

    anim_start(obj, cb_opa, from, to, ms, delay_ms, lv_anim_path_ease_out);
}

void anime_move_y(lv_obj_t *obj, int16_t from, int16_t to, uint32_t ms, uint32_t delay_ms)
{
    if(obj == NULL)
    {
        return;
    }

    anim_start(obj, cb_trans_y, from, to, ms, delay_ms, lv_anim_path_ease_out);
}

/* ==============================================================================================
 *                                    扩展动画
 *
 * 全部用 LVGL 自带 path + playback/repeat，不自定义曲线（守住文件头第 1 条约束）。
 * ============================================================================================== */

/* 不带下限钳制的 opa：cb_opa 会卡在 OPA_MIN，淡不到全透明 */
static void cb_opa_raw(void *o, int32_t v)
{
    if(v < 0)   { v = 0; }
    if(v > 255) { v = 255; }

    lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
}

static void cb_w(void *o, int32_t v)
{
    if(v < 1) { v = 1; }

    lv_obj_set_width((lv_obj_t *)o, (lv_coord_t)v);
}

static void cb_h(void *o, int32_t v)
{
    if(v < 1) { v = 1; }

    lv_obj_set_height((lv_obj_t *)o, (lv_coord_t)v);
}

static void cb_bg_color(void *o, int32_t v)
{
    lv_obj_set_style_bg_color((lv_obj_t *)o, lv_color_hex((uint32_t)v), 0);
}

/* 往复动画的公共部分：0 -> n 再回到 0，重复 times 次 */
static void anim_swing(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t to,
                       uint32_t ms, uint16_t times)
{
    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, 0, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_playback_time(&a, ms);
    lv_anim_set_repeat_count(&a, times);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void anime_slide_in(lv_obj_t *obj, int8_t dir)
{
    if(obj == NULL)
    {
        return;
    }

    dir = (dir >= 0) ? 1 : -1;

    lv_obj_set_style_translate_x(obj, (lv_coord_t)(dir * SHIFT_SWITCH), 0);
    anim_start(obj, cb_trans_x, dir * SHIFT_SWITCH, 0, T_PAGE_SWITCH, 0, lv_anim_path_ease_out);
}

void anime_fade_out(lv_obj_t *obj, uint32_t ms, uint32_t delay_ms)
{
    if(obj == NULL)
    {
        return;
    }

    if(ms == 0u)
    {
        ms = T_FADE_OUT;
    }

    anim_start(obj, cb_opa_raw, 255, 0, ms, delay_ms, lv_anim_path_ease_out);
}

void anime_bounce_in(lv_obj_t *obj, uint32_t delay_ms)
{
    if(obj == NULL)
    {
        return;
    }

    lv_obj_set_style_translate_y(obj, -POP_SHIFT, 0);
    anim_start(obj, cb_trans_y, -POP_SHIFT, 0, T_POP, delay_ms, lv_anim_path_overshoot);
}

void anime_shake(lv_obj_t *obj)
{
    if(obj == NULL)
    {
        return;
    }

    /* 0 -> 5 -> 0，来回 3 次，约 540ms */
    anim_swing(obj, cb_trans_x, SHAKE_SHIFT, T_SHAKE, 3);
}

void anime_pulse(lv_obj_t *obj, uint16_t times)
{
    lv_anim_t a;

    if(obj == NULL)
    {
        return;
    }

    if(times == 0u)
    {
        times = 2u;
    }

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb_opa_raw);
    lv_anim_set_values(&a, 255, OPA_PRESS);
    lv_anim_set_time(&a, T_PULSE);
    lv_anim_set_playback_time(&a, T_PULSE);
    lv_anim_set_repeat_count(&a, times);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void anime_bg_flash(lv_obj_t *obj, uint32_t hex)
{
    if(obj == NULL)
    {
        return;
    }

    anim_start(obj, cb_bg_color, (int32_t)0xFFFFFF, (int32_t)hex, T_BG_FLASH, 0,
               lv_anim_path_ease_out);
}

void anime_grow_w(lv_obj_t *obj, uint32_t ms, uint32_t delay_ms)
{
    lv_coord_t w;

    if(obj == NULL)
    {
        return;
    }

    if(ms == 0u)
    {
        ms = T_GROW;
    }

    w = lv_obj_get_width(obj);
    anim_start(obj, cb_w, 1, (int32_t)w, ms, delay_ms, lv_anim_path_ease_out);
}

void anime_grow_h(lv_obj_t *obj, uint32_t ms, uint32_t delay_ms)
{
    lv_coord_t h;

    if(obj == NULL)
    {
        return;
    }

    if(ms == 0u)
    {
        ms = T_GROW;
    }

    h = lv_obj_get_height(obj);
    anim_start(obj, cb_h, 1, (int32_t)h, ms, delay_ms, lv_anim_path_ease_out);
}

void anime_list_in(lv_obj_t *obj, uint8_t index)
{
    uint32_t delay;

    if(obj == NULL)
    {
        return;
    }

    delay = (uint32_t)index * T_LIST_STEP;

    lv_obj_set_style_opa(obj, 0, 0);
    lv_obj_set_style_translate_y(obj, LIST_SHIFT, 0);

    anim_start(obj, cb_opa_raw, 0, 255, T_LIST, delay, lv_anim_path_ease_out);
    anim_start(obj, cb_trans_y, LIST_SHIFT, 0, T_LIST, delay, lv_anim_path_ease_out);
}

void anime_stop(lv_obj_t *obj)
{
    if(obj == NULL)
    {
        return;
    }

    lv_anim_del(obj, cb_opa);
    lv_anim_del(obj, cb_trans_x);
    lv_anim_del(obj, cb_trans_y);

    /* 兜底：停掉动画后强制回到完全可见 */
    lv_obj_set_style_opa(obj, 255, 0);
}