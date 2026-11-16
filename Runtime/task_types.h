#ifndef TASK_TYPES_H
#define TASK_TYPES_H

#include <stdint.h>

typedef struct
{
    uint16_t code;
    int32_t value;
} task_cmd_t;

typedef struct
{
    int32_t value;
    uint32_t alarm_sequence; /* 每次闹钟触发时加一，供 GUI 显示提示。 */
    uint32_t alarm_target_seconds;
} task_sample_t;

typedef struct
{
    int (*init)(void);
    int (*command)(const task_cmd_t *cmd);
    void (*step)(uint32_t now_ms, task_sample_t *sample);
    void (*stop)(void);
    void (*detach)(void); /* 后台页面隐藏时只释放页面占用的外设。 */
} task_ops_t;

#endif
