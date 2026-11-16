# CH32V307VCT6_ESAD2025 V1.1 + ST7796 + LVGL

依据新开发板原理图第 1 页 MCU 网络和第 2 页 LCD 连接器 P1。屏幕仍是 320×480 的 ST7796 16 位 8080 并口，软件横屏 480×320。板上 P1 是 2×14 排针；先对照 PCB 的 1 号标记与屏幕的 3V3/GND 方向，不要只按排针外形插。

| 新板 P1 | 屏幕信号 | MCU 引脚 | 新板 P1 | 屏幕信号 | MCU 引脚 |
|---:|---|---|---:|---|---|
| 1 | 3V3 | 3V3 | 2 | GND | GND |
| 3 | BLK | PC4 | 4 | LCD RST/RES | PD13 |
| 5 | CS | PD11 | 6 | DC | PD12 |
| 7 | WR | PD5 | 8 | RD | PD4 |
| 9 | D0 | PD14 | 10 | D1 | PD15 |
| 11 | D2 | PD0 | 12 | D3 | PD1 |
| 13 | D4 | PE7 | 14 | D5 | PE8 |
| 15 | D6 | PE9 | 16 | D7 | PE10 |
| 17 | D8 | PE11 | 18 | D9 | PE12 |
| 19 | D10 | PE13 | 20 | D11 | PE14 |
| 21 | D12 | PE15 | 22 | D13 | PD8 |
| 23 | D14 | PD9 | 24 | D15 | PD10 |
| 25 | 触摸 RST/CRES | PD3 | 26 | 触摸 INT/CINT | PD2 |
| 27 | 触摸 SCL/CSCL | PD7 | 28 | 触摸 SDA/CSDA | PD6 |

LCD 用 FSMC Bank1 的 16 位数据通路；PD12 是 A17，用地址区分命令和数据。新板的 CS 接 PD11，因此软件把 PD11 配成普通输出并保持低电平；PD7 不再作为 FSMC_NE1 输出，因为它连接触摸 SCL。WR、RD 分别保持 PD5、PD4。

LVGL 使用两块 480×10 的 RGB565 绘图缓冲区，共 19,200 字节。DMA2 通道 2 将像素以 16 位写入固定的 FSMC LCD 数据地址，同时 LVGL 可以绘制另一块缓冲区。DMA 结束后才释放总线锁并通知 LVGL。若传输报错或超时，本次开机会改用 CPU 刷新，串口会打印 `LCD DMA error/timeout`。Shell 的 `lcd_test` 仍使用 CPU 写屏，可用于排查 LCD 接线，但上板确认 DMA 时需观察 LVGL 菜单刷新和翻页。

GT911 触摸现已接入 LVGL：PD6/PD7 用开漏软件 I²C，PD3 复位，PD2 在复位期间拉低以选择 7 位地址 0x5D，随后改为输入。LVGL 线程读取触点，按屏幕横屏方向转换坐标，并把触点传给按钮等控件。当前只使用第一个触点。触摸接口必须接到 P1-25～28；如果串口提示 `I2C lines not high`，检查屏幕触摸排线、电源，以及 SDA/SCL 到 3V3 的上拉电阻。代码不会用 `Delay_Us()`，以免改写 RT-Thread 的 SysTick。

与旧板不同，新板 PA0 接风扇控制，PC3 接麦克风输入。User/main.c 的演示闪灯已改到新板的 PE6/ELED1；`led` 命令改用 PC13/ELED2，避免误驱动风扇或麦克风引脚。

烧录后先观察 LVGL 界面与串口的 `[GT911] product ID: 911...`、`LVGL touch input ready`。用手指点屏幕底部的 `LED: OFF` 按钮，松开后文字应切换为 `LED: ON`，串口应出现 `[LVGL] button clicked`。这个按钮当前只切换文字，不控制实体 LED。shell 输入 `touch_test` 可看最近一次坐标；按住屏幕时 `pressed=1`，松开后 `pressed=0`。还可输入 `lcd_test` 检查红、绿、蓝、白、黑、八色色条和网格。编译成功只证明软件集成，触摸和显示仍需在实物上确认。
