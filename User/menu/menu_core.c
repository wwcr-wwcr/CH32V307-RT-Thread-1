/******************************************************************************
 * 文件名   : menu_core.c
 * 说明     : 菜单核心：页面状态机 + UI 线程主循环。全工程唯一碰 lv_ 的地方。
 *
 * 两条页面：
 *     0 = 列表页    K1/K2 选任务，K3 进入，触摸左右箭头/卡片等效
 *     1 = 任务页    当前任务的 render() 每拍调一次；长按 K4 回列表页
 *
 * 按键路由规则（重要）：
 *     列表页   短按 K1 = 上一个   短按 K2 = 下一个   短按 K3 = 进入
 *     任务页   长按 K4(8)   = 回列表页，菜单自己处理，不下发
 *              其余所有事件 = 原样下发给任务，包括短按 K4(4)
 *
 * 命令从哪来：
 *     按键线程 / 触摸回调 / MSH 命令 / 串口帧，全部只调 menu_post()，
 *     投到 s_menu_q 里排队，由本文件的 ui_entry 单线程消费。
 *     所以页面状态只在一个线程里被改，不需要任何锁。
 ******************************************************************************/

#include "menu.h"
#include "task_runner.h"
#include "key.h"
#include "rtthread.h"
#include "ui/menu_ui.h"
#include "ui/ui_event.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"

/**************************************************************************************************
 *                                        配置
 **************************************************************************************************/
#define UI_STACK_SIZE       6144        /* UI 线程栈，LVGL 绘制吃栈 */
#define UI_PRIORITY         9           /* 比 key(8) 低，比任务(12) 高 */
#define UI_TICK             10
#define MENU_MQ_DEPTH       8           /* 命令队列深度，连按最多积 8 条 */

/**************************************************************************************************
 *                                        内部类型与变量
 **************************************************************************************************/
typedef struct
{
    uint8_t cmd;                        /* 0~5，含义见 menu.h 里 menu_post 的说明 */
    uint8_t id;                         /* cmd=0 时是 key_id_t，cmd=5 时是控件编号 */
    int32_t value;                      /* cmd=5 时的控件值，其余命令忽略 */
} menu_msg_t;

/* 页面状态：只有 ui_entry 这个线程会改，读的地方也都在同一线程里 */
static uint8_t  s_page   = 0;           /* 0 = 列表页，1 = 任务页 */
static uint8_t  s_cursor = 0;           /* 列表页光标 */
static uint32_t s_lost   = 0;           /* 队列满被丢掉的事件数 */

/* IPC 与线程 */
static rt_mq_t          s_menu_q = RT_NULL;
static struct rt_thread s_ui_tid;
static rt_uint8_t       s_ui_stack[UI_STACK_SIZE];

/**************************************************************************************************
 *                                        页面操作（只在 UI 线程调用）
 **************************************************************************************************/
/* 按当前光标重绘列表页 */
static void menu_draw_list(void)
{
    const app_task_t *task = task_runner_at(s_cursor);

    if(task == RT_NULL)
    {
        return;                         /* 一个任务都没登记，没什么可画的 */
    }

    menu_ui_render(s_cursor, task_runner_count(), task->name);
}

/* 光标移动，dir = 1 下一个，-1 上一个 */
static void menu_move(int8_t dir)
{
    uint8_t n = task_runner_count();

    if(n == 0)
    {
        return;
    }

    if(dir > 0)
    {
        s_cursor = (uint8_t)((s_cursor + 1) % n);
    }
    else
    {
        s_cursor = (uint8_t)((s_cursor + n - 1) % n);
    }

    menu_draw_list();
}

/* 进入第 index 个任务 */
static void menu_enter(uint8_t index)
{
    if(s_page != 0)
    {
        return;                         /* 已经在任务页，不重复进 */
    }

    if(task_runner_start(index) != 0)
    {
        return;                         /* 越界或线程建失败，留在列表页 */
    }

    s_cursor = index;
    s_page   = 1;                       /* 切到任务页 */
}

/* 回到列表页：常驻任务转后台，其余停线程 */
static void menu_exit(void)
{
    const app_task_t *task = task_runner_current();

    if(s_page != 1)
    {
        return;
    }

    task_runner_leave();

    s_page = 0;                         /* 切回列表页 */
    menu_draw_list();

    /* 删页面必须在 UI 线程做，这里正好是 */
    if((task != RT_NULL) && (task->ui_destroy != RT_NULL))
    {
        task->ui_destroy();
    }
}

/**************************************************************************************************
 *                                        命令分发（只在 UI 线程调用）
 **************************************************************************************************/
static void menu_handle_key(key_id_t key)
{
    /* ---- 列表页 ---- */
    if(s_page == 0)
    {
        if(key == KEY_ID_1)
        {
            menu_move(-1);
        }
        else if(key == KEY_ID_2)
        {
            menu_move(1);
        }
        else if(key == KEY_ID_3)
        {
            menu_enter(s_cursor);
        }
        /* 列表页的长按、双击暂时没有定义，忽略 */
        return;
    }

    /* ---- 任务页 ---- */

    /* 唯一被菜单截走的键：长按 K4（8）回列表页 */
    if(key == KEY_ID_LONG_4)
    {
        menu_exit();
        return;
    }

    /* 其余全部下发给任务，短按 K4 = 4 也在其中 */
    (void)task_runner_post_key(key);
}

static void menu_handle(const menu_msg_t *msg)
{
    switch(msg->cmd)
    {
        case 0:     /* 模拟按键，id 是 key_id_t */
            menu_handle_key((key_id_t)msg->id);
            break;

        case 1:     /* 光标上一个 */
            if(s_page == 0)
            {
                menu_move(-1);
            }
            break;

        case 2:     /* 光标下一个 */
            if(s_page == 0)
            {
                menu_move(1);
            }
            break;

        case 3:     /* 打开第 id 个任务，越界检查在 task_runner_start 里 */
            menu_enter(msg->id);
            break;

        case 4:     /* 回到列表页 */
            menu_exit();
            break;

        case 5:     /* 触摸控件，id 是控件编号，value 是控件值 */
            if(s_page == 1)
            {
                /* 任务页：和实体按键一样，只是转发 */
                (void)task_runner_post_set(msg->id, msg->value);
            }
            else if(msg->id == 1)       /* 列表页 1 = 上一个 */
            {
                menu_move(-1);
            }
            else if(msg->id == 2)       /* 2 = 下一个 */
            {
                menu_move(1);
            }
            else if(msg->id == 3)       /* 3 = 进入当前任务 */
            {
                menu_enter(s_cursor);
            }
            break;

        default:
            break;
    }
}

/**************************************************************************************************
 *                                        UI 线程
 **************************************************************************************************/
/* key 线程回调：只入队，不做任何菜单逻辑 */
static void menu_on_key(key_id_t key)
{
    if(menu_post_key(key) != RT_EOK)
    {
        s_lost++;
    }
}

static void ui_entry(void *parameter)
{
    menu_msg_t msg;

    (void)parameter;

    /* 1. LVGL 初始化：内部完成 lcd_init + lv_init + 注册显示设备 */
    lv_port_disp_init();

    /* 1.1 触摸输入设备：内部完成 gt911_init */
    lv_port_indev_init();

    /* 2. 直接画列表页。
     *    触摸不需要在这里注册回调：界面上的控件自己调 ui_post_event() */
    menu_draw_list();

    /* 3. 主循环：收命令 -> 渲染 -> 跑 LVGL */
    for(;;)
    {
        if(rt_mq_recv(s_menu_q, &msg, sizeof(msg),
                      rt_tick_from_millisecond(APP_TICK_MS)) == RT_EOK)
        {
            menu_handle(&msg);
        }

        /* 任务页：每拍调一次任务的 render，保证 lv_ 只在这个线程里 */
        if(s_page == 1)
        {
            const app_task_t *task = task_runner_current();

            if((task != RT_NULL) && (task->render != RT_NULL))
            {
                task->render();
            }
        }

        /* 驱动器节拍 */
        lv_timer_handler();
    }
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
void menu_start(void)
{
    if(task_runner_init() != 0)
    {
        return;
    }

    s_menu_q = rt_mq_create("menu", sizeof(menu_msg_t), MENU_MQ_DEPTH, RT_IPC_FLAG_FIFO);
    if(s_menu_q == RT_NULL)
    {
        rt_kprintf("menu: mq create failed\r\n");
        return;
    }

    key_event_register(menu_on_key);    /* 按键线程 -> 命令队列 */
    key_start();

    if(rt_thread_init(&s_ui_tid, "ui", ui_entry, RT_NULL,
                      s_ui_stack, sizeof(s_ui_stack),
                      UI_PRIORITY, UI_TICK) != RT_EOK)
    {
        rt_kprintf("menu: ui thread init failed\r\n");
        return;
    }

    rt_thread_startup(&s_ui_tid);
}

/* 所有命令的唯一入队口，先校验再发 */
static rt_err_t menu_post_ex(uint8_t cmd, uint8_t id, int32_t value)
{
    menu_msg_t msg;

    if(cmd > 5)                         /* 只有 0~5 六个命令 */
    {
        return -RT_EINVAL;
    }

    if(s_menu_q == RT_NULL)
    {
        return -RT_ERROR;               /* 菜单还没启动 */
    }

    msg.cmd   = cmd;
    msg.id    = id;
    msg.value = value;

    return rt_mq_send(s_menu_q, &msg, sizeof(msg));   /* 满时返回 -RT_EFULL */
}

rt_err_t menu_post(uint8_t cmd, uint8_t arg)
{
    return menu_post_ex(cmd, arg, 0);
}

/* ui_event.h 里的触摸入口：和按键一样只入队，由 UI 线程按页面分发 */
void ui_post_event(uint8_t id, int32_t value)
{
    (void)menu_post_ex(5, id, value);   /* 5 = 触摸控件 */
}

rt_err_t menu_post_key(key_id_t key)
{
    if((uint8_t)key >= (uint8_t)KEY_ID_MAX)
    {
        return -RT_EINVAL;
    }

    return menu_post(0, (uint8_t)key);  /* 0 = 模拟按键 */
}

rt_err_t menu_request_exit(void)
{
    return menu_post(4, 0);             /* 4 = 回列表页 */
}

uint8_t menu_task_count(void)
{
    return task_runner_count();
}

uint8_t menu_task_index(void)
{
    return s_cursor;
}

const char *menu_task_name(uint8_t index)
{
    const app_task_t *task = task_runner_at(index);

    return (task != RT_NULL) ? task->name : RT_NULL;
}

void menu_dump(void)
{
    const app_task_t *task = task_runner_current();
    rt_size_t total = 0;
    rt_size_t used  = 0;
    uint8_t   i;

    rt_kprintf("menu : page=%s, cursor=%d, task=%s, lost=%lu\r\n",
               (s_page == 0) ? "SELECT" : "TASK",
               (int)s_cursor,
               (task != RT_NULL) ? task->name : "-",
               (unsigned long)s_lost);

    rt_kprintf("tasks: %d\r\n", (int)task_runner_count());
    for(i = 0; i < task_runner_count(); i++)
    {
        const app_task_t *t = task_runner_at(i);

        rt_kprintf("  [%d] %s%s\r\n",
                   (int)i,
                   (t != RT_NULL) ? t->name : "?",
                   (i == s_cursor) ? "  <-" : "");
    }

    rt_memory_info(&total, &used, RT_NULL);
    rt_kprintf("heap : used=%lu free=%lu\r\n",
               (unsigned long)used, (unsigned long)(total - used));

    /* LVGL 自己的内存池。这里只读统计量、不分配，跨线程读是安全的；
     * 但不要在 UI 线程之外调用任何会分配/释放 lv_ 对象的接口 */
    {
        lv_mem_monitor_t mon;

        lv_mem_monitor(&mon);
        rt_kprintf("lvgl : free=%u biggest=%u frag=%u%%\r\n",
                   (unsigned)mon.free_size,
                   (unsigned)mon.free_biggest_size,
                   (unsigned)mon.frag_pct);
    }
}
