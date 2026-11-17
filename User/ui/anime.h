/******************************************************************************
 * 文件名   : anime.h
 * 说明     : 动画分装
 *
 * 设计原则：
 *   1. 曲线只用两种：缓出五次曲线（"苹果味"）和轻微回弹（按压/弹出）；
 *   2. 时长、位移、透明度全部是 anime.c 顶部的宏，改手感只改数字；
 *   3. 只动"小面积"对象（按钮、卡片、文字），绝不做全屏淡入——
 *      这块屏是 FSMC 软件刷，全屏重绘一次要几十毫秒，必然卡。
 ******************************************************************************/

#ifndef __UI_ANIME_H
#define __UI_ANIME_H

#include "lvgl/lvgl.h"

/**
 * @brief  页面进入：淡入 + 从下方轻微上浮
 */
void anime_page_in(lv_obj_t *obj);

/**
 * @brief  页面切换：按方向横向滑入 + 淡入
 * @param  dir  1 = 新页从右侧进来，-1 = 从左侧
 */
void anime_page_switch(lv_obj_t *obj, int8_t dir);

/**
 * @brief  弹出：缩放 + 淡入，带一点点回弹（只给小对象用，<= 120px）
 * @param  delay_ms  延迟，用来做"依次出现"的错落感
 */
void anime_pop_in(lv_obj_t *obj, uint32_t delay_ms);

/**
 * @brief  按下反馈：压暗后弹回
 */
void anime_press(lv_obj_t *obj);

/**
 * @brief  数值变化时轻微闪一下
 */
void anime_value_flash(lv_obj_t *obj);

/**
 * @brief  通用淡入/淡出（给启动动画这类需要自己排时间线的地方用）
 */
void anime_fade(lv_obj_t *obj, uint8_t from, uint8_t to, uint32_t ms, uint32_t delay_ms);

/**
 * @brief  通用纵向移动（translate，不改布局）
 */
void anime_move_y(lv_obj_t *obj, int16_t from, int16_t to, uint32_t ms, uint32_t delay_ms);

/**
 * @brief  从左右滑入（比 page_in 更有方向感，用于横向布局的页面）
 * @param  dir  1 = 从右边进来，-1 = 从左边进来
 */
void anime_slide_in(lv_obj_t *obj, int8_t dir);

/**
 * @brief  淡出到全透明（anime_fade 最暗只到 OPA_MIN）
 */
void anime_fade_out(lv_obj_t *obj, uint32_t ms, uint32_t delay_ms);

/**
 * @brief  从上往下落并回弹（比 pop_in 更弹，适合卡片）
 */
void anime_bounce_in(lv_obj_t *obj, uint32_t delay_ms);

/**
 * @brief  左右快速摆动：参数越界、操作失败的提示
 * @note   用 playback + repeat 做往返，不改透明度，不会把元素弄丢
 */
void anime_shake(lv_obj_t *obj);

/**
 * @brief  呼吸：透明度来回渐变 N 次
 * @param  times  往复次数，0 按 2 次算
 * @note   想做"一直闪"的指示灯，times 给大一点（比如 20）
 */
void anime_pulse(lv_obj_t *obj, uint16_t times);

/**
 * @brief  背景色由白渐入到目标色（强调某一块刚更新）
 */
void anime_bg_flash(lv_obj_t *obj, uint32_t hex);

/**
 * @brief  宽度从 1 长到当前宽度（进度条、分隔线、下划线）
 */
void anime_grow_w(lv_obj_t *obj, uint32_t ms, uint32_t delay_ms);

/**
 * @brief  高度从 1 长到当前高度（竖向展开）
 */
void anime_grow_h(lv_obj_t *obj, uint32_t ms, uint32_t delay_ms);

/**
 * @brief  列表逐项入场：第 index 项延迟 index*60ms
 * @param  index  从 0 开始的行号，同一屏的元素按顺序给 0,1,2...
 */
void anime_list_in(lv_obj_t *obj, uint8_t index);

/**
 * @brief  停掉某对象上的所有动画
 */
void anime_stop(lv_obj_t *obj);

#endif