#ifndef APP_USB_AUDIO_H
#define APP_USB_AUDIO_H
#include <stdint.h>

/* 初始化USB与扬声器，取得USB接收流；不创建线程、不写歌曲到Flash。
 * 此后在单个工作线程/main循环中每1ms调用process。
 */
int app_usb_audio_init(void);
void app_usb_audio_process(void);
/* 只提交开关请求，process执行停止及所有权交接。关闭后可运行usbhs_echo。 */
void app_usb_audio_enable(uint8_t enable);
#endif
