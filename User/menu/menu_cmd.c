/******************************************************************************
 * 文件名   : menu_cmd.c
 * 说明     : MSH 调试命令。这里只做「字符串 -> menu_post()」的翻译，
 *            不含任何菜单逻辑，也不直接碰页面和 LVGL。
 *
 * 用法：
 *     menu                打印菜单状态、任务清单、堆占用
 *     menu list           同上
 *     menu open 0         打开第 0 个任务（等同列表页按 K3）
 *     menu exit           回到列表页（等同任务页长按 K4）
 *     menu next           光标下一个
 *     menu prev           光标上一个
 *
 * 所有操作都是投递命令后立刻返回，真正的切换发生在 UI 线程下一次循环里。
 * 序号越界、队列满都会打印原因并返回 -1，不会像旧版 task2() 那样野指针崩溃。
 ******************************************************************************/

#include "menu.h"
#include "rtthread.h"

#include <stdlib.h>     /* atoi */

/**************************************************************************************************
 *                                        内部函数
 **************************************************************************************************/
static void menu_usage(void)
{
    rt_kprintf("usage: menu [list | open <n> | exit | next | prev]\r\n");
    rt_kprintf("  menu             print menu state, task list and heap\r\n");
    rt_kprintf("  menu open <n>    open task n, same as K3 on list page\r\n");
    rt_kprintf("  menu exit        back to list, same as long press K4\r\n");
    rt_kprintf("  menu next/prev   move cursor on list page\r\n");
}

/* 投递命令并统一处理失败原因 */
static int menu_send(uint8_t cmd, uint8_t arg)
{
    rt_err_t err = menu_post(cmd, arg);

    if(err == RT_EOK)
    {
        return 0;
    }

    if(err == -RT_EFULL)
    {
        rt_kprintf("menu: command queue full, try again\r\n");
    }
    else
    {
        rt_kprintf("menu: post failed (%d)\r\n", (int)err);
    }

    return -1;
}

/**************************************************************************************************
 *                                        MSH 命令
 **************************************************************************************************/
static int menu(int argc, char **argv)
{
    if(argc < 2)
    {
        menu_dump();
        return 0;
    }

    if(rt_strcmp(argv[1], "list") == 0)
    {
        menu_dump();
        return 0;
    }

    if(rt_strcmp(argv[1], "open") == 0)
    {
        int idx;

        if(argc < 3)
        {
            rt_kprintf("menu: 缺少序号，例如 menu open 0\r\n");
            return -1;
        }

        idx = atoi(argv[2]);

        /* 越界在本地就拦掉，不把非法序号投进队列 */
        if((idx < 0) || (idx >= (int)menu_task_count()))
        {
            rt_kprintf("menu: 序号 %d 越界，有效范围 0~%d\r\n",
                       idx, (int)menu_task_count() - 1);
            return -1;
        }

        return menu_send(3, (uint8_t)idx);      /* 3 = 打开第 arg 个任务 */
    }

    if(rt_strcmp(argv[1], "exit") == 0)
    {
        return menu_send(4, 0);                 /* 4 = 回列表页 */
    }

    if(rt_strcmp(argv[1], "next") == 0)
    {
        return menu_send(2, 0);                 /* 2 = 光标下一个 */
    }

    if(rt_strcmp(argv[1], "prev") == 0)
    {
        return menu_send(1, 0);                 /* 1 = 光标上一个 */
    }

    menu_usage();
    return -1;
}
MSH_CMD_EXPORT(menu, menu: list/open/exit/next/prev);
