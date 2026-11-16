/**
 * @file lv_conf.h
 * LVGL v8.3.11 配置文件 —— 针对 CH32V307 (64KB SRAM) + RT-Thread 裁剪
 *
 * 说明：本文件只覆盖需要改动的项，其余全部由 lvgl/src/lv_conf_internal.h
 *       提供默认值。想改任何默认行为，在这里加一行 #define 即可。
 *
 * 内存预算（64KB SRAM）：
 *   现有工程 data+bss ..........  ~6 KB
 *   LVGL 内存池 LV_MEM_SIZE ....  22 KB
 *   显示缓冲区 (480x10x2) ......  9.6 KB
 *   RT-Thread 堆 ...............  12 KB
 *   栈区 .......................   2 KB
 *   ------------------------------------
 *   实际占用以重新链接后的 .map 为准
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   颜色设置
 *====================*/

/* 颜色深度：16 = RGB565，匹配 ST7796 的 16 位并口 */
#define LV_COLOR_DEPTH 16

/* RGB565 字节序交换。
 * FSMC 16 位并口一次写入整个半字，不交换两个字节；
 * 若红蓝颠倒，先检查 ST7796 MADCTL 的 BGR 位和数据线。 */
#define LV_COLOR_16_SWAP 0

/*====================
   内存设置
 *====================*/

/* 0 = 使用 LVGL 自带静态内存池（不依赖 rt_malloc，最稳） */
#define LV_MEM_CUSTOM 0
#if LV_MEM_CUSTOM == 0
    /* LVGL 内存池大小。控件多、界面复杂就加大；不够时屏幕会花或控件不显示 */
    #define LV_MEM_SIZE (22U * 1024U)
#endif

/*====================
   心跳(tick)：直接用 RT-Thread 的系统 tick
 *====================*/
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE      "rtthread.h"
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (rt_tick_get() * (1000 / RT_TICK_PER_SECOND))
#endif

/*====================
   日志：首次移植强烈建议开着，方便定位"白屏/花屏"
 *====================*/
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF   0       /* 使用 lvgl_console_log -> rt_kprintf 输出 */
#endif

/*====================
   性能/内存监视器（调试期开着，能直接看到 FPS 和内存占用）
   稳定后改成 0 即可
 *====================*/
#define LV_USE_PERF_MONITOR 1
#define LV_USE_MEM_MONITOR  1

/*====================
   字体：只开需要的大小，省 Flash/RAM
 *====================*/
#define LV_FONT_MONTSERRAT_14 1     /* 默认字体，必须开 */
#define LV_FONT_MONTSERRAT_20 1     /* 标题用 */
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

/*====================
   控件裁剪：省 Flash
   CH32V307 只有 256KB Flash，LVGL 全量会占到 240KB(94%)。
   下面这些是初学者暂时用不到的控件，关掉能省 40KB 左右。
   以后需要哪个，把对应的 0 改成 1 即可。
 *====================*/
#define LV_USE_ARC        0     /* 圆弧 */
#define LV_USE_ANIMIMG    0     /* 动画图片 */
#define LV_USE_CALENDAR   0     /* 日历 */
#define LV_USE_CANVAS     0     /* 画布 */
#define LV_USE_CHART      0     /* 图表（要显示曲线数据时改 1） */
#define LV_USE_COLORWHEEL 0     /* 色轮 */
#define LV_USE_IMGBTN     0     /* 图片按钮 */
#define LV_USE_KEYBOARD   0     /* 软键盘 */
#define LV_USE_LED        0     /* LED 指示灯 */
#define LV_USE_LIST       0     /* 列表 */
#define LV_USE_MENU       0     /* 菜单 */
#define LV_USE_METER      0     /* 仪表盘 */
#define LV_USE_MSGBOX     0     /* 消息框 */
#define LV_USE_SPAN       0     /* 富文本 */
#define LV_USE_SPINBOX    0     /* 数字微调框 */
#define LV_USE_SPINNER    0     /* 加载圈 */
#define LV_USE_TABVIEW    0     /* 标签页 */
#define LV_USE_TILEVIEW   0     /* 平铺视图 */
#define LV_USE_WIN        0     /* 窗口 */
/* Widgets with no callers in the competition project. */
#define LV_USE_ROLLER     0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   0
#define LV_USE_TABLE      0     /* 表格 */

/*====================
   主题
 *====================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_BASIC   1


/* Keep the shell alive when LVGL detects an error; never continue with corrupt state. */
#define LV_ASSERT_HANDLER_INCLUDE "rtthread.h"
#define LV_ASSERT_HANDLER do { rt_kprintf("[LVGL ASSERT] %s:%d\n", __FILE__, __LINE__); while(1) { rt_thread_mdelay(1000); } } while(0);
#endif /* LV_CONF_H */

