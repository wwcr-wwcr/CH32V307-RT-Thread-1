/******************************************************************************
 * 文件名   : menu.c
 * 说明     : 菜单的全部逻辑，由原来的 menu_core.c + menu_cmd.c 合并而成。
 *
 * ┌─ 链路一句话 ────────────────────────────────────────────────────────────┐
 * │ 任何线程 menu_post() 入队  →  ui_entry() 单线程取出  →  menu_dispatch() │
 * │   ├ 有任务在跑 → 转发给 task_runner（BACK 例外，核心自己处理）          │
 * │   └ 没有任务   →  ENTER 由核心处理，其余交给 menu_ui_list_cmd()         │
 * └────────────────────────────────────────────────────────────────────────┘
 *
 * 核心只维护一件事：有没有任务在跑。它从 task_runner_current() 派生，
 * 不保存页面状态、不保存光标、不知道一共有几个任务。
 * 「显示几个、怎么排版、怎么选」全在 User/ui/menu_ui.c。
 *
 * 文件四块，从上往下读一遍就能看完整条链路：
 *     一、命令      消息结构与队列
 *     二、状态机    进入 / 退出 / 分发
 *     三、线程      ui_entry / menu_start
 *     四、外围      menu_post / 按键回调 / MSH 命令 / menu_dump
 ******************************************************************************/

#include "menu.h"
#include "task_runner.h"
#include "key.h"
#include "rtthread.h"
#include "ui/menu_ui.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"

#include <stdlib.h>     /* atoi */

#define UI_STACK_SIZE       6144        /* UI 线程栈，LVGL 绘制吃栈 */
#define UI_PRIORITY         9           /* 比 key(8) 低，比任务(12) 高 */
#define UI_TICK             10
#define MENU_MQ_DEPTH       8           /* 命令队列深度，连按最多积 8 条 */

/*==============================================================================
 *  一、命令
 *============================================================================*/

/* 命令三元组。id 的含义随 cmd 变，见 menu.h */
typedef struct
{
    uint8_t cmd;
    uint8_t id;
    int32_t value;
} menu_msg_t;

static rt_mq_t          s_q    = RT_NULL;   /* 命令队列，全工程只有 ui_entry 收 */
static uint32_t         s_lost = 0;         /* 队列满被丢掉的命令数 */
static struct rt_thread s_ui_tid;
static rt_uint8_t       s_ui_stack[UI_STACK_SIZE];

/* 外围的按键回调，定义在文件末尾，menu_start() 里要注册它 */
static void on_key(key_id_t key);

/*==============================================================================
 *  二、状态机
 *  只有"进任务""退任务""分发"三件事。页面靠 task_runner_current() 派生，
 *  所以这里既不存页面状态，也不存光标。
 *============================================================================*/

/**
 * @brief  进入第 id 个任务（只在选择页会走到）
 * @note   先确认任务存在、线程起得来，成功后才把选择页回收掉。
 *         回收必须在 task_runner_start() 之后：此时任务已经在跑，下一拍
 *         render() 会把任务页建出来并 load，中间的"没有活动屏幕"窗口期
 *         不会有 lv_timer_handler 去刷屏。
 */
static void menu_enter(uint8_t id)
{
    if(task_runner_at(id) == RT_NULL)
    {
        rt_kprintf("menu: 任务 %d 不存在\r\n", (int)id);
        return;
    }

    if(task_runner_start(id) != 0)
    {
        return;                         /* 线程建失败，留在选择页 */
    }

    menu_ui_list_free();
}

/**
 * @brief  退出当前任务，回选择页
 * @note   顺序不能换：先把选择页切回来，再删任务页。
 *         反过来会删掉正在显示的 LVGL 页面，紧接着 load 时踩到已释放的对象。
 */
static void menu_back(void)
{
    const app_task_t *task = task_runner_current();

    if(task == RT_NULL)
    {
        return;
    }

    task_runner_leave();                /* 常驻任务转后台，其余停线程 */
    menu_ui_list_draw();                /* 先有页面可以显示 */

    if(task->ui_destroy != RT_NULL)
    {
        task->ui_destroy();             /* 再删任务页 */
    }
}

/**
 * @brief  命令分发：按"有没有任务在跑"分成两条路
 */
static void menu_dispatch(uint8_t cmd, uint8_t id, int32_t value)
{
    /* ---------- 任务页 ---------- */
    if(task_runner_current() != RT_NULL)
    {
        switch(cmd)
        {
            case MENU_CMD_BACK:
                menu_back();
                break;

            case MENU_CMD_KEY:
                (void)task_runner_post_key((key_id_t)id);
                break;

            case MENU_CMD_SET:
                (void)task_runner_post_set(id, value);
                break;

            default:                    /* PREV / NEXT / ENTER 在任务页没有意义 */
                break;
        }
        return;
    }

    /* ---------- 选择页：只有"进入任务"归核心 ---------- */
    if(cmd == MENU_CMD_ENTER)
    {
        menu_enter(id);
    }
    else
    {
        menu_ui_list_cmd(cmd, id, value);
    }
}

/*==============================================================================
 *  三、线程
 *============================================================================*/
static void ui_entry(void *parameter)
{
    menu_msg_t msg;

    (void)parameter;

    lv_port_disp_init();                /* 内部完成 lcd_init + lv_init + 注册显示 */
    lv_port_indev_init();               /* 内部完成 gt911_init */
    menu_ui_list_draw();                /* 开机就是选择页 */

    for(;;)
    {
        /* 1. 收命令，最多等一拍 */
        if(rt_mq_recv(s_q, &msg, sizeof(msg),
                      rt_tick_from_millisecond(APP_TICK_MS)) == RT_EOK)
        {
            menu_dispatch(msg.cmd, msg.id, msg.value);
        }

        /* 2. 有任务就让任务的 render 刷一帧（lv_ 只能在这个线程） */
        {
            const app_task_t *task = task_runner_current();

            if((task != RT_NULL) && (task->render != RT_NULL))
            {
                task->render();
            }
        }

        /* 3. 跑 LVGL。
         *    刚进任务那一拍，选择页已被回收、任务第一帧还没建出来，
         *    act_scr 是 NULL，这时候不能刷，否则会解引用空屏 */
        if(lv_scr_act() != RT_NULL)
        {
            lv_timer_handler();
        }
    }
}

void menu_start(void)
{
    if(task_runner_init() != 0)
    {
        return;
    }

    s_q = rt_mq_create("menu", sizeof(menu_msg_t), MENU_MQ_DEPTH, RT_IPC_FLAG_FIFO);
    if(s_q == RT_NULL)
    {
        rt_kprintf("menu: mq create failed\r\n");
        return;
    }

    key_event_register(on_key);         /* 按键线程 -> 命令队列 */
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

/*==============================================================================
 *  四、外围
 *  下面只做"把外部输入翻译成命令"，不写菜单业务。
 *============================================================================*/
rt_err_t menu_post(uint8_t cmd, uint8_t id, int32_t value)
{
    menu_msg_t msg;

    if(cmd >= MENU_CMD_NUM)
    {
        return -RT_EINVAL;
    }

    if(s_q == RT_NULL)
    {
        return -RT_ERROR;               /* 菜单还没启动 */
    }

    msg.cmd   = cmd;
    msg.id    = id;
    msg.value = value;

    return rt_mq_send(s_q, &msg, sizeof(msg));   /* 满时返回 -RT_EFULL */
}

/* 按键线程回调：只翻译成命令，不判断当前在哪个页面。
 * 长按 K4 是"返回"这个语义动作，所以直接发 BACK，其余原样发 KEY */
static void on_key(key_id_t key)
{
    uint8_t cmd = (key == KEY_ID_LONG_4) ? MENU_CMD_BACK : MENU_CMD_KEY;

    if(menu_post(cmd, (uint8_t)key, 0) != RT_EOK)
    {
        s_lost++;
    }
}

/**************************************************************************************************
 *                                        MSH 命令
 **************************************************************************************************/
static void menu_usage(void)
{
    rt_kprintf("usage: menu [list | open <n> | exit | next | prev]\r\n");
    rt_kprintf("  menu             print menu state, task list and heap\r\n");
    rt_kprintf("  menu open <n>    open task n\r\n");
    rt_kprintf("  menu exit        back to select page (same as long press K4)\r\n");
    rt_kprintf("  menu next/prev   forwarded to the list page\r\n");
}

/* 投递命令并统一处理失败原因 */
static int menu_send(uint8_t cmd, uint8_t id)
{
    rt_err_t err = menu_post(cmd, id, 0);

    if(err == RT_EOK)
    {
        return 0;
    }

    if(err == -RT_EFULL)
    {
        rt_kprintf("menu: command queue full, try again\r\n");
    }
    else
    {
        rt_kprintf("menu: post failed (%d)\r\n", (int)err);
    }

    return -1;
}

static int menu(int argc, char **argv)
{
    if(argc < 2)
    {
        menu_dump();
        return 0;
    }

    if(rt_strcmp(argv[1], "list") == 0)
    {
        menu_dump();
        return 0;
    }

    if(rt_strcmp(argv[1], "open") == 0)
    {
        int idx;

        if(argc < 3)
        {
            rt_kprintf("menu: 缺少序号，例如 menu open 0\r\n");
            return -1;
        }

        if(task_runner_current() != RT_NULL)
        {
            rt_kprintf("menu: 已在任务页，先 menu exit\r\n");
            return -1;
        }

        idx = atoi(argv[2]);

        if((idx < 0) || (idx >= (int)task_runner_count()))
        {
            rt_kprintf("menu: 序号 %d 越界，有效范围 0~%d\r\n",
                       idx, (int)task_runner_count() - 1);
            return -1;
        }

        return menu_send(MENU_CMD_ENTER, (uint8_t)idx);
    }

    if(rt_strcmp(argv[1], "exit") == 0)
    {
        if(task_runner_current() == RT_NULL)
        {
            rt_kprintf("menu: 已在选择页\r\n");
            return 0;
        }

        return menu_send(MENU_CMD_BACK, 0);
    }

    if(rt_strcmp(argv[1], "next") == 0)
    {
        return menu_send(MENU_CMD_NEXT, 0);
    }

    if(rt_strcmp(argv[1], "prev") == 0)
    {
        return menu_send(MENU_CMD_PREV, 0);
    }

    menu_usage();
    return -1;
}
MSH_CMD_EXPORT(menu, menu: list/open/exit/next/prev);

/**************************************************************************************************
 *                                        调试
 **************************************************************************************************/
void menu_dump(void)
{
    const app_task_t *task = task_runner_current();
    rt_size_t total = 0;
    rt_size_t used  = 0;
    uint8_t   i;
    lv_mem_monitor_t mon;

    rt_kprintf("page : %s\r\n", (task != RT_NULL) ? "TASK" : "SELECT");

    if(task != RT_NULL)
    {
        rt_kprintf("task : %s\r\n", task->name);
    }

    rt_kprintf("lost : %lu\r\n", (unsigned long)s_lost);

    rt_kprintf("tasks: %d\r\n", (int)task_runner_count());
    for(i = 0; i < task_runner_count(); i++)
    {
        const app_task_t *t = task_runner_at(i);

        rt_kprintf("  [%d] %s\r\n", (int)i, (t != RT_NULL) ? t->name : "?");
    }

    rt_memory_info(&total, &used, RT_NULL);
    rt_kprintf("heap : used=%lu free=%lu\r\n",
               (unsigned long)used, (unsigned long)(total - used));

    /* 只读统计量、不分配，跨线程读是安全的 */
    lv_mem_monitor(&mon);
    rt_kprintf("lvgl : free=%u biggest=%u frag=%u%%\r\n",
               (unsigned)mon.free_size,
               (unsigned)mon.free_biggest_size,
               (unsigned)mon.frag_pct);
}
