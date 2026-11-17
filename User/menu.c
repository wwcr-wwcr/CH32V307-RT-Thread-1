#include "menu.h"
#include "task/task.h"
#include "ui/menu_ui.h"
#include "key.h"
#include "rtthread.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"
#include "ui/ui_event.h"
#include "ui/boot_ui.h"

#define PAGE_MENU     0      //菜单状态机的状态,这是在列表
#define PAGE_TASK     1      //这是到task了

#define TASK_EVT_KEY        0       //投给任务线程的消息 转发K1-K3
#define TASK_EVT_QUIT       1       //退出任务
#define TASK_EVT_SET        2       /* 触摸控件事件（滑条/按钮） */
#define TASK_EVT_BG         3       /* 转后台：线程别退，只跑 update 不再收事件 */

#define UI_STACK_SIZE       6144        //UI 线程的栈大小
#define UI_PRIORITY         9           //UI 线程优先级
#define UI_TICK             10          //线程时间片
#define UI_PERIOD_MS        10          //整个应用的统一节拍
#define MENU_MQ_DEPTH       8           //菜单按键队列数量
#define TASK_MQ_DEPTH       4           //任务事件队列数量
#define MENU_TASK_NUM       (sizeof(g_task_list) / sizeof(g_task_list[0]))      //task的总数
typedef struct                      //消息队列
{
    uint8_t type;   
    uint8_t key;
    uint8_t id;                     /* TASK_EVT_SET 用：控件编号 */
    int32_t value;                  /* TASK_EVT_SET 用：控件数值 */
} task_evt_t;

/* ---- 菜单自身的状态 ---- */

static uint8_t   s_page     = PAGE_MENU;           //菜单在哪个页
static uint8_t   s_cursor       = 0;                         //选择页的光标位置
/* ---- 当前任务 + 任务线程 ---- */

static const app_task_t *s_task = RT_NULL;      /* 当前任务（UI 线程用它渲染） */

/* ---- 菜单 + 任务调度用到的内核对象（全部只在 menu.c 内部可见） ---- */

/* 按键队列：key 线程（中断消抖后）和屏上三个按钮都往这里投 key_id_t， */
static rt_mq_t   s_key_q   = RT_NULL;
/* 任务事件队列：UI 线程往这里投 task_evt_t（K1~K3 按键 / 退出 / 触摸控件 id+数值），*/
static rt_mq_t   s_cmd_q   = RT_NULL;
/* 任务线程退出握手*/
static struct rt_semaphore s_exit_sem;
/* 当前任务线程句柄*/
static rt_thread_t s_task_tid = RT_NULL;
/* 丢事件计数*/
static uint32_t  s_lost      = 0;

/* UI 线程的控制块 + 静态栈：menu_start() 里 rt_thread_init 一次性建好，不占堆。
 * 这是全系统唯一调用 lv_* / 操作 lcd 的线程，栈 6144 字节。 */
static struct rt_thread s_ui_tid;
static rt_uint8_t       s_ui_stack[UI_STACK_SIZE];
static const app_task_t *const g_task_list[] =      //================在这里添加task
{
    &task_example,
};

/* 退出页面后仍然在后台继续跑的任务（退出时只转后台，不销毁线程）。
 *
 * 往这里加之前先想清楚两件事：
 *   1. 它占不占独占外设？ADC 突发 / DAC / TIM 这类，后台继续跑会和
 *      后面的任务抢资源，别往里放（mic_fft 和 uart_plot 都吃 ADC）。
 *   2. 它有没有 update？没有 update 的线程转后台后无事可做，白占一份栈。
 *
 * 典型适合：倒计时、闹钟、时钟、后台按键计数。
 */
static const app_task_t *const g_bg_list[] =
{
    /* 在这里加，例如：  &task_countdown,  */
    RT_NULL,                    /* 占位，保证数组非空 */
};

#define BG_LIST_NUM     (sizeof(g_bg_list) / sizeof(g_bg_list[0]))

/* 这个任务退出页面后，要不要留着线程继续跑 */
static uint8_t task_keep_alive(const app_task_t *task)
{
    uint32_t i;

    if(task == RT_NULL)
    {
        return 0;
    }

    for(i = 0; i < BG_LIST_NUM; i++)
    {
        if(g_bg_list[i] == task)
        {
            return 1;
        }
    }

    return 0;
}                                                                             

static void task_start(uint8_t index);
static void task_stop(void);
static void menu_post_key(key_id_t key);
static void menu_tap_prev(void)  { menu_post_key(KEY_ID_1); }           //触摸按键合成
static void menu_tap_next(void)  { menu_post_key(KEY_ID_2); }
static void menu_tap_enter(void) { menu_post_key(KEY_ID_3); }
/* 列表页：交给 menu_ui 一个函数搞定 */
static void menu_draw_list(void)
{
    menu_ui_render(s_cursor, (uint8_t)MENU_TASK_NUM, g_task_list[s_cursor]->name);          //渲染
}

/* K3：进入任务 */
static void menu_enter(uint8_t index)       //传入在哪个task上
{
    s_task = g_task_list[index];        //s_task就是进入线task的名字
    s_page    = PAGE_TASK;              //把状态设定成进task

    task_start(index);              /* 逻辑线程起来，界面由 render 回调画 */
}

/* K4：退出任务 */
static void menu_exit(void)
{
    const app_task_t *task = s_task;

    if(task_keep_alive(task))
    {
        task_evt_t ev;

        /* 常驻任务：不销毁线程，只发一条"转后台"，线程收到后自己切模式继续跑。
         * 这里不清 s_task_tid —— 句柄丢了无所谓，这种任务关掉后不会再打开。 */
        ev.type  = TASK_EVT_BG;
        ev.key   = 0;
        ev.id    = 0;
        ev.value = 0;
        (void)rt_mq_send(s_cmd_q, &ev, sizeof(ev));
    }
    else
    {
        task_stop();
    }

    s_page    = PAGE_MENU;              //把状态设定成在menu菜单上
    s_task = RT_NULL;                   //s_task赋0

    menu_draw_list();
    if((task != RT_NULL) && (task->ui_destroy != RT_NULL))
    {
        task->ui_destroy();
    }                                   //把lvgl删除
}

static void menu_dispatch_key(key_id_t key)             //如果key按下
{
    task_evt_t ev;                      //将task消息队列设定为ev

    if(s_page == PAGE_MENU)             //如果在menu界面上
    {
        if(key == KEY_ID_1)
        {
            s_cursor = (uint8_t)((s_cursor + MENU_TASK_NUM - 1) % MENU_TASK_NUM);
            menu_draw_list();           //渲染这个task
        }
        else if(key == KEY_ID_2)
        {
            s_cursor = (uint8_t)((s_cursor + 1) % MENU_TASK_NUM);
            menu_draw_list();
        }
        else if(key == KEY_ID_3)
        {
            menu_enter(s_cursor);       //进入这个task
        }
        return;
    }

    /* 任务页：K4 是硬退出，绝不转发 */
    if(key == KEY_ID_4)
    {
        menu_exit();
        return;
    }

    if(s_task_tid == RT_NULL)
    {
        return;
    }

    ev.type = TASK_EVT_KEY;
    ev.key  = (uint8_t)key;
    if(rt_mq_send(s_cmd_q, &ev, sizeof(ev)) != RT_EOK)
    {
        s_lost++;
    }
}


static void ui_entry(void *parameter)
{
    key_id_t key;
    static uint8_t s_boot_ready = 0;

    (void)parameter;

    /* 1. LVGL 初始化（内部会 lcd_init + lv_init + 注册显示驱动） */
    lv_port_disp_init();

    /* 1.1 触摸输入设备（GT911，内部会 gt911_init） */
    lv_port_indev_init();

    /* 1.2 触摸按钮：合成按键事件，复用现有按键逻辑 */
    menu_ui_set_tap_cb(menu_tap_prev, menu_tap_next, menu_tap_enter);

    /* 2. 先把首屏画好（动画在覆盖层下面跑完），再盖启动层 */
    menu_draw_list();
    boot_ui_show();

    /* 3. 主循环：按键 -> 渲染 -> LVGL 心跳 */
    for(;;)
    {
        /* 启动动画播完：只把覆盖层删掉，露出已经画好的首屏 */
        if((s_boot_ready == 0) && (boot_ui_done() != 0))        //只在开始调用一次
        {
            s_boot_ready = 1;
            boot_ui_close();
        }

        if(rt_mq_recv(s_key_q, &key, sizeof(key),
                      rt_tick_from_millisecond(UI_PERIOD_MS)) == RT_EOK)            //从key消息队列中读取按键信息,同时死等10ms
        {
            menu_dispatch_key(key);
        }

        /* 任务页：任务的 render 在这里跑，保证所有 lv_ 都在本线程 */
        if((s_page == PAGE_TASK) && (s_task != RT_NULL) &&              //有内容就把东西画在屏幕上
           (s_task->render != RT_NULL))
        {
            s_task->render();
        }

        /* 必须周期调用，动画和刷新都靠它 */
        lv_timer_handler();                 //lvgl的时序
    }
}


void menu_start(void)                                       //上电启动
{
    s_key_q = rt_mq_create("menu", sizeof(key_id_t), MENU_MQ_DEPTH, RT_IPC_FLAG_FIFO);          //KEY给UI线程 装按键编号
    s_cmd_q = rt_mq_create("task", sizeof(task_evt_t), TASK_MQ_DEPTH, RT_IPC_FLAG_FIFO);        //UI线程给任务线程  装事件KEY/QIOT/SET
    if(s_key_q == RT_NULL || s_cmd_q == RT_NULL)
    {
        rt_kprintf("menu: mq create failed\r\n");
        return;
    }

    rt_sem_init(&s_exit_sem, "texit", 0, RT_IPC_FLAG_FIFO);     //添加一个s_exit_sem信号量(),有人发信号+,有人消费-,释放用	rt_sem_detach

    key_event_register(menu_post_key);                  //注册按键线程
    key_start();                                        //启动按键线程

    if(rt_thread_init(&s_ui_tid, "ui", ui_entry, RT_NULL,
                      s_ui_stack, sizeof(s_ui_stack),
                      UI_PRIORITY, UI_TICK) != RT_EOK)   //初始化线程
    {
        rt_kprintf("menu: ui thread init failed\r\n");
        return;
    }

    rt_thread_startup(&s_ui_tid);               //开始UI主线程
}


/**************************************************************************************************
 *                                        七、任务线程调度                                        
 **************************************************************************************************/

static void task_entry(void *parameter)
{
    const app_task_t *task = (const app_task_t *)parameter;
    task_evt_t ev;
    rt_int32_t timeout;
    uint8_t    bg = 0;                      /* 0 = 前台，1 = 已转后台 */

    if(task->setup != RT_NULL)              //如果有开始就运行开始
    {
        task->setup();
    }

    for(;;)
    {
        /* 转后台之后就不再收事件队列了，否则会把前台任务的事件吃掉。
         * bg 是线程自己的局部变量，所以白名单里放几个任务都互不干扰。 */
        if(bg != 0u)
        {
            if(task->update != RT_NULL)
            {
                task->update();
            }

            rt_thread_mdelay(UI_PERIOD_MS);
            continue;
        }

        timeout = (task->update != RT_NULL)
                ? (rt_int32_t)rt_tick_from_millisecond(UI_PERIOD_MS)
                : (rt_int32_t)RT_WAITING_FOREVER;
                                                                                        //获取要更新的事件片
        if(rt_mq_recv(s_cmd_q, &ev, sizeof(ev), timeout) == RT_EOK)                     //UI线程给任务线程的任务,没有就等待timeout
        {
            if(ev.type == TASK_EVT_QUIT)
            {
                break;
            }

            if(ev.type == TASK_EVT_BG)      /* 被要求转后台：切模式，别退出 */
            {
                bg = 1;
                continue;
            }

            if(ev.type == TASK_EVT_SET)
            {
                if(task->on_set != RT_NULL)
                {
                    task->on_set(ev.id, ev.value);
                }
            }
            else if(task->on_key != RT_NULL)
            {
                task->on_key((key_id_t)ev.key);
            }
        }

        if(task->update != RT_NULL)
        {
            task->update();
        }
    }

    if(task->teardown != RT_NULL)           //如果有关闭就运行关闭
    {
        task->teardown();
    }

    /* 通知 UI 线程后直接返回就行。
     * 这个内核里 rt_thread_delete(self) 不会调度，返回后还会再走一遍
     * rt_thread_exit()，同一个线程被清理两次（重复 detach 定时器 +
     * 重复挂到 defunct 链表）。交给返回路径清理一次就够。 */
    rt_sem_release(&s_exit_sem);
}

static void task_stop(void)
{
    task_evt_t ev;

    if(s_task_tid == RT_NULL)
    {
        return;
    }

    /* 清掉上一次超时残留的计数，否则下一次 take 会立刻返回 */
    (void)rt_sem_control(&s_exit_sem, RT_IPC_CMD_RESET, RT_NULL);

    /* 掏空队列：上一个任务没处理完的按键/控件事件，不能流到下一个任务 */
    while(rt_mq_recv(s_cmd_q, &ev, sizeof(ev), 0) == RT_EOK)
    {
    }

    ev.type = TASK_EVT_QUIT;
    ev.key  = 0;
    (void)rt_mq_send(s_cmd_q, &ev, sizeof(ev));

    if(rt_sem_take(&s_exit_sem, rt_tick_from_millisecond(3000)) != RT_EOK)
    {
        /* 这里绝对不能强删：线程可能正阻塞在驱动里，从别的线程 free 掉
         * 它正在用的栈会把内核挂起链表和堆一起搞坏。 */
        rt_kprintf("menu: task exit timeout\r\n");
    }

    s_task_tid = RT_NULL;
}

static void task_start(uint8_t index)
{
    const app_task_t *task = g_task_list[index];

    s_task_tid = rt_thread_create(task->name, task_entry, (void *)task,                 //创建线程
                                     task->stack_size, task->priority, UI_TICK);
    if(s_task_tid == RT_NULL)
    {
        rt_kprintf("menu: create task %s failed\r\n", task->name);
        return;
    }

    rt_thread_startup(s_task_tid);
}

/* UI 线程（滑条/按钮）-> 当前任务的逻辑线程 */
void ui_task_event(uint8_t id, int32_t value)
{
    task_evt_t ev;

    if((s_cmd_q == RT_NULL) || (s_page != PAGE_TASK) ||
       (s_task_tid == RT_NULL))
    {
        return;
    }

    ev.type  = TASK_EVT_SET;
    ev.key   = 0;
    ev.id    = id;
    ev.value = value;

    (void)rt_mq_send(s_cmd_q, &ev, sizeof(ev));
}


/* 唯一的按键投递口：物理按键（key 线程）和屏上按钮（UI 线程）都走这里。
 * 两边最后都只是往菜单队列里塞一个 key_id_t，菜单逻辑不用区分来源。 */
static void menu_post_key(key_id_t key)         //在key.c被s_key_cb函数制作调用
{
    if(s_key_q == RT_NULL)
    {
        return;
    }

    if(rt_mq_send(s_key_q, &key, sizeof(key)) != RT_EOK)
    {
        s_lost++;
    }
}

static void menu_cmd(void)
{
    rt_size_t total = 0;
    rt_size_t used  = 0;

    rt_memory_info(&total, &used, RT_NULL);

    /* 菜单页 / 任务页分开打印，一眼看出 "当前在哪" 和 "光标停在哪" */
    rt_kprintf("menu : page=%s, task=%s, cursor=%d, lost=%lu\r\n",
               (s_page == PAGE_MENU) ? "SELECT" : "TASK",
               (s_task != RT_NULL) ? s_task->name : "-",
               s_cursor, (unsigned long)s_lost);
    rt_kprintf("heap : used=%lu free=%lu\r\n",
               (unsigned long)used, (unsigned long)(total - used));

    {
        lv_mem_monitor_t mon;

        lv_mem_monitor(&mon);
        rt_kprintf("lvgl : free=%u biggest=%u frag=%u%%\r\n",
                   (unsigned)mon.free_size, (unsigned)mon.free_biggest_size,
                   (unsigned)mon.frag_pct);
    }
}
MSH_CMD_EXPORT(menu_cmd, show menu state and heap usage);

static void task2(void){
    menu_enter(2);
}
MSH_CMD_EXPORT(task2,open task2);


// #include "rtthread.h"

// static int set_lux(int argc, char **argv)
// {
//     int v;

//     if(argc < 2)
//     {
//         rt_kprintf("usage: set_lux <value>\n");
//         return -1;
//     }

//     v = atoi(argv[1]);
//     rt_kprintf("set lux = %d\n", v);

//     return 0;
// }
// MSH_CMD_EXPORT(set_lux, set lux threshold);
