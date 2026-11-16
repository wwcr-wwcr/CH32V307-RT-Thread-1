#include "task_menu.h"
#include "task_registry.h"
#include "../Tasks/task01.h"

static lv_obj_t *task00_main_screen;
static lv_obj_t *task00_grid;
static lv_obj_t *task00_pages;
static lv_obj_t *task00_navigation;
static lv_obj_t *task00_page_label;
static lv_obj_t *state_label;
static lv_obj_t *task_screen;
static lv_obj_t *alarm_notice;
static lv_timer_t *refresh_timer;
static rt_uint8_t task00_page_index;
static const task_page_t *active;
static uint32_t generation;
static uint32_t alarm_seen_sequence;
static uint32_t alarm_seen_generation;
static uint32_t alarm_notice_started;
static rt_bool_t back_requested;

static void back_clicked(lv_event_t *event)
{
    (void)event;
    if (!active) return;
    if (active->spec.run_mode == TASK_RUN_BACKGROUND)
    {
        if (task_detach(active->spec.id) == RT_EOK)
            back_requested = RT_TRUE;
    }
    else if (task_request_stop(active->spec.id) == RT_EOK)
        back_requested = RT_TRUE;
}

static void refresh(lv_timer_t *timer)
{
    static const char *const names[] =
        {"Idle", "Starting...", "Running", "Stopping...", "Stopped", "Failed"};
    task_status_t status;
    task_status_t alarm_status = task_get_status(TASK01_ID);
    task_event_t event;
    unsigned int i;
    (void)timer;

    if (alarm_status.generation != alarm_seen_generation)
    {
        alarm_seen_generation = alarm_status.generation;
        alarm_seen_sequence = 0U;
    }

    /* 后台闹钟由 GUI 线程显示；其他 TASK 的界面也能收到提示。 */
    if (alarm_notice && alarm_status.sample_valid &&
        alarm_status.sample.alarm_sequence != alarm_seen_sequence)
    {
        alarm_seen_sequence = alarm_status.sample.alarm_sequence;
        alarm_notice_started = lv_tick_get();
        lv_label_set_text_fmt(alarm_notice, "TASK01 ALARM %02u:%02u",
            (unsigned)(alarm_status.sample.alarm_target_seconds / 60U),
            (unsigned)(alarm_status.sample.alarm_target_seconds % 60U));
        lv_obj_clear_flag(alarm_notice, LV_OBJ_FLAG_HIDDEN);
    }
    if (alarm_notice && !lv_obj_has_flag(alarm_notice, LV_OBJ_FLAG_HIDDEN) &&
        lv_tick_elaps(alarm_notice_started) >= 2000U)
        lv_obj_add_flag(alarm_notice, LV_OBJ_FLAG_HIDDEN);

    for (i = 0; i < 8 && task_poll_event(&event) == RT_EOK; i++)
        if (active && !back_requested && active->event &&
            event.task_id == active->spec.id && event.generation == generation)
            active->event(&event);

    if (!active) return;
    status = task_get_status(active->spec.id);
    if (back_requested && (active->spec.run_mode == TASK_RUN_BACKGROUND ||
        status.state == TASK_STOPPED || status.state == TASK_FAILED))
    {
        lv_scr_load(task00_main_screen);
        lv_obj_del(task_screen);
        task_screen = NULL;
        active = NULL;
        return;
    }

    if (status.state == TASK_FAILED)
        lv_label_set_text_fmt(state_label, "Failed: %d", status.result);
    else
        lv_label_set_text(state_label, names[status.state]);

    if (active->refresh && status.task_id == active->spec.id &&
        status.generation == generation)
        active->refresh(&status);
}

static void show_unavailable(const char *message)
{
    lv_label_set_text(task00_page_label, message);
    lv_obj_align(task00_page_label, LV_ALIGN_TOP_MID, 0, 50);
}

static void open_task(const task_page_t *entry)
{
    lv_obj_t *button;
    lv_obj_t *label;
    lv_obj_t *content;
    task_status_t status;

    if (task_manager_init() != RT_EOK)
    {
        show_unavailable("Task manager initialization failed");
        return;
    }
    if (!refresh_timer)
    {
        refresh_timer = lv_timer_create(refresh, 80, NULL);
        if (!refresh_timer)
        {
            show_unavailable("Cannot create refresh timer");
            return;
        }
        lv_timer_pause(refresh_timer);
    }

    task_screen = lv_obj_create(NULL);
    if (!task_screen) return;
    if (task_start(&entry->spec) != RT_EOK)
    {
        lv_obj_del(task_screen);
        task_screen = NULL;
        show_unavailable("Cannot start task");
        return;
    }
    status = task_get_status(entry->spec.id);

    active = entry;
    generation = status.generation;
    back_requested = RT_FALSE;
    lv_obj_clear_flag(task_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(task_screen, lv_color_hex(0xF5F7FA), 0);
    button = lv_btn_create(task_screen);
    lv_obj_set_size(button, 78, 32);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, 4, 3);
    label = lv_label_create(button);
    lv_label_set_text(label, "< BACK");
    lv_obj_center(label);
    lv_obj_add_event_cb(button, back_clicked, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(task_screen);
    lv_label_set_text(label, entry->title);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 12);
    state_label = lv_label_create(task_screen);
    lv_obj_align(state_label, LV_ALIGN_TOP_RIGHT, -8, 12);

    content = lv_obj_create(task_screen);
    lv_obj_set_size(content, LV_PCT(95), LV_PCT(82));
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    if (entry->open) entry->open(content, entry->spec.id);

    refresh(refresh_timer);
    lv_scr_load(task_screen);
    lv_timer_resume(refresh_timer);
}

static void task00_button_event_cb(lv_event_t *event)
{
    rt_uint16_t selected;
    uint16_t id;
    size_t i;

    if (active || lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    if (lv_indev_get_act() && lv_indev_get_scroll_obj(lv_indev_get_act())) return;
    selected = lv_btnmatrix_get_selected_btn(lv_event_get_target(event));
    if (selected >= 9U) return;

    id = (uint16_t)(task00_page_index * 9U + selected + 1U);
    for (i = 0; i < task_registry_count; i++)
        if (task_registry[i].spec.id == id)
        {
            open_task(&task_registry[i]);
            return;
        }

    lv_label_set_text_fmt(task00_page_label, "TASK %u - Not configured", (unsigned)id);
    lv_obj_align(task00_page_label, LV_ALIGN_TOP_MID, 0, 50);
}

/* 创建任务按键 */
/* 松手时触发一次；九宫格不参与滚动。 */
static const char *task00_maps[2][12] = {
        {"TASK 1","TASK 2","TASK 3","\n","TASK 4","TASK 5","TASK 6","\n","TASK 7","TASK 8","TASK 9",""},
        {"TASK 10","TASK 11","TASK 12","\n","TASK 13","TASK 14","TASK 15","\n","TASK 16","TASK 17","TASK 18",""}
    };

static void task00_grid_create(lv_obj_t *parent, rt_uint8_t page)
{
    lv_obj_t *grid = lv_btnmatrix_create(parent);
    task00_grid = grid;
    lv_obj_set_size(grid, 456, 180);
    lv_obj_set_pos(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 4, 0);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(grid, lv_color_hex(0x2196F3), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(grid, lv_color_hex(0x1565C0), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(grid, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(grid, 10, LV_PART_ITEMS);
    lv_obj_set_style_border_width(grid, 0, LV_PART_ITEMS);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_btnmatrix_set_map(grid, task00_maps[page]);
    lv_btnmatrix_set_btn_ctrl_all(grid, LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(grid, task00_button_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/* 更新地图、页码和翻页键状态；只在GUI线程中调用，不创建任务线程。 */
static void task00_page_show(rt_uint8_t page)
{
    if (page >= 2U || !task00_grid || !task00_navigation) return;
    task00_page_index = page;
    lv_btnmatrix_set_map(task00_grid, task00_maps[page]);
    lv_btnmatrix_set_btn_ctrl_all(task00_grid, LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_btnmatrix_set_selected_btn(task00_grid, LV_BTNMATRIX_BTN_NONE);
    lv_label_set_text_fmt(task00_page_label, "PAGE %u / 2 - Please select a task", (unsigned)(page + 1U));
    lv_obj_align(task00_page_label, LV_ALIGN_TOP_MID, 0, 50);
    lv_btnmatrix_clear_btn_ctrl_all(task00_navigation, LV_BTNMATRIX_CTRL_DISABLED);
    /* 第一页不能再向前，第二页不能再向后。 */
    lv_btnmatrix_set_btn_ctrl(task00_navigation, page == 0U ? 0U : 1U, LV_BTNMATRIX_CTRL_DISABLED);
}

/* 0为上一页，1为下一页；一次松手只切换一次。 */
static void task00_navigation_cb(lv_event_t *event)
{
    rt_uint16_t selected;
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    selected = lv_btnmatrix_get_selected_btn(lv_event_get_target(event));
    if (selected == 0U && task00_page_index > 0U) task00_page_show(0U);
    if (selected == 1U && task00_page_index < 1U) task00_page_show(1U);
}

void task_menu_create(void)
{
    static const char *navigation_map[] = {"< PREV", "NEXT >", ""};
    lv_obj_t *screen = lv_scr_act(), *label;
    task00_main_screen = screen;
    lv_obj_clean(screen);
    task00_pages = task00_page_label = NULL;
    task00_grid = task00_navigation = NULL;
    task00_page_index = 0;
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF5F7FA), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    label = lv_label_create(screen);
    lv_obj_set_width(label, 460);
    lv_label_set_text(label, "EMBEDDED SKILLS COMPETITION");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x17324D), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);
    label = lv_label_create(screen);
    task00_page_label = label;
    lv_label_set_text(label, "PAGE 1 / 2 - Please select a task");
    lv_obj_set_style_text_color(label, lv_color_hex(0x64748B), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 50);

    task00_pages = lv_obj_create(screen);
    lv_obj_set_pos(task00_pages, 12, 82);
    lv_obj_set_size(task00_pages, 456, 180);
    lv_obj_set_style_pad_all(task00_pages, 0, 0);
    lv_obj_set_style_border_width(task00_pages, 0, 0);
    lv_obj_set_style_radius(task00_pages, 0, 0);
    lv_obj_set_style_bg_opa(task00_pages, LV_OPA_TRANSP, 0);
    /* 禁止触摸滚动，通过按键换地图，避免滑动翻页误触。 */
    lv_obj_clear_flag(task00_pages, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_set_scroll_dir(task00_pages, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(task00_pages, LV_SCROLLBAR_MODE_OFF);
    task00_grid_create(task00_pages, 0U);

    task00_navigation = lv_btnmatrix_create(screen);
    lv_obj_set_pos(task00_navigation, 12, 274);
    lv_obj_set_size(task00_navigation, 456, 36);
    lv_obj_set_style_pad_all(task00_navigation, 0, 0);
    lv_obj_set_style_pad_column(task00_navigation, 16, 0);
    lv_obj_set_style_border_width(task00_navigation, 0, 0);
    lv_obj_set_style_bg_opa(task00_navigation, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(task00_navigation, 8, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(task00_navigation, lv_color_hex(0x334155), LV_PART_ITEMS);
    lv_obj_set_style_text_color(task00_navigation, lv_color_white(), LV_PART_ITEMS);
    lv_obj_clear_flag(task00_navigation, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_btnmatrix_set_map(task00_navigation, navigation_map);
    lv_btnmatrix_set_btn_ctrl_all(task00_navigation, LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_event_cb(task00_navigation, task00_navigation_cb, LV_EVENT_VALUE_CHANGED, NULL);
    task00_page_show(0U);

    /* 顶层提示不属于任何任务页面，BACK 后仍可显示后台闹钟。 */
    alarm_notice = lv_label_create(lv_layer_top());
    lv_label_set_text(alarm_notice, "TASK01 ALARM");
    lv_obj_set_style_bg_opa(alarm_notice, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(alarm_notice, lv_color_hex(0xCC2020), 0);
    lv_obj_set_style_text_color(alarm_notice, lv_color_white(), 0);
    lv_obj_set_style_pad_all(alarm_notice, 8, 0);
    lv_obj_align(alarm_notice, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_add_flag(alarm_notice, LV_OBJ_FLAG_HIDDEN);
}
