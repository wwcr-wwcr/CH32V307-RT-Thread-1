/******************************************************************************
                                                                                                                全局视觉常量
 ******************************************************************************/

#ifndef __UI_THEME_H
#define __UI_THEME_H

#include "lvgl/lvgl.h"

/*********************************************************************
 * 颜色
 *********************************************************************/
#define UI_C_BG          lv_color_hex(0xFFF5F9)     /* 页面底色（顶部奶油粉） */
#define UI_C_BG_BOTTOM   lv_color_hex(0xE7F3FF)     /* 页面底色（底部淡蓝，纵向渐变） */
#define UI_C_CARD        lv_color_hex(0xFFFFFF)     /* 卡片：纯白 */
#define UI_C_BORDER      lv_color_hex(0xFFD9E8)     /* 描边：淡草莓 */
#define UI_C_TITLE       lv_color_hex(0xFF7AA8)     /* 标题栏：草莓粉 */
#define UI_C_TEXT        lv_color_hex(0x3A2E39)     /* 主文字：深可可 */
#define UI_C_TEXT_DIM    lv_color_hex(0x9A8C98)     /* 次要文字：灰紫 */
#define UI_C_VALUE       lv_color_hex(0xFF4F8B)     /* 数值：亮草莓 */
#define UI_C_ACCENT      lv_color_hex(0x3FC8B4)     /* 强调：薄荷绿 */
#define UI_C_HINT        lv_color_hex(0xFFE6F1)     /* 底部提示条 */

/*********************************************************************
 * 形状 / 字体
 *********************************************************************/
#define UI_RADIUS        22                         /* 卡片圆角，越大越"软" */
#define UI_FONT_SMALL    (&lv_font_montserrat_14)
#define UI_FONT_NORMAL   (&lv_font_montserrat_14)
#define UI_FONT_BIG      (&lv_font_montserrat_14)
#define UI_FONT_HUGE     (&lv_font_montserrat_14)
/*********************************************************************
 * 尺寸
 *********************************************************************/
#define UI_W             320
#define UI_H             480
#define UI_TITLE_H       56
#define UI_HINT_H        40
#define UI_PAD           16

#endif