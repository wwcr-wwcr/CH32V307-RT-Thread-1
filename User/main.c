/********************************** (C) COPYRIGHT *******************************

* File Name          : main.c

* Author             : WCH

* Version            : V1.0.0

* Date               : 2020/04/30
  作者:zhangruim
* Description        : Main program body.

*********************************************************************************

* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.

* Attention: This software (modified or not) and binary are used for 

* microcontroller manufactured by Nanjing Qinheng Microelectronics.

*******************************************************************************/

#include "ch32v30x.h"
#include <rtthread.h>
#include <rthw.h>
#include "drivers/pin.h"
#include "app_rgb.h"
#include "app_segment.h"
#include "app_key.h"
#include "app_buzzer.h"
#include "app_ntc.h"
#include "app_light.h"
#include "app_rtc.h"
#include "app_speaker.h"
#include "app_usbhs.h"
#include "app_usb_audio.h"
#include "app_mic.h"
#include "app_ultrasonic.h"


int main(void)
{
    SystemCoreClockUpdate();
    while (1)
    {
        rt_thread_mdelay(100);
    }
}

