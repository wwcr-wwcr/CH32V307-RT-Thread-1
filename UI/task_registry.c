#include "task_registry.h"
#include "ui_task01.h"
#include "../tasks/task01.h"

/* 在结束项前登记任务，菜单编号为 1～18，每个编号只能使用一次。 */
const task_page_t task_registry[] =
{
    {
    {TASK01_ID, "task01", TASK01_PERIOD_MS, 1024, 12, &task01_ops, TASK_RUN_BACKGROUND},
    "TASK 01 - TIMER",
    ui_task01_open, ui_task01_refresh, NULL
    },

    {0} /* 结束项 */
};
const size_t task_registry_count = sizeof(task_registry) / sizeof(task_registry[0]) - 1U;
