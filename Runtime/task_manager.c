#include "task_manager.h"
#include <rthw.h>

#define WAKE_COMMAND 1U
#define WAKE_STOP    2U
#define COMMAND_BUDGET 4U
#define FOREGROUND_SLOT 0U
#define BACKGROUND_SLOT 1U
#define SLOT_COUNT 2U

/* 一个前台位置、一个后台位置，各自管理线程、状态和命令队列。 */
typedef struct
{
    rt_mq_t commands;
    rt_event_t wakeup;
    task_status_t status;
    const task_spec_t *spec;
    rt_bool_t attached; /* 后台任务的页面是否打开。 */
} task_slot_t;

static task_slot_t slots[SLOT_COUNT];
static rt_mq_t events;

static task_status_t slot_status(task_slot_t *slot)
{
    rt_base_t level = rt_hw_interrupt_disable();
    task_status_t copy = slot->status;
    rt_hw_interrupt_enable(level);
    return copy;
}

static task_slot_t *slot_for_id(uint16_t task_id)
{
    unsigned int i;
    if (task_id == 0U) return RT_NULL;
    for (i = 0; i < SLOT_COUNT; i++)
        if (slot_status(&slots[i]).task_id == task_id)
            return &slots[i];
    return RT_NULL;
}

task_status_t task_get_status(uint16_t task_id)
{
    task_slot_t *slot = slot_for_id(task_id);
    task_status_t empty = {0};
    return slot ? slot_status(slot) : empty;
}

rt_bool_t task_is_attached(uint16_t task_id)
{
    task_slot_t *slot = slot_for_id(task_id);
    rt_base_t level;
    rt_bool_t attached;
    if (!slot) return RT_FALSE;
    level = rt_hw_interrupt_disable();
    attached = slot->attached;
    rt_hw_interrupt_enable(level);
    return attached;
}

rt_bool_t task_foreground_active(void)
{
    task_state_t state = slot_status(&slots[FOREGROUND_SLOT]).state;
    return state == TASK_STARTING || state == TASK_RUNNING || state == TASK_STOPPING;
}

static void publish(task_slot_t *slot, task_status_t next)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (slot->status.state == TASK_STOPPING && next.state == TASK_RUNNING)
        next.state = TASK_STOPPING;
    slot->status = next;
    rt_hw_interrupt_enable(level);
}

static void send_event(const task_status_t *current, task_event_kind_t kind,
                       uint16_t command, int result)
{
    task_event_t event = {current->task_id, current->generation, kind, command, result};
    /* 事件可丢，任务状态快照仍然可靠。 */
    (void)rt_mq_send(events, &event, sizeof(event));
}

static void task_worker(void *parameter)
{
    task_slot_t *slot = parameter;
    const task_spec_t *spec = slot->spec;
    const task_ops_t *ops = spec->ops;
    task_status_t current = slot_status(slot);
    rt_tick_t period = rt_tick_from_millisecond(spec->period_ms);
    rt_tick_t last;
    rt_tick_t last_clock = rt_tick_get();
    uint32_t now_ms = 0;
    uint32_t remainder = 0;
    rt_uint32_t bits;
    task_cmd_t cmd;
    unsigned int i;

    if (current.state != TASK_STOPPING)
    {
        current.result = ops->init();
        if (current.result == 0)
        {
            current.state = TASK_RUNNING;
            publish(slot, current);
            send_event(&current, TASK_EVENT_STARTED, 0, 0);
            if (period == 0) period = 1;
            last = rt_tick_get() - period;

            while (slot_status(slot).state != TASK_STOPPING)
            {
                for (i = 0; i < COMMAND_BUDGET; i++)
                {
                    int result;
                    if (slot_status(slot).state == TASK_STOPPING ||
                        rt_mq_recv(slot->commands, &cmd, sizeof(cmd), 0) != RT_EOK)
                        break;
                    result = ops->command ? ops->command(&cmd) : -RT_ENOSYS;
                    send_event(&current, TASK_EVENT_COMMAND_DONE, cmd.code, result);
                }

                if (slot_status(slot).state == TASK_STOPPING) break;

                rt_tick_t now = rt_tick_get();
                if ((rt_tick_t)(now - last) >= period)
                {
                    uint64_t scaled = (uint64_t)(rt_tick_t)(now - last_clock) * 1000U + remainder;
                    now_ms += (uint32_t)(scaled / RT_TICK_PER_SECOND);
                    remainder = (uint32_t)(scaled % RT_TICK_PER_SECOND);
                    last_clock = now;
                    last = now;
                    ops->step(now_ms, &current.sample);
                    current.sample_valid = 1;
                    publish(slot, current);
                }

                if (i == COMMAND_BUDGET) continue;
                now = rt_tick_get();
                rt_tick_t elapsed = now - last;
                if (elapsed < period)
                    (void)rt_event_recv(slot->wakeup, WAKE_COMMAND | WAKE_STOP,
                        RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, period - elapsed, &bits);
            }
        }
        ops->stop();
    }

    current.state = current.result == 0 ? TASK_STOPPED : TASK_FAILED;
    send_event(&current, TASK_EVENT_FINISHED, 0, current.result);
    publish(slot, current);
}

rt_err_t task_manager_init(void)
{
    if (events) return RT_EOK;
    slots[FOREGROUND_SLOT].commands = rt_mq_create("fcmd", sizeof(task_cmd_t), 8, RT_IPC_FLAG_FIFO);
    slots[FOREGROUND_SLOT].wakeup = rt_event_create("fwake", RT_IPC_FLAG_FIFO);
    slots[BACKGROUND_SLOT].commands = rt_mq_create("bcmd", sizeof(task_cmd_t), 8, RT_IPC_FLAG_FIFO);
    slots[BACKGROUND_SLOT].wakeup = rt_event_create("bwake", RT_IPC_FLAG_FIFO);
    events = rt_mq_create("tevt", sizeof(task_event_t), 12, RT_IPC_FLAG_FIFO);
    if (slots[0].commands && slots[0].wakeup && slots[1].commands &&
        slots[1].wakeup && events) return RT_EOK;

    if (slots[0].commands) rt_mq_delete(slots[0].commands);
    if (slots[0].wakeup) rt_event_delete(slots[0].wakeup);
    if (slots[1].commands) rt_mq_delete(slots[1].commands);
    if (slots[1].wakeup) rt_event_delete(slots[1].wakeup);
    if (events) rt_mq_delete(events);
    slots[0].commands = slots[1].commands = RT_NULL;
    slots[0].wakeup = slots[1].wakeup = RT_NULL;
    events = RT_NULL;
    return -RT_ENOMEM;
}

rt_err_t task_start(const task_spec_t *spec)
{
    task_slot_t *slot;
    task_status_t previous;
    task_status_t next = {0};
    rt_thread_t thread;
    rt_err_t result;
    task_cmd_t discarded;
    rt_uint32_t bits;
    rt_base_t level;

    if (!events || !spec || !spec->id || !spec->period_ms || !spec->ops ||
        !spec->ops->init || !spec->ops->step || !spec->ops->stop ||
        spec->priority >= RT_THREAD_PRIORITY_MAX ||
        (spec->run_mode != TASK_RUN_FOREGROUND && spec->run_mode != TASK_RUN_BACKGROUND))
        return -RT_EINVAL;

    slot = &slots[spec->run_mode == TASK_RUN_BACKGROUND ? BACKGROUND_SLOT : FOREGROUND_SLOT];
    previous = slot_status(slot);
    if (previous.state == TASK_RUNNING || previous.state == TASK_STARTING ||
        previous.state == TASK_STOPPING)
    {
        if (spec->run_mode != TASK_RUN_BACKGROUND || previous.task_id != spec->id ||
            previous.state == TASK_STOPPING) return -RT_EBUSY;
        level = rt_hw_interrupt_disable();
        slot->attached = RT_TRUE; /* 再进后台页面，沿用原来的线程。 */
        rt_hw_interrupt_enable(level);
        return RT_EOK;
    }

    while (rt_mq_recv(slot->commands, &discarded, sizeof(discarded), 0) == RT_EOK) { }
    (void)rt_event_recv(slot->wakeup, WAKE_COMMAND | WAKE_STOP,
        RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0, &bits);

    next.task_id = spec->id;
    next.generation = previous.generation + 1U;
    next.state = TASK_STARTING;
    slot->spec = spec;
    level = rt_hw_interrupt_disable();
    slot->attached = RT_TRUE;
    rt_hw_interrupt_enable(level);
    thread = rt_thread_create(spec->thread_name, task_worker, slot,
                              spec->stack_size, spec->priority, 10);
    if (thread)
    {
        publish(slot, next);
        result = rt_thread_startup(thread);
        if (result == RT_EOK) return RT_EOK;
        rt_thread_delete(thread);
    }
    else result = -RT_ENOMEM;

    next.state = TASK_FAILED;
    next.result = result;
    publish(slot, next);
    return result;
}

rt_err_t task_send_cmd(uint16_t task_id, const task_cmd_t *cmd)
{
    task_slot_t *slot = slot_for_id(task_id);
    task_cmd_t message;
    rt_err_t result;
    if (!slot || !cmd || slot_status(slot).state != TASK_RUNNING)
        return -RT_EINVAL;
    message = *cmd;
    result = rt_mq_send(slot->commands, &message, sizeof(message));
    if (result == RT_EOK) (void)rt_event_send(slot->wakeup, WAKE_COMMAND);
    return result;
}

rt_err_t task_detach(uint16_t task_id)
{
    task_slot_t *slot = slot_for_id(task_id);
    rt_base_t level;
    if (!slot || slot != &slots[BACKGROUND_SLOT]) return -RT_EINVAL;
    level = rt_hw_interrupt_disable();
    slot->attached = RT_FALSE;
    rt_hw_interrupt_enable(level);
    if (slot->spec && slot->spec->ops->detach)
        slot->spec->ops->detach();
    return RT_EOK;
}

rt_err_t task_request_stop(uint16_t task_id)
{
    task_slot_t *slot = slot_for_id(task_id);
    rt_base_t level;
    rt_bool_t wake = RT_FALSE;
    if (!slot) return -RT_EINVAL;
    level = rt_hw_interrupt_disable();
    if (slot->status.state == TASK_STARTING || slot->status.state == TASK_RUNNING)
    {
        slot->status.state = TASK_STOPPING;
        wake = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);
    return wake ? rt_event_send(slot->wakeup, WAKE_STOP) : RT_EOK;
}

rt_err_t task_poll_event(task_event_t *event)
{
    if (!events || !event) return -RT_EINVAL;
    return rt_mq_recv(events, event, sizeof(*event), 0);
}
