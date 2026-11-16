#ifndef TASK_REGISTRY_H
#define TASK_REGISTRY_H

#include "lvgl.h"
#include "../Runtime/task_manager.h"
#include <stddef.h>

typedef struct
{
    task_spec_t spec;
    const char *title;
    void (*open)(lv_obj_t *parent, uint16_t task_id);
    void (*refresh)(const task_status_t *status);
    void (*event)(const task_event_t *event);
} task_page_t;

extern const task_page_t task_registry[];
extern const size_t task_registry_count;

#endif
