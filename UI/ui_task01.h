#ifndef UI_TASK01_H
#define UI_TASK01_H

#include "../Runtime/task_manager.h"  
#include "lvgl.h"                     

void ui_task01_open(lv_obj_t *parent, uint16_t task_id);
void ui_task01_refresh(const task_status_t *status);

#endif