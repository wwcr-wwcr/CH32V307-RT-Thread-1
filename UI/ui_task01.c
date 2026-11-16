#include "ui_task01.h"

static lv_obj_t *time_label;

void ui_task01_open(lv_obj_t *parent , uint16_t task_id)
{
    (void)task_id;
    time_label = lv_label_create(parent);
    lv_label_set_text(time_label,"--:--");
    lv_obj_center(time_label);
}

void ui_task01_refresh(const task_status_t *status)
{
    unsigned int total;

    if (!status->sample_valid) return;
    total = (unsigned int)status->sample.value;
    lv_label_set_text_fmt(time_label, "%02u:%02u", total / 60U, total % 60U);
}