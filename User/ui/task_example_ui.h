#ifndef __TASK_CUSTOM_UI_H
#define __TASK_CUSTOM_UI_H

#include "common.h"
#include <stdint.h>

/**
 * @brief  渲染"麦克风 / 环境光"时间轴页面
 * @param  mic    麦克风峰峰值 0~4095
 * @param  lux    VEML7700 照度 lux
 * @param  count  数码管当前值（9999 倒数）
 * @param  run    1 = 采样/节拍在跑
 * @param  music  1 = 扬声器在放歌
 * @param  beat   1 = 蜂鸣器节拍器开着
 * @param  seq    采样序号，变了才往曲线里塞一个点
 */
void example_ui_render(uint16_t mic, uint16_t lux, uint16_t count,
                       uint8_t run, uint8_t music, uint8_t beat, uint8_t seq);

void example_ui_destroy(void);

#endif
