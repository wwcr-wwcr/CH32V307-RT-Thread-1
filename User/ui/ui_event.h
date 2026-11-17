/******************************************************************************
 * 文件名   : ui_event.h
 * 说明     : UI 线程 -> 当前任务逻辑线程 的事件通道
 *
 * 为什么需要它：
 *     滑条、按钮这些控件只存在于 UI 线程（所有 lv_ 调用都在那），
 *     但真正干活（例如改 DAC 频率）必须在任务的逻辑线程里做。
 *     所以控件回调里只调 ui_task_event()，把"改什么、改成多少"投递过去。
 *
 * 约定：
 *     id    由各页面自己定义（例如 DAC 页面用 DAC_UI_EV_xxx）
 *     value 控件当前值
 ******************************************************************************/

#ifndef __UI_EVENT_H
#define __UI_EVENT_H

#include <stdint.h>

/**
 * @brief  从 UI 线程给当前任务的逻辑线程发一个事件
 * @param  id     事件编号（页面自定义）
 * @param  value  事件数值（滑条位置等）
 * @note   非阻塞：消息队列满了就丢弃，不会卡住 LVGL
 */
void ui_task_event(uint8_t id, int32_t value);                  //相当于(void)rt_mq_send(s_cmd_q, &ev, sizeof(ev));

#endif