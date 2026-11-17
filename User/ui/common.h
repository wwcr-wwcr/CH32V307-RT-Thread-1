/******************************************************************************
 * 文件名   : common.h
 * 说明     : 通用控件封装
 *
 * 只给 ui/ 目录下的 *_ui.c 用。逻辑层（menu.c / task_x.c）不要直接调这里的函数。
 * 例外：ui_plot_push() / ui_plot_clear() 只写数字不碰 lv_，任务线程可以调。
 ******************************************************************************/

#ifndef __UI_COMMON_H
#define __UI_COMMON_H

#include "lvgl/lvgl.h"
#include "theme.h"
/**
 * @brief  创建一个独立页面（LVGL screen），带统一背景色
 * @note   每个界面用自己的一页，切换时互不干扰
 */
lv_obj_t *ui_page_create(void);

/**
 * @brief  切换到某个页面
 */
void ui_page_load(lv_obj_t *page);

/**
 * @brief  销毁一个页面并归还 LVGL 堆
 * @param  page 页面指针的地址，删完会置 NULL
 * @note   UI 线程专用。调用前先 load 别的页面，不要删当前显示的 screen。
 */
void ui_page_destroy(lv_obj_t **page);

/**
 * @brief  这个页面当前是不是正在显示
 * @return 1 是，0 不是
 */
int ui_page_is_active(lv_obj_t *page);

/**
 * @brief  取当前活动屏幕
 */
lv_obj_t *ui_root(void);

/**
 * @brief  顶部标题栏：左边标题，右边页码
 * @param  title  左侧标题（一般固定不变）
 * @param  right  右侧文字，例如 "1/3"
 * @return 右侧的 label，方便后续更新页码
 */
lv_obj_t *ui_title_bar(lv_obj_t *root, const char *title, const char *right);

/**
 * @brief  底部提示条
 * @return 中间的 label
 */
lv_obj_t *ui_hint_bar(lv_obj_t *root, const char *text);

/**
 * @brief  无边框、无滚动的圆角容器
 */
lv_obj_t *ui_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                   lv_coord_t w, lv_coord_t h, lv_color_t bg);

/**
 * @brief  一段文字，默认透明底、无边框
 */
lv_obj_t *ui_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color);

/**
 * @brief  设置文字内容（NULL 显示空串）
 */
void ui_text_set(lv_obj_t *label, const char *text);

/**
 * @brief  一行"标签 + 数值"，返回右侧的数值 label
 * @param  parent     父容器
 * @param  y          这一行的 y 坐标
 * @param  label_text 左侧标签文字
 */
lv_obj_t *ui_row(lv_obj_t *parent, lv_coord_t y, const char *label_text);

/**
 * @brief  相对父对象对齐
 */
void ui_text_align(lv_obj_t *label, lv_align_t align, lv_coord_t x, lv_coord_t y);

/*********************************************************************
 * 数值显示（省得每次都自己准备 char 缓冲 + snprintf）
 *
 * 内部用 16~24 字节栈缓冲，可在任何任务里调用，不要传 NULL label。
 * 固定不变的文字仍然直接用 ui_text_set()。
 *********************************************************************/

/**
 * @brief  显示整数，例如 -25 / 1234
 */
void ui_text_int(lv_obj_t *label, int32_t value);

/**
 * @brief  显示"整数 + 单位"，例如 1234 -> "1234 lux"
 * @param  unit  单位，传 NULL 表示不带
 */
void ui_text_num(lv_obj_t *label, int32_t value, const char *unit);

/**
 * @brief  显示一位小数（传放大 10 倍后的整数），例如 253 -> "25.3"
 * @param  value_x10  放大 10 倍的整数（ntc_temperature_x10_get() 正好是这个格式）
 * @param  unit       单位，传 NULL 表示不带
 */
void ui_text_x10(lv_obj_t *label, int32_t value_x10, const char *unit);

/**
 * @brief  显示百分比，例如 60 -> "60%"
 */
void ui_text_pct(lv_obj_t *label, uint8_t percent);


/*********************************************************************
 * 交互控件（内部已经挂好事件，页面代码只需一行）
 *
 * 事件编号 ev_id 会通过 ui_task_event(id, value) 送到任务线程，
 * 任务在 on_set(id, value) 里按 id 区分是哪个控件。
 *********************************************************************/

/**
 * @brief  带统一配色的滑条，拖动时自动发 VALUE_CHANGED 事件
 * @param  ev_id  事件编号（0~255，任务侧自定义）
 * @return 滑条对象，便于后续 lv_slider_set_value
 */
lv_obj_t *ui_slider(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                    int32_t min, int32_t max, int32_t value, uint8_t ev_id);

/**
 * @brief  圆角按钮：白底 + 细边框 + 居中文字，点一下发事件，按下带回弹
 * @param  ev_id  点击事件编号
 * @return 按钮对象
 */
lv_obj_t *ui_button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                    lv_coord_t w, lv_coord_t h, const char *text, uint8_t ev_id);

/**
 * @brief  胶囊形进度条（0~100）
 * @param  fg  已完成部分的颜色（建议 UI_C_ACCENT）
 */
lv_obj_t *ui_bar(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                 lv_coord_t w, lv_coord_t h, uint8_t value, lv_color_t fg);

/**
 * @brief  1px 分隔线
 */
lv_obj_t *ui_divider(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                     lv_coord_t w, lv_color_t color);

/**
 * @brief  圆形色块（做状态灯、图标底）
 * @param  d  直径
 */
lv_obj_t *ui_dot(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                 lv_coord_t d, lv_color_t color);

/**
 * @brief  数据卡片：左上小标题 + 左下大数值
 * @return 数值 label，render 里直接 ui_text_set 更新
 */
lv_obj_t *ui_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                  lv_coord_t w, lv_coord_t h, const char *title, const char *value);



/*********************************************************************
 * 曲线图（坐标轴 + 折线，虚线 / 实线 / 颜色可选）
 *
 * 这是 common 里唯一允许【逻辑层】调用的两个函数：
 *     ui_plot_push() / ui_plot_clear()  —— 只写数字，一个 lv_ 都不碰
 * 其余（create / axis / line / refresh / delete）仍然只能在 render 链路里调。
 *
 * 用法：
 *     ui_plot_t *p = ui_plot_create(page, 48, 78, 256, 120);
 *     ui_plot_axis(p, 4, 5, 0xE8DEE6);                 // 4 行 5 列网格
 *     ui_plot_line(p, 0, &(ui_plot_line_t){
 *             .color = 0x00A0C8, .width = 2,
 *             .style = UI_LINE_DASH, .dash = 4, .gap = 3,
 *             .vmin  = 0, .vmax = 100 });
 *
 *     ui_plot_push(p, 0, value);   // 任务线程
 *     ui_plot_refresh(p);          // UI 线程（render 里）
 *
 * 注意：ui_plot_line_t 没写的字段会被清零，所以"显示"用 hidden 反着标，
 *       忘了写某个字段线也照样画得出来。
 *********************************************************************/
#define UI_PLOT_CH_MAX      2       /* 一块画布最多几条曲线 */
#define UI_PLOT_POINT_MAX   64      /* 每条曲线存多少个点（滚动窗口） */

typedef enum
{
    UI_LINE_SOLID = 0,              /* 实线 */
    UI_LINE_DASH,                   /* 虚线 */
} ui_line_style_t;

typedef struct
{
    uint32_t color;                 /* 0xRRGGBB，注意是 32 位，0x00A0C8 塞不进 16 位 */
    int32_t  vmin;                  /* 量程下限，映射到画布底边 */
    int32_t  vmax;                  /* 量程上限，映射到画布顶边 */
    uint8_t  width;                 /* 线宽 1~3，0 当 1 */
    uint8_t  style;                 /* ui_line_style_t */
    uint8_t  dash;                  /* 虚线：实线段长 px，0 用默认 4 */
    uint8_t  gap;                   /* 虚线：间隔长 px，0 用默认 3 */
    uint8_t  dot;                   /* 1 = 每个采样点再画 3x3 方块 */
    uint8_t  hidden;                /* 1 = 不画这条 */
} ui_plot_line_t;

typedef struct ui_plot ui_plot_t;

ui_plot_t *ui_plot_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h);
void    ui_plot_delete(ui_plot_t *p);
void    ui_plot_axis(ui_plot_t *p, uint8_t y_div, uint8_t x_div, uint32_t grid_color);
void    ui_plot_line(ui_plot_t *p, uint8_t ch, const ui_plot_line_t *cfg);
void    ui_plot_push(ui_plot_t *p, uint8_t ch, int32_t v);
void    ui_plot_clear(ui_plot_t *p, uint8_t ch);
void    ui_plot_refresh(ui_plot_t *p);
uint8_t ui_plot_cnt(const ui_plot_t *p, uint8_t ch);

#endif
