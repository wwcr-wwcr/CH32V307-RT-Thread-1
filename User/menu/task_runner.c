/******************************************************************************
 * 文件名   : task_runner.c
 * 说明     : 任务线程的运行器。只碰线程和消息队列，不碰 LVGL、不碰菜单页面。
 *
 * 线程生命周期：
 *
 *     菜单线程                       任务线程
 *     ────────                       ────────
 *     task_runner_start(idx) ──建──> task_entry()
 *                                      setup() 初始化硬件
 *                                      ┌─ 取事件队列（有 update 就带超时）
 *                                      │  收到 TASK_EVT_KEY  -> on_key()
 *                                      │  收到 TASK_EVT_SET  -> on_set()
 *                                      │  收到 TASK_EVT_BG   -> 转后台，只刷 update()
 *                                      │  收到 TASK_EVT_QUIT -> 跳出循环
 *                                      └─ update()
 *     task_runner_stop()  ──QUIT──> teardown() → 释放信号量（线程自己结束）
 *         等信号量，最多 3 秒
 *
 * 常驻表 s_keep_list：
 *     登记过的任务退到后台时不停线程，只把 update() 继续跑着（比如计时、采样）。
 *     没登记的走「停线程」这条路，退界面就彻底释放。
 *     常驻任务转后台后线程句柄不再被菜单管理，会被下一条任务覆盖，
 *     所以常驻任务自己退出要靠 teardown，不要再指望菜单来停它。
 ******************************************************************************/

#include "task_runner.h"
#include "rtthread.h"

/**************************************************************************************************
 *                                        配置
 **************************************************************************************************/
#define TASK_MQ_DEPTH       4           /* 事件队列深度 */
#define TASK_EXIT_TIMEOUT   3000        /* 等任务线程退出的上限（ms） */

/* 事件类型：type 字段取值
 *   0 = 按键，key 有效        1 = 退出线程
 *   2 = 触摸控件，id+value    3 = 转后台，只继续跑 update() */
#define TASK_EVT_KEY        0
#define TASK_EVT_QUIT       1
#define TASK_EVT_SET        2
#define TASK_EVT_BG         3

/**************************************************************************************************
 *                                        内部类型
 **************************************************************************************************/
typedef struct
{
    uint8_t type;
    uint8_t key;
    uint8_t id;
    int32_t value;
} task_evt_t;

/**************************************************************************************************
 *                                        内部变量
 **************************************************************************************************/
/* 任务表。加新任务：先在 task.h 里加 extern，再在这里加一行 &任务名 */
static const app_task_t *const s_task_list[] =
{
    &task_example,
};

#define TASK_NUM        ((uint8_t)(sizeof(s_task_list) / sizeof(s_task_list[0])))
#define KEEP_NUM        ((uint8_t)(sizeof(s_keep_list) / sizeof(s_keep_list[0])))

/* 常驻任务：转后台时不停线程。表里只能放任务，第一个 RT_NULL 是占位，防止空数组 */
static const app_task_t *const s_keep_list[] =
{
    /* 例如：&task_countdown, */
    RT_NULL,
};

static rt_mq_t            s_cmd_q    = RT_NULL;   /* 菜单线程 -> 任务线程的事件队列 */
static struct rt_semaphore s_exit_sem;            /* 任务线程退出时释放 */
static rt_thread_t        s_task_tid = RT_NULL;   /* 当前任务线程句柄 */
static const app_task_t  *s_task     = RT_NULL;   /* 当前前台任务 */

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
/* 这个任务转后台时是否保留线程 */
static uint8_t task_is_keep(const app_task_t *task)
{
    uint8_t i;

    if(task == RT_NULL)
    {
        return 0;
    }

    for(i = 0; i < KEEP_NUM; i++)
    {
        if((s_keep_list[i] != RT_NULL) && (s_keep_list[i] == task))
        {
            return 1;
        }
    }

    return 0;
}

/* 任务线程本体 */
static void task_entry(void *parameter)
{
    const app_task_t *task = (const app_task_t *)parameter;
    task_evt_t ev;
    rt_int32_t timeout;
    uint8_t    bg = 0;                      /* 0 = 前台，1 = 已转后台 */

    if(task->setup != RT_NULL)
    {
        task->setup();
    }

    for(;;)
    {
        /* 后台：不再收事件，只按节拍跑 update()，bg 是线程私有变量所以切不回来也没关系 */
        if(bg != 0u)
        {
            if(task->update != RT_NULL)
            {
                task->update();
            }

            rt_thread_mdelay(APP_TICK_MS);
            continue;
        }

        timeout = (task->update != RT_NULL)
                ? (rt_int32_t)rt_tick_from_millisecond(APP_TICK_MS)
                : (rt_int32_t)RT_WAITING_FOREVER;

        if(rt_mq_recv(s_cmd_q, &ev, sizeof(ev), timeout) == RT_EOK)
        {
            if(ev.type == TASK_EVT_QUIT)
            {
                break;
            }

            if(ev.type == TASK_EVT_BG)
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

    if(task->teardown != RT_NULL)
    {
        task->teardown();
    }

    /* 通知菜单线程后直接返回。
     * 不要在这里调 rt_thread_delete(self)：返回后内核还会走一次 exit，
     * 同一线程被删两次会重复 detach，出现 defunct 线程 */
    rt_sem_release(&s_exit_sem);
}

/**************************************************************************************************
 *                                        对外函数
 **************************************************************************************************/
int task_runner_init(void)
{
    if(s_cmd_q != RT_NULL)
    {
        return 0;                           /* 重复调用直接成功 */
    }

    s_cmd_q = rt_mq_create("task", sizeof(task_evt_t), TASK_MQ_DEPTH, RT_IPC_FLAG_FIFO);
    if(s_cmd_q == RT_NULL)
    {
        rt_kprintf("task: mq create failed\r\n");
        return -1;
    }

    rt_sem_init(&s_exit_sem, "texit", 0, RT_IPC_FLAG_FIFO);

    return 0;
}

uint8_t task_runner_count(void)
{
    return TASK_NUM;
}

const app_task_t *task_runner_at(uint8_t index)
{
    if(index >= TASK_NUM)
    {
        return RT_NULL;
    }

    return s_task_list[index];
}

const app_task_t *task_runner_current(void)
{
    return s_task;
}

int task_runner_start(uint8_t index)
{
    const app_task_t *task;

    if(s_cmd_q == RT_NULL)
    {
        return -1;
    }

    task = task_runner_at(index);           /* 越界检查在 task_runner_at 里 */
    if(task == RT_NULL)
    {
        rt_kprintf("task: index %d out of range (0~%d)\r\n",
                   (int)index, (int)TASK_NUM - 1);
        return -1;
    }

    if(s_task_tid != RT_NULL)
    {
        return -1;                          /* 已经有一条任务线程在跑，先停掉再来 */
    }

    s_task_tid = rt_thread_create(task->name, task_entry, (void *)task,
                                  task->stack_size, task->priority, 10);
    if(s_task_tid == RT_NULL)
    {
        rt_kprintf("task: create %s failed\r\n", task->name);
        return -1;
    }

    s_task = task;
    rt_thread_startup(s_task_tid);

    return 0;
}

void task_runner_stop(void)
{
    task_evt_t ev;

    if(s_task_tid == RT_NULL)
    {
        return;
    }

    /* 清掉上一轮留下的计数，避免 take 立刻返回 */
    (void)rt_sem_control(&s_exit_sem, RT_IPC_CMD_RESET, RT_NULL);

    /* 清空队列里还没处理的按键/控件事件，它们对已经要退出的任务没有意义 */
    while(rt_mq_recv(s_cmd_q, &ev, sizeof(ev), 0) == RT_EOK)
    {
    }

    ev.type  = TASK_EVT_QUIT;
    ev.key   = 0;
    ev.id    = 0;
    ev.value = 0;
    (void)rt_mq_send(s_cmd_q, &ev, sizeof(ev));

    if(rt_sem_take(&s_exit_sem, rt_tick_from_millisecond(TASK_EXIT_TIMEOUT)) != RT_EOK)
    {
        /* 超时也不强删线程：栈和控制块还在用，硬删会留下野内存。
         * 这种情况一般是任务里的 update() 卡住了，去查那个任务 */
        rt_kprintf("task: exit timeout\r\n");
    }

    s_task_tid = RT_NULL;
    s_task     = RT_NULL;
}

void task_runner_leave(void)
{
    const app_task_t *task = s_task;

    if(task == RT_NULL)
    {
        return;
    }

    if(task_is_keep(task))
    {
        task_evt_t ev;

        /* 常驻任务：只发一条「转后台」，线程继续跑 update() */
        ev.type  = TASK_EVT_BG;
        ev.key   = 0;
        ev.id    = 0;
        ev.value = 0;
        (void)rt_mq_send(s_cmd_q, &ev, sizeof(ev));

        /* 放弃这条线程的管理权：句柄置空，否则下一条任务会因为
         * s_task_tid != NULL 而启动失败。后台线程从此不受菜单控制，
         * 它自己退出只能靠内部逻辑，不要再指望 task_runner_stop() */
        s_task     = RT_NULL;
        s_task_tid = RT_NULL;
    }
    else
    {
        task_runner_stop();
    }
}

rt_err_t task_runner_post_key(key_id_t key)
{
    task_evt_t ev;

    if((s_cmd_q == RT_NULL) || (s_task_tid == RT_NULL) || (s_task == RT_NULL))
    {
        return -RT_ERROR;                   /* 不在任务页，丢掉 */
    }

    ev.type  = TASK_EVT_KEY;
    ev.key   = (uint8_t)key;
    ev.id    = 0;
    ev.value = 0;

    return rt_mq_send(s_cmd_q, &ev, sizeof(ev));
}

rt_err_t task_runner_post_set(uint8_t id, int32_t value)
{
    task_evt_t ev;

    if((s_cmd_q == RT_NULL) || (s_task_tid == RT_NULL) || (s_task == RT_NULL))
    {
        return -RT_ERROR;
    }

    ev.type  = TASK_EVT_SET;
    ev.key   = 0;
    ev.id    = id;
    ev.value = value;

    return rt_mq_send(s_cmd_q, &ev, sizeof(ev));
}
