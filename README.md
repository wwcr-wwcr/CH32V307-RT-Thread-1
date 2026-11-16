# CH32V307 RT-Thread + LVGL 比赛工程

面向 CH32V307VCT6 开发板的嵌入式技能大赛练习工程，使用 RT-Thread、LVGL 8.3.11 和 CH32 标准外设库。工程保留两页 TASK 菜单，并将外设驱动放在 `APP/`，任务逻辑放在 `Tasks/`，界面放在 `UI/`。

## 目录

| 路径 | 内容 |
| --- | --- |
| `APP/` | 按键、数码管、RGB、蜂鸣器和传感器等外设接口 |
| `Runtime/` | 任务管理器、命令队列和状态快照 |
| `Tasks/` | TASK01 等任务逻辑 |
| `UI/` | LVGL 菜单和任务页面 |
| `lvgl/`、`lvgl_port/` | LVGL 源码及本板显示、触摸移植 |
| `rtthread/` | RT-Thread 源码及配置 |
| `Peripheral/`、`Core/`、`Startup/`、`Ld/` | MCU 外设库、启动代码和链接脚本 |
| `PC_Player/` | 电脑端 USB 音频播放脚本 |

## 编译

1. 安装支持 CH32V307 的 MounRiver Studio 3 和对应 RISC-V 工具链。
2. 在 IDE 中导入本目录中的 `.project` / `.cproject` 工程。
3. 选择 CH32V307VCT6，执行 Build。`obj/` 是 IDE 生成的编译目录，不纳入版本管理。
4. 烧录前确认开发板型号、LCD 接线和外设引脚与工程配置一致。

目前 `TASK01` 注册为后台任务：在 TASK01 页面按 BACK 只关闭页面，计时继续；再次进入会接回原线程。任务管理器预留一个后台位置和一个前台位置。其他 TASK 可在 `UI/task_registry.c` 中登记，前台任务将 `run_mode` 设为 `TASK_RUN_FOREGROUND`。

后台 TASK01 不抢占前台 TASK 的按键、数码管和 RGB。闹钟到点时，GUI 顶层显示提示；TASK01 页面可见时仍会使用蜂鸣器和红灯。屏幕输入闹钟时间的键盘界面尚未接入。

## 电脑端音频脚本

`PC_Player/play_usb_mp3.py` 需要 Python 3、`pyserial` 和 FFmpeg：

```powershell
py -3 -m pip install pyserial
```

从 FFmpeg 官方发行渠道取得 `ffmpeg.exe`，放入 `PC_Player/`，然后运行 `PC_Player/播放水手.bat`，或在命令行使用 `play_usb_mp3.py`。`ffmpeg.exe` 与本机安装的 Python 包没有放入仓库。

## 说明

仓库包含第三方组件源码，请保留其原有许可文件和源码中的版权声明。固件能否在目标板上正确运行，仍需连接实际硬件验证。
