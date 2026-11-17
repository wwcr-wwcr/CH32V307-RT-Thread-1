/******************************************************************************
 * 文件名   : common.c
 * 说明     : 通用控件封装（含曲线图 ui_plot_*）
 ******************************************************************************/

#include "common.h"
#include "ui_event.h"
#include "anime.h"
#include "rtthread.h"
#include "rthw.h"
#include <string.h>
#include <stdio.h>

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
/**
 * @brief  创建独立页面
 */
lv_obj_t *ui_page_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(scr, UI_C_BG, 0);
    lv_obj_set_style_bg_grad_color(scr, UI_C_BG_BOTTOM, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    return scr;
}

/**
 * @brief  切换页面
 */
void ui_page_load(lv_obj_t *page)
{
    if(page != NULL)
    {
        lv_scr_load(page);
    }
}

/* 递归清掉整页所有对象上的动画。
 * LVGL 的动画只记着对象裸指针，不先删就直接 lv_obj_del，动画定时器会踩到
 * 已经释放的内存。 */
static void page_anim_del(lv_obj_t *obj)
{
    uint32_t i;
    uint32_t cnt = lv_obj_get_child_cnt(obj);

    for(i = 0; i < cnt; i++)
    {
        page_anim_del(lv_obj_get_child(obj, i));
    }

    lv_anim_del(obj, NULL);
}

/**
 * @brief  销毁一个页面并归还 LVGL 堆
 * @param  page 页面指针的地址，删完会置 NULL
 * @note   只能在 UI 线程调用；调用前必须先把别的页面 load 上去，
 *         不能删当前正在显示的 screen。
 */
void ui_page_destroy(lv_obj_t **page)
{
    if((page == NULL) || (*page == NULL))
    {
        return;
    }

    page_anim_del(*page);
    lv_obj_del(*page);
    *page = NULL;
}

/**
 * @brief  页面是否正在显示
 */
int ui_page_is_active(lv_obj_t *page)
{
    if(page == NULL)
    {
        return 0;
    }

    return (lv_scr_act() == page) ? 1 : 0;
}

/**
 * @brief  当前活动屏幕
 */
lv_obj_t *ui_root(void)
{
    static lv_obj_t *root = NULL;

    if(root != NULL)
    {
        return root;
    }

    root = lv_scr_act();
    lv_obj_set_style_bg_color(root, UI_C_BG, 0);
    lv_obj_set_style_bg_grad_color(root, UI_C_BG_BOTTOM, 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);

    return root;
}

/**
 * @brief  标题栏
 */
lv_obj_t *ui_title_bar(lv_obj_t *root, const char *title, const char *right)
{
    lv_obj_t *bar;
    lv_obj_t *lbl;
    lv_obj_t *rbl;

    /* 悬浮胶囊，不再是一条满宽的色块 */
    bar = ui_panel(root, 12, 10, (lv_coord_t)(UI_W - 24), (lv_coord_t)(UI_TITLE_H - 10), UI_C_CARD);
    lv_obj_set_style_radius(bar, UI_RADIUS, 0);
    lv_obj_set_style_bg_opa(bar, 242, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, UI_C_BORDER, 0);

    lbl = ui_text(bar, UI_FONT_BIG, UI_C_TEXT);
    ui_text_set(lbl, title);
    ui_text_align(lbl, LV_ALIGN_LEFT_MID, UI_PAD, 0);

    rbl = ui_text(bar, UI_FONT_SMALL, UI_C_TEXT_DIM);
    ui_text_set(rbl, right);
    ui_text_align(rbl, LV_ALIGN_RIGHT_MID, -UI_PAD, 0);

    return rbl;
}

/**
 * @brief  底部提示条
 */
lv_obj_t *ui_hint_bar(lv_obj_t *root, const char *text)
{
    lv_obj_t *bar;
    lv_obj_t *lbl;

    /* 底部提示：同样做成悬浮胶囊，留出边距 */
    bar = ui_panel(root, 24, (lv_coord_t)(UI_H - 46), (lv_coord_t)(UI_W - 48), 36, UI_C_CARD);
    lv_obj_set_style_radius(bar, 18, 0);
    lv_obj_set_style_bg_opa(bar, 215, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, UI_C_BORDER, 0);

    lbl = ui_text(bar, UI_FONT_SMALL, UI_C_TEXT_DIM);
    ui_text_set(lbl, text);
    ui_text_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return lbl;
}

/**
 * @brief  容器
 */
lv_obj_t *ui_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                   lv_coord_t w, lv_coord_t h, lv_color_t bg)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, UI_RADIUS, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    return obj;
}

/**
 * @brief  文字
 */
lv_obj_t *ui_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);

    return label;
}

/**
 * @brief  设置文字
 */
void ui_text_set(lv_obj_t *label, const char *text)
{
    const char *old;

    if(label == NULL)
    {
        return;
    }

    if(text == NULL)
    {
        text = "";
    }

    /* 内容没变就不设，避免每帧无谓重绘 */
    old = lv_label_get_text(label);
    if((old != NULL) && (strcmp(old, text) == 0))
    {
        return;
    }

    lv_label_set_text(label, text);
}

/**
 * @brief  对齐
 */
void ui_text_align(lv_obj_t *label, lv_align_t align, lv_coord_t x, lv_coord_t y)
{
    if(label == NULL)
    {
        return;
    }

    lv_obj_align(label, align, x, y);
}

/**
 * @brief  一行"标签 + 数值"
 */
lv_obj_t *ui_row(lv_obj_t *parent, lv_coord_t y, const char *label_text)
{
    lv_obj_t *lbl;
    lv_obj_t *val;

    lbl = ui_text(parent, UI_FONT_NORMAL, UI_C_TEXT_DIM);
    ui_text_set(lbl, label_text);
    ui_text_align(lbl, LV_ALIGN_TOP_LEFT, 20, y);

    val = ui_text(parent, UI_FONT_BIG, UI_C_VALUE);
    ui_text_set(val, "--");
    ui_text_align(val, LV_ALIGN_TOP_RIGHT, -20, (lv_coord_t)(y - 6));

    return val;
}

/*********************************************************************
 *                              交互控件
 *********************************************************************/
/* 滑条：编号从 user_data 进来，统一投给任务线程 */
static void on_slider(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    uint8_t   id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    ui_task_event(id, (int32_t)lv_slider_get_value(sl));
}

/* 按钮：点一下 */
static void on_button(lv_event_t *e)
{
    uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    ui_task_event(id, 0);
}

/* 按钮：按下回弹 */
static void on_button_press(lv_event_t *e)
{
    anime_press(lv_event_get_target(e));
}

lv_obj_t *ui_slider(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                    int32_t min, int32_t max, int32_t value, uint8_t ev_id)
{
    lv_obj_t *sl = lv_slider_create(parent);

    if(sl == NULL)
    {
        return NULL;
    }

    lv_obj_set_pos(sl, x, y);
    lv_obj_set_size(sl, w, 10);
    lv_slider_set_range(sl, min, max);
    lv_slider_set_value(sl, value, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(sl, UI_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, UI_C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, UI_C_TEXT, LV_PART_KNOB);

    lv_obj_add_event_cb(sl, on_slider, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)ev_id);

    return sl;
}

lv_obj_t *ui_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                    lv_coord_t w, lv_coord_t h, const char *text, uint8_t ev_id)
{
    lv_obj_t *btn = ui_panel(parent, x, y, w, h, UI_C_CARD);
    lv_obj_t *lbl;

    if(btn == NULL)
    {
        return NULL;
    }

    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, UI_C_BORDER, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, on_button, LV_EVENT_CLICKED, (void *)(uintptr_t)ev_id);
    lv_obj_add_event_cb(btn, on_button_press, LV_EVENT_PRESSED, NULL);

    lbl = ui_text(btn, UI_FONT_BIG, UI_C_VALUE);
    ui_text_set(lbl, text);
    ui_text_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return btn;
}

lv_obj_t *ui_bar(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                 lv_coord_t w, lv_coord_t h, uint8_t value, lv_color_t fg)
{
    lv_obj_t *bar = lv_bar_create(parent);

    if(bar == NULL)
    {
        return NULL;
    }

    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);

    lv_obj_set_style_radius(bar, (lv_coord_t)(h / 2), LV_PART_MAIN);
    lv_obj_set_style_radius(bar, (lv_coord_t)(h / 2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, UI_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, fg, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);

    return bar;
}

lv_obj_t *ui_divider(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                     lv_coord_t w, lv_color_t color)
{
    lv_obj_t *div = ui_panel(parent, x, y, w, 1, color);

    if(div != NULL)
    {
        lv_obj_set_style_radius(div, 0, 0);
    }

    return div;
}

lv_obj_t *ui_dot(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                 lv_coord_t d, lv_color_t color)
{
    lv_obj_t *dot = ui_panel(parent, x, y, d, d, color);

    if(dot != NULL)
    {
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    }

    return dot;
}

lv_obj_t *ui_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                  lv_coord_t w, lv_coord_t h, const char *title, const char *value)
{
    lv_obj_t *card = ui_panel(parent, x, y, w, h, UI_C_CARD);
    lv_obj_t *cap;
    lv_obj_t *val;

    if(card == NULL)
    {
        return NULL;
    }

    cap = ui_text(card, UI_FONT_SMALL, UI_C_TEXT_DIM);
    ui_text_set(cap, (title != NULL) ? title : "");
    ui_text_align(cap, LV_ALIGN_TOP_LEFT, 16, 12);

    val = ui_text(card, UI_FONT_HUGE, UI_C_VALUE);
    ui_text_set(val, (value != NULL) ? value : "--");
    ui_text_align(val, LV_ALIGN_BOTTOM_LEFT, 16, -12);

    return val;
}

/*********************************************************************
 *                              数值显示
 *********************************************************************/
void ui_text_int(lv_obj_t *label, int32_t value)
{
    char buf[16];

    snprintf(buf, sizeof(buf), "%d", (int)value);
    ui_text_set(label, buf);
}

void ui_text_num(lv_obj_t *label, int32_t value, const char *unit)
{
    char buf[24];

    if(unit != NULL)
    {
        snprintf(buf, sizeof(buf), "%d %s", (int)value, unit);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%d", (int)value);
    }

    ui_text_set(label, buf);
}

void ui_text_x10(lv_obj_t *label, int32_t value_x10, const char *unit)
{
    char        buf[24];
    uint32_t    mag;
    const char *sign = "";

    /* 先取绝对值：直接 -value 在 INT32_MIN 时会溢出 */
    if(value_x10 < 0)
    {
        mag  = (uint32_t)(-(value_x10 + 1)) + 1u;
        sign = "-";
    }
    else
    {
        mag = (uint32_t)value_x10;
    }

    if(unit != NULL)
    {
        snprintf(buf, sizeof(buf), "%s%u.%u %s", sign,
                 (unsigned)(mag / 10u), (unsigned)(mag % 10u), unit);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%s%u.%u", sign,
                 (unsigned)(mag / 10u), (unsigned)(mag % 10u));
    }

    ui_text_set(label, buf);
}

void ui_text_pct(lv_obj_t *label, uint8_t percent)
{
    char buf[8];

    snprintf(buf, sizeof(buf), "%u%%", (unsigned)percent);
    ui_text_set(label, buf);
}

/**************************************************************************************************
 *                                        曲线图（坐标轴 + 折线）
 *
 * 自绘实现：LV_EVENT_DRAW_MAIN + lv_draw_line，不建 canvas
 * （一块 256x120 的 canvas 要 61KB 显存，板子总共才 96KB）。
 * 支持实线/虚线、任意颜色、多通道、每通道独立量程。
 *
 * 线程模型 —— 注意：这是 common 里【唯一允许逻辑层调用】的两个函数，
 * 因为它们只写数字、一个 lv_ 都不碰：
 *     task 线程 -> ui_plot_push() / ui_plot_clear()
 *     UI  线程 -> ui_plot_create / axis / line / refresh / delete
 *     绘制     -> plot_draw_cb()（DRAW_MAIN 回调，跑在 UI 线程）
 *
 * 三个坑（都在下面注释里标了）：
 *   1) lv_draw_line 要【屏幕绝对坐标】，先 lv_obj_get_coords 加偏移；
 *   2) 虚线相位必须跨点连续，否则 4px 的点间距会让虚线看起来像实线；
 *   3) 复合字面量没写的字段会被清零，所以"显示"用 hidden 反着标。
 **************************************************************************************************/

#define PLOT_DEF_DASH       4       /* 虚线默认实线段长 */
#define PLOT_DEF_GAP        3       /* 虚线默认间隔长 */
#define PLOT_GRID_INSET     2       /* 曲线绘制区相对外框内缩，免得压住边框 */

/**************************************************************************************************
 *                                        数据结构
 **************************************************************************************************/
struct ui_plot
{
    lv_obj_t      *obj;

    uint8_t        y_div;
    uint8_t        x_div;
    lv_color_t     grid;

    ui_plot_line_t cfg[UI_PLOT_CH_MAX];

    /* 任务线程写的原始数据 */
    int16_t        raw[UI_PLOT_CH_MAX][UI_PLOT_POINT_MAX];
    uint16_t       raw_cnt[UI_PLOT_CH_MAX];

    /* UI 线程快照（绘制只读这份，保证不会被半路改掉） */
    int16_t        val[UI_PLOT_CH_MAX][UI_PLOT_POINT_MAX];
    uint16_t       cnt[UI_PLOT_CH_MAX];

    /* 有新数据才重绘。不加这个的话每帧都让 LVGL 重画 256x120，
     * 两块图加起来 12 万个像素，屏根本刷不过来，界面会明显拖帧 */
    volatile uint8_t dirty;
};

/**************************************************************************************************
 *                                        坐标换算
 **************************************************************************************************/

/* 第 i 个点画在哪个 x：0 在最左，POINT_MAX-1 在最右。
 * 用固定步距（不是按当前点数均分），这样刚开机时曲线从左往右长，像示波器。 */
static lv_coord_t plot_x(lv_coord_t xl, lv_coord_t xr, uint16_t i)
{
    return (lv_coord_t)(xl + (int32_t)(xr - xl) * (int32_t)i / (UI_PLOT_POINT_MAX - 1));
}

/* 数值 -> y。vmin 落到底边，vmax 落到顶边，超范围夹住。 */
static lv_coord_t plot_y(const ui_plot_line_t *c, int32_t v,
                         lv_coord_t yt, lv_coord_t yb)
{
    int32_t lo = c->vmin;
    int32_t hi = c->vmax;

    if(hi <= lo) { hi = lo + 1; }

    if(v < lo) { v = lo; }
    if(v > hi) { v = hi; }

    return (lv_coord_t)(yb - (int32_t)(v - lo) * (yb - yt) / (hi - lo));
}

/**************************************************************************************************
 *                                        画线
 **************************************************************************************************/

/* 画一段：实线一次画完，虚线沿线分段挑着画 */
static void plot_seg(lv_draw_ctx_t *ctx, const lv_draw_line_dsc_t *d,
                     lv_coord_t xa, lv_coord_t ya, lv_coord_t xb, lv_coord_t yb,
                     const ui_plot_line_t *c, uint8_t *phase)
{
    lv_point_t p1;
    lv_point_t p2;
    int32_t    dx, dy, adx, ady, steps, i;
    uint8_t    dash, gap, period, cnt;

    p1.x = xa; p1.y = ya;
    p2.x = xb; p2.y = yb;

    if(c->style != UI_LINE_DASH)
    {
        lv_draw_line(ctx, d, &p1, &p2);
        return;
    }

    dash = (c->dash == 0u) ? PLOT_DEF_DASH : c->dash;
    gap  = (c->gap  == 0u) ? PLOT_DEF_GAP  : c->gap;
    period = (uint8_t)(dash + gap);

    dx = (int32_t)(xb - xa);
    dy = (int32_t)(yb - ya);
    adx = (dx < 0) ? -dx : dx;
    ady = (dy < 0) ? -dy : dy;

    /* 步数取 max(|dx|,|dy|)：水平/垂直/斜线都不会漏像素，也不用开方算长度 */
    steps = (adx > ady) ? adx : ady;
    if(steps <= 0) { return; }

    cnt = *phase;

    for(i = 0; i < steps; i++)
    {
        if(cnt <= dash)                     /* 落在实线段里才画这 1 像素 */
        {
            p1.x = (lv_coord_t)(xa + dx * i / steps);
            p1.y = (lv_coord_t)(ya + dy * i / steps);
            p2.x = (lv_coord_t)(xa + dx * (i + 1) / steps);
            p2.y = (lv_coord_t)(ya + dy * (i + 1) / steps);
            lv_draw_line(ctx, d, &p1, &p2);
        }

        cnt++;
        if(cnt > period) { cnt = 1; }
    }

    *phase = cnt;
}

/* 一条曲线的全部线段 */
static void plot_series(lv_draw_ctx_t *ctx, const ui_plot_t *p, uint8_t ch,
                        lv_coord_t xl, lv_coord_t yt,
                        lv_coord_t xr, lv_coord_t yb)
{
    const ui_plot_line_t *c = &p->cfg[ch];
    const int16_t        *v = p->val[ch];
    uint16_t              n = p->cnt[ch];
    lv_draw_line_dsc_t    d;
    uint8_t               phase = 1;
    uint16_t              i;

    if((c->hidden != 0u) || (n == 0u)) { return; }

    lv_draw_line_dsc_init(&d);
    d.color       = lv_color_hex(c->color);
    d.width       = (c->width == 0u) ? 1 : c->width;
    d.opa         = LV_OPA_COVER;
    d.round_start = 1;                      /* 圆头，折点才不缺口 */
    d.round_end   = 1;

    for(i = 1; i < n; i++)
    {
        plot_seg(ctx, &d,
                 plot_x(xl, xr, (uint16_t)(i - 1u)), plot_y(c, v[i - 1u], yt, yb),
                 plot_x(xl, xr, i),                  plot_y(c, v[i], yt, yb),
                 c, &phase);
    }

    /* 采样点标记：数据点少的时候很直观，多了就糊成一团，按需要开 */
    if(c->dot != 0u)
    {
        lv_draw_rect_dsc_t rd;
        lv_area_t          a;

        lv_draw_rect_dsc_init(&rd);
        rd.bg_color = d.color;
        rd.bg_opa   = LV_OPA_COVER;
        rd.radius   = 0;

        for(i = 0; i < n; i++)
        {
            lv_coord_t px = plot_x(xl, xr, i);
            lv_coord_t py = plot_y(c, v[i], yt, yb);

            a.x1 = (lv_coord_t)(px - 1);
            a.y1 = (lv_coord_t)(py - 1);
            a.x2 = (lv_coord_t)(px + 1);
            a.y2 = (lv_coord_t)(py + 1);
            lv_draw_rect(ctx, &rd, &a);
        }
    }
}

/**************************************************************************************************
 *                                        DRAW_MAIN 回调
 **************************************************************************************************/
static void plot_draw_cb(lv_event_t *e)
{
    ui_plot_t           *p   = (ui_plot_t *)lv_event_get_user_data(e);
    lv_obj_t            *obj = lv_event_get_target(e);
    lv_draw_ctx_t       *ctx = lv_event_get_draw_ctx(e);
    lv_area_t            co;
    lv_draw_line_dsc_t   d;
    lv_point_t           a;
    lv_point_t           b;
    lv_coord_t           xl, xr, yt, yb;
    uint8_t              i;

    if(p == NULL) { return; }

    lv_obj_get_coords(obj, &co);            /* 绝对坐标！见文件头第 1 条 */

    xl = co.x1;  xr = co.x2;
    yt = co.y1;  yb = co.y2;

    lv_draw_line_dsc_init(&d);
    d.color = p->grid;
    d.width = 1;
    d.opa   = LV_OPA_COVER;

    /* 1. 外框 */
    a.x = xl; a.y = yt; b.x = xr; b.y = yt; lv_draw_line(ctx, &d, &a, &b);
    a.x = xr; a.y = yt; b.x = xr; b.y = yb; lv_draw_line(ctx, &d, &a, &b);
    a.x = xr; a.y = yb; b.x = xl; b.y = yb; lv_draw_line(ctx, &d, &a, &b);
    a.x = xl; a.y = yb; b.x = xl; b.y = yt; lv_draw_line(ctx, &d, &a, &b);

    /* 2. 内部网格 */
    for(i = 1; i < p->y_div; i++)
    {
        lv_coord_t y = (lv_coord_t)(yt + (int32_t)(yb - yt) * i / p->y_div);
        a.x = xl; a.y = y; b.x = xr; b.y = y;
        lv_draw_line(ctx, &d, &a, &b);
    }

    for(i = 1; i < p->x_div; i++)
    {
        lv_coord_t x = (lv_coord_t)(xl + (int32_t)(xr - xl) * i / p->x_div);
        a.x = x; a.y = yt; b.x = x; b.y = yb;
        lv_draw_line(ctx, &d, &a, &b);
    }

    /* 3. 曲线（内缩一点，别把边框盖掉） */
    xl += PLOT_GRID_INSET;
    xr -= PLOT_GRID_INSET;
    yt += PLOT_GRID_INSET;
    yb -= PLOT_GRID_INSET;

    for(i = 0; i < UI_PLOT_CH_MAX; i++)
    {
        plot_series(ctx, p, i, xl, yt, xr, yb);
    }
}

/* 对象被删时（不管是 ui_plot_delete 还是父页面连带删）把结构体还掉 */
static void plot_del_cb(lv_event_t *e)
{
    ui_plot_t *p = (ui_plot_t *)lv_event_get_user_data(e);

    if(p != NULL)
    {
        lv_mem_free(p);
    }
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
ui_plot_t *ui_plot_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h)
{
    ui_plot_t *p;
    uint8_t    ch;

    if(parent == NULL) { return NULL; }

    p = (ui_plot_t *)lv_mem_alloc(sizeof(ui_plot_t));
    if(p == NULL) { return NULL; }

    memset(p, 0, sizeof(ui_plot_t));

    /* 默认样式：实线 1px，量程 0~100，先当它存在，省得外面配漏了画不出来 */
    for(ch = 0; ch < UI_PLOT_CH_MAX; ch++)
    {
        p->cfg[ch].color = 0x3FC8B4u;
        p->cfg[ch].width = 1;
        p->cfg[ch].vmin  = 0;
        p->cfg[ch].vmax  = 100;
    }

    p->y_div = 4;
    p->x_div = 5;
    p->grid  = lv_color_hex(0xD8CFD6);
    p->dirty = 1;

    p->obj = lv_obj_create(parent);
    lv_obj_set_pos(p->obj, x, y);
    lv_obj_set_size(p->obj, w, h);
    lv_obj_set_style_bg_color(p->obj, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(p->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p->obj, 0, 0);
    lv_obj_set_style_radius(p->obj, 10, 0);
    lv_obj_set_style_pad_all(p->obj, 0, 0);
    lv_obj_clear_flag(p->obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(p->obj, plot_draw_cb, LV_EVENT_DRAW_MAIN, p);
    lv_obj_add_event_cb(p->obj, plot_del_cb,  LV_EVENT_DELETE,    p);

    return p;
}

void ui_plot_delete(ui_plot_t *p)
{
    if((p == NULL) || (p->obj == NULL)) { return; }

    /* 结构体在 DELETE 回调里释放；调完这句 p 就野了 */
    lv_obj_del(p->obj);
}

void ui_plot_axis(ui_plot_t *p, uint8_t y_div, uint8_t x_div, uint32_t grid_color)
{
    if(p == NULL) { return; }

    p->y_div = y_div;
    p->x_div = x_div;
    p->grid  = lv_color_hex(grid_color);
    p->dirty = 1;

    if(p->obj != NULL) { lv_obj_invalidate(p->obj); }
}

void ui_plot_line(ui_plot_t *p, uint8_t ch, const ui_plot_line_t *cfg)
{
    if((p == NULL) || (ch >= UI_PLOT_CH_MAX) || (cfg == NULL)) { return; }

    p->cfg[ch] = *cfg;
    p->dirty   = 1;

    if(p->obj != NULL) { lv_obj_invalidate(p->obj); }
}

void ui_plot_push(ui_plot_t *p, uint8_t ch, int32_t v)
{
    int16_t  *buf;
    uint16_t  i;
    rt_base_t level;

    if((p == NULL) || (ch >= UI_PLOT_CH_MAX)) { return; }

    if(v > 32000)  { v = 32000; }
    if(v < -32000) { v = -32000; }

    /* 关中断：UI 线程的 render 优先级更高，不清场的话可能正好读到"左移一半"的数组 */
    level = rt_hw_interrupt_disable();

    buf = p->raw[ch];

    for(i = 1; i < UI_PLOT_POINT_MAX; i++)
    {
        buf[i - 1u] = buf[i];
    }
    buf[UI_PLOT_POINT_MAX - 1u] = (int16_t)v;

    if(p->raw_cnt[ch] < UI_PLOT_POINT_MAX)
    {
        p->raw_cnt[ch]++;
    }

    p->dirty = 1;

    rt_hw_interrupt_enable(level);
}

void ui_plot_clear(ui_plot_t *p, uint8_t ch)
{
    rt_base_t level;

    if((p == NULL) || (ch >= UI_PLOT_CH_MAX)) { return; }

    level = rt_hw_interrupt_disable();
    memset(p->raw[ch], 0, sizeof(p->raw[ch]));
    p->raw_cnt[ch] = 0;
    p->dirty       = 1;
    rt_hw_interrupt_enable(level);

    p->cnt[ch] = 0;
}

void ui_plot_refresh(ui_plot_t *p)
{
    rt_base_t level;
    uint8_t   ch;

    if((p == NULL) || (p->obj == NULL)) { return; }
    if(p->dirty == 0u)                  { return; }   /* 没新数据，连重绘都省了 */

    level = rt_hw_interrupt_disable();

    for(ch = 0; ch < UI_PLOT_CH_MAX; ch++)
    {
        memcpy(p->val[ch], p->raw[ch], sizeof(p->val[ch]));
        p->cnt[ch] = p->raw_cnt[ch];
    }

    p->dirty = 0u;

    rt_hw_interrupt_enable(level);

    lv_obj_invalidate(p->obj);
}

uint8_t ui_plot_cnt(const ui_plot_t *p, uint8_t ch)
{
    if((p == NULL) || (ch >= UI_PLOT_CH_MAX)) { return 0; }

    return (uint8_t)p->cnt[ch];
}
