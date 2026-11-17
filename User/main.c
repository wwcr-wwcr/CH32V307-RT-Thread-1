/********************************** (C) COPYRIGHT *******************************
* File Name          : main.c
* Description        : 应用入口
*
* 只做两件事：
*     1. 初始化板级外设（屏幕 / I2C 总线 / RTC / 按键中断）
*     2. 启动菜单，然后退出
*
* 线程分工：
*     key    常驻   中断驱动，消抖后回调           HAL/KEY/key.c
*     menu   常驻   管菜单：切界面、建/删任务线程   menu.c
*     task   动态   当前界面自己一个线程，退出即删  task_x.c
*
* 界面和业务一律不写在这里。
*********************************************************************************/

#include "rtthread.h"
#include "lcd.h"
#include "key.h"
#include "i2c.h"
#include "menu.h"

int main(void)
{
    SystemCoreClockUpdate();

    /* 1. 板级外设：失败也不阻塞，方便单独排查 */
    if(lcd_init() != 0)
    {
        rt_kprintf("main: lcd init failed\r\n");
    }

    i2c_init(I2C_DEFAULT_SPEED);
   // rtc_init();

    /* 2. 按键：配好中断并创建线程，此时先不启动 */
    key_init();

    /* 3. 注册按键回调 -> 启动按键线程 -> 启动菜单线程，main 到此结束 */
    menu_start();

    return 0;
}
