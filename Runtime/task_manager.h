#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <rtthread.h>
#include "task_types.h"

typedef enum
{
    TASK_RUN_FOREGROUND = 0,
    TASK_RUN_BACKGROUND = 1
} task_run_mode_t;

typedef struct
{
    uint16_t id;
    const char *thread_name;
    uint16_t period_ms;
    uint16_t stack_size;
    uint8_t priority;
    const task_ops_t *ops;
    task_run_mode_t run_mode; /* BACK 停止前台任务，只隐藏后台任务页面。 */
} task_spec_t;

typedef enum
{
    TASK_IDLE, TASK_STARTING, TASK_RUNNING, TASK_STOPPING, TASK_STOPPED, TASK_FAILED
} task_state_t;

typedef struct
{
    uint16_t task_id;
    uint32_t generation;
    task_state_t state;
    int result;
    task_sample_t sample;
    uint8_t sample_valid;
} task_status_t;

typedef enum { TASK_EVENT_STARTED, TASK_EVENT_COMMAND_DONE, TASK_EVENT_FINISHED } task_event_kind_t;

typedef struct
{
    uint16_t task_id;
    uint32_t generation;
    task_event_kind_t kind;
    uint16_t command;
    int result;
} task_event_t;

/* 由 GUI 线程串行调用。spec 和 ops 必须在任务停止前保持有效。 */
rt_err_t task_manager_init(void);
rt_err_t task_start(const task_spec_t *spec);
rt_err_t task_send_cmd(uint16_t task_id, const task_cmd_t *cmd);
rt_err_t task_request_stop(uint16_t task_id);
rt_err_t task_detach(uint16_t task_id);
rt_bool_t task_is_attached(uint16_t task_id);
rt_bool_t task_foreground_active(void);
task_status_t task_get_status(uint16_t task_id);
rt_err_t task_poll_event(task_event_t *event);

#endif
