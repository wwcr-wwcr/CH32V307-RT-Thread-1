/******************************************************************************
 * 文件名   : menu_ui.c
 * 说明     : 选择页 —— 网格版：一屏铺开全部任务，2 列 × N 行卡片，点哪张进哪个。
 *
 * 这个文件拥有"列表长什么样、有几个、怎么选"的全部决定权。
 * menu.c 只知道三件事：画出来、处理本页命令、回收。它不知道有几个任务。
 *
 * 【换成翻页版要改哪几行】只动本文件，menu.c 一行都不用改：
 *     1) 文件头加 static uint8_t s_cur;            ← 当前第几个
 *     2) menu_ui_list_draw() 里 for 循环改成只画 s_cur 那一张，
 *        标题栏右侧改成 "s_cur+1 / n"
 *     3) menu_ui_list_cmd() 里补上 MENU_CMD_PREV / MENU_CMD_NEXT 改 s_cur，
 *        以及 MENU_CMD_KEY 的 K1/K2/K3（KEY_ID_1/2/3）→ 上 / 下 / 进入
 *     4) menu_ui_list_free() 不动（还是整页销毁）
 *   之所以不用改 menu.c：菜单核心只发 MENU_CMD_ENTER(id)，id 从哪来是本页的事。
 ******************************************************************************/

#include "menu_ui.h"
#include "common.h"
#include "menu/menu.h"
#include "menu/task_runner.h"

#include <stdio.h>

/**************************************************************************************************
 *                                        布局参数
 **************************************************************************************************/
#define LIST_COLS       2                                   /* 列数 */
#define LIST_GAP        12                                  /* 卡片间距 */
#define LIST_X0         UI_PAD                              /* 左边距 */
#define LIST_Y0         (UI_TITLE_H + UI_PAD)               /* 标题栏下面 */
#define LIST_BOTTOM     (UI_H - UI_HINT_H - UI_PAD)         /* 提示条上面 */
#define LIST_ROWS_MIN   3                                   /* 任务少也按 3 行排，卡片不会撑成一块 */

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
static lv_obj_t *s_page = NULL;                             /* 选择页，NULL = 还没建 */

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* 点卡片 → 发"进入第 idx 个"命令。idx 在注册回调时塞进 user_data */
static void on_pick(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    (void)menu_post(MENU_CMD_ENTER, idx, 0);
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
void menu_ui_list_draw(void)
{
    lv_obj_t  *pagebar;
    lv_coord_t cw;
    lv_coord_t ch;
    lv_coord_t area_h;
    uint8_t    n;
    uint8_t    rows;
    uint8_t    rows_use;
    uint8_t    i;
    char       buf[16];

    /* 1. 幂等：已经建过就只切回来，不重建 */
    if(s_page != NULL)
    {
        if(!ui_page_is_active(s_page))
        {
            ui_page_load(s_page);
        }
        return;
    }

    /* 2. 搭页面骨架 */
    s_page  = ui_page_create();
    pagebar = ui_title_bar(s_page, "SELECT", "");
    ui_hint_bar(s_page, "TAP A CARD TO ENTER");

    n = task_runner_count();

    snprintf(buf, sizeof(buf), "%d", (int)n);
    ui_text_set(pagebar, buf);

    if(n == 0)                          /* 一个任务都没登记，画个提示就完事 */
    {
        lv_obj_t *tip = ui_text(s_page, UI_FONT_BIG, UI_C_TEXT_DIM);

        ui_text_set(tip, "NO TASK");
        ui_text_align(tip, LV_ALIGN_CENTER, 0, 0);
        ui_page_load(s_page);
        return;
    }

    /* 3. 算格子。行数按任务数动态来；任务少时按 3 行排，卡片高度才稳定 */
    area_h   = (lv_coord_t)(LIST_BOTTOM - LIST_Y0);
    cw       = (lv_coord_t)((UI_W - 2 * UI_PAD - (LIST_COLS - 1) * LIST_GAP) / LIST_COLS);
    rows     = (uint8_t)((n + LIST_COLS - 1) / LIST_COLS);
    rows_use = (rows < LIST_ROWS_MIN) ? LIST_ROWS_MIN : rows;
    ch       = (lv_coord_t)((area_h - (rows_use - 1) * LIST_GAP) / rows_use);

    /* 4. 逐个建卡片 */
    for(i = 0; i < n; i++)
    {
        const app_task_t *task = task_runner_at(i);
        lv_coord_t        x    = (lv_coord_t)(LIST_X0 + (i % LIST_COLS) * (cw + LIST_GAP));
        lv_coord_t        y    = (lv_coord_t)(LIST_Y0 + (i / LIST_COLS) * (ch + LIST_GAP));
        lv_obj_t         *card = ui_panel(s_page, x, y, cw, ch, UI_C_CARD);
        lv_obj_t         *num;
        lv_obj_t         *name;

        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, UI_C_BORDER, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, on_pick, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        snprintf(buf, sizeof(buf), "TASK %d", i + 1);
        num = ui_text(card, UI_FONT_SMALL, UI_C_TEXT_DIM);
        ui_text_set(num, buf);
        ui_text_align(num, LV_ALIGN_TOP_MID, 0, 12);

        name = ui_text(card, UI_FONT_BIG, UI_C_TEXT);
        ui_text_set(name, (task != NULL) ? task->name : "?");
        ui_text_align(name, LV_ALIGN_BOTTOM_MID, 0, -12);
    }

    ui_page_load(s_page);
}

void menu_ui_list_cmd(uint8_t cmd, uint8_t id, int32_t value)
{
    /* 网格版没有"光标"：所有任务都摊在屏幕上，点哪张进哪张。
     * 所以 PREV / NEXT / KEY 在本布局下无事可做，这里空实现。
     * 以后要做翻页版或加高亮，在这里改（见文件头的四步说明）。 */
    (void)cmd;
    (void)id;
    (void)value;
}

void menu_ui_list_free(void)
{
    ui_page_destroy(&s_page);           /* 内部判空，删完置 NULL */
}
