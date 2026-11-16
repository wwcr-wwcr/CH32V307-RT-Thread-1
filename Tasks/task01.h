#ifndef TASK_01_H
#define TASK_01_H

#include "../Runtime/task_types.h"
#define TASK01_ID                  1U
#define TASK01_PERIOD_MS           10U
#define TASK01_CMD_SET_ALARM       1U  /* 设置闹钟，value 是总秒数 */

extern const task_ops_t task01_ops;

#endif
