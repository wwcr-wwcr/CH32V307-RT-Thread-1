# CH32V307 RT-Thread + LVGL 工程

CH32V307VCT6（RISC-V 青稞 V4F，144MHz，224KB Flash / 96KB RAM）上的菜单式多任务框架。
内核用 RT-Thread 3.1.3，界面用 LVGL 8.2，屏幕是 ST7796S（320×480，FSMC 并口）+ GT911 触摸。

设计目标只有两条：**任务之间互不干扰，加一个任务不用改菜单。**

---

## 1. 运行结构

```
                    ┌──────────────┐
   PE2~PE5 按键 ───▶ │  key 线程     │ 优先级 8，栈 1536
                    │  消抖 + 状态机 │ 产出短按/长按/双击
                    └──────┬───────┘
                           │ menu_post(0, key_id)  只入队
   屏幕触摸 ────────────────┤
   MSH 命令 menu open N ────┤        ┌─────────────────┐
   UART 帧（预留）──────────┴──────▶ │ 菜单命令队列      │ 深度 8
                                      └────────┬────────┘
                                               │ 唯一消费者
                                      ┌────────▼────────┐
                                      │  ui 线程         │ 优先级 9，栈 6144
                                      │  页面状态机       │ ★全工程唯一调 lv_ 的线程
                                      └───┬─────────┬───┘
                            建/停线程      │         │ 每 10ms 调一次 render()
                                      ┌───▼─────┐   │
                                      │ 任务线程 │◀──┘
                                      │ 优先级12 │ 动态创建/销毁
                                      └─────────┘
                                           │ task_evt 队列（深度 4）
                                           ▼
                                      setup / on_key / on_set / update / teardown
```

一句话概括：**所有输入源都只往队列里投命令，`ui` 线程一个人消费，所以页面状态不需要锁，`lv_*` 也永远只在一个线程里跑。**

## 2. 目录

| 路径 | 内容 |
| --- | --- |
| `User/` | 应用层，见下一节 |
| `HAL/` | 外设驱动，一个外设一个目录（KEY / LCD / MIC / AUDIO / BEEP / DAC / ADC / I2C / RTC / TIM / VEML7700 / DigitalTube 等） |
| `drivers/` | RT-Thread 设备驱动（`drv_gpio`、`drv_usart`）与 `uart_stream`（UART1 二进制帧通道） |
| `rtthread/` | 内核、finsh（MSH 命令行）及配置 `rtconfig.h` |
| `LVGL/GUI/lvgl/` | LVGL 8.2 源码 |
| `LVGL/GUI/lvgl/examples/porting/` | 本板显示与触摸移植层 |
| `Peripheral/`、`Core/`、`Startup/`、`Ld/` | CH32 标准外设库、启动代码、链接脚本 |
| `Debug/` | WCH 的 `printf` 重定向与延时（调试打印走 UART1） |
| `obj/` | IDE 生成的编译输出，不入库 |

> `HAL/` 里还有一批当年写的、本板没有对应硬件的驱动（NFC、MOTOR、Attitude、PD、CS100、ASRPRO、BleUart、NTC、LED 等），保留但不参与当前主线。

### `User/` 的分工

| 文件 | 职责 | 能碰 lv_？ |
| --- | --- | --- |
| `main.c` | 只做板级初始化然后调 `menu_start()` | 否 |
| `menu/menu.h` | 对外唯一接口：命令号 + `menu_start` / `menu_post` / `menu_dump` | — |
| `menu/menu.c` | 菜单全部逻辑：命令、状态机、线程、MSH、dump | **是（唯一）** |
| `menu/task_runner.c` | 任务线程生命周期：创建 / 停止 / 转后台 / 投事件 | 否 |
| `task/task.h` | `app_task_t` 定义，任务回调契约 | — |
| `task/task_example.c` | 示例任务（麦克风 / 光照 / 数码管 / 音乐 / 节拍器） | 否 |
| `ui/common.c` | 通用控件封装（页面、卡片、按钮、滑条、曲线） | 是 |
| `ui/menu_ui.c` | 选择页（网格版）：排版、卡片、点击进入 | 是 |
| `ui/task_example_ui.c` | 示例任务的界面 | 是 |

### 菜单核心只认一件事：有没有任务在跑

核心**不保存**页面状态和光标，全部从 `task_runner_current()` 派生：

| | 任务页 | 选择页 |
| --- | --- | --- |
| 判据 | `task_runner_current() != NULL` | `task_runner_current() == NULL` |
| 收到 BACK | 核心处理（退出） | 转给 `menu_ui_list_cmd` |
| 收到 KEY / SET | 转发给 `task_runner` | 转给 `menu_ui_list_cmd` |
| 收到 PREV / NEXT | 忽略 | 转给 `menu_ui_list_cmd` |
| 收到 ENTER | 忽略 | 核心处理（建线程进任务） |

「一屏显示几个任务、怎么排版、怎么选、有没有光标」**全部**在 `ui/menu_ui.c`，`menu.c` 完全不知道。

### menu.c 分四块

从上往下读一遍就能看完整条链路：

| 块 | 内容 |
| --- | --- |
| 一、命令 | 消息结构 `menu_msg_t`、队列、线程句柄 |
| 二、状态机 | `menu_enter()` / `menu_back()` / `menu_dispatch()` |
| 三、线程 | `ui_entry()` 主循环、`menu_start()` |
| 四、外围 | `menu_post()` / `on_key()` / MSH `menu` 命令 / `menu_dump()` |

### 触摸统一发命令

| 位置 | 回调里写 | 效果 |
| --- | --- | --- |
| 选择页 卡片 | `menu_post(MENU_CMD_ENTER, i, 0)` | 进第 i 个任务 |
| 任务页 按钮/滑条 | `menu_post(MENU_CMD_SET, id, value)` | 转给当前任务的 `on_set` |

不伪造按键事件，也没有第二套语义。

## 3. 线程

| 线程 | 优先级 | 栈 | 何时存在 |
| --- | --- | --- | --- |
| `key` | 8 | 1536 | 常驻 |
| `ui` | 9 | 6144 | 常驻 |
| 任务线程 | 12（由任务自定） | 1024~2048 | 进界面创建，退界面销毁 |

优先级数字越大越低。`key` 比 `ui` 高，保证按键不丢；任务放到 12，保证界面永远比业务先跑。

整机内存占用 = key + ui + 当前这一个任务，不随任务数量增长。

## 4. 按键事件

每个物理键产出三种事件，编号连成一张表：

| 物理键 | 短按 | 长按 | 双击 |
| --- | --- | --- | --- |
| K1 (PE2) | 1 | 5 | 9 |
| K2 (PE3) | 2 | 6 | 10 |
| K3 (PE4) | 3 | 7 | 11 |
| K4 (PE5) | 4 | 8 | 12 |

编号规律：`id = 物理键序号(0~3) + 事件类型 × 4`，可用 `KEY_ID_OF()` / `KEY_PHYS_OF()` / `KEY_KIND_OF()` 换算。

时序（`HAL/KEY/key.c` 顶部可调）：

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| `KEY_DEBOUNCE_MS` | 20 | 电平稳定这么久才算按下 |
| `KEY_LONG_MS` | 700 | 按住超过这个时间报长按（按住过程中就报，不用等松开） |
| `KEY_DBL_MS` | 220 | 松开后这么久内再按算双击 |

⚠️ **短按有 220ms 延迟**：为了和双击互斥，短按要等双击窗口过期才上报，否则双击会先冒出两个短按、菜单连翻两格。嫌不跟手就调小 `KEY_DBL_MS`（120~150），代价是手快点两下可能被识别成双击。

### 按键路由规则

| 页面 | 行为 |
| --- | --- |
| 列表页 | 短按 K1 = 上一个，短按 K2 = 下一个，短按 K3 = 进入 |
| 任务页 | **长按 K4（8）= 回列表页**，由菜单截走，不下发 |
| 任务页 | 其余全部下发给任务，**包括短按 K4（4）** |

所以任务里短按 K4 是可以自己用的，只有长按 K4 被菜单占用。

## 5. 菜单命令协议

所有输入源（按键、触摸、MSH、以后接的 UART 帧）都只调这一个函数：

```c
rt_err_t menu_post(uint8_t cmd, uint8_t id, int32_t value);
```

| 命令 | 含义 | id / value |
| --- | --- | --- |
| `MENU_CMD_KEY` | 按键事件，**只有按键线程发** | `id` = `key_id_t`：短按 1~4 / 长按 5~8 / 双击 9~12 |
| `MENU_CMD_PREV` | 上一个 | — |
| `MENU_CMD_NEXT` | 下一个 | — |
| `MENU_CMD_ENTER` | 进入第 N 个任务 | `id` = 任务序号，从 0 开始 |
| `MENU_CMD_BACK` | 退出当前任务（等同长按 K4） | — |
| `MENU_CMD_SET` | 任务页的触摸控件 | `id` = 控件编号，`value` = 控件数值 |

命令号定义在 `menu.h`，代码里**不出现裸数字**。列表页收到 PREV/NEXT/KEY 时怎么处理由 `menu_ui_list_cmd()` 决定（网格版是空实现）。

返回值：`RT_EOK` 成功；`-RT_EFULL` 队列满（命令被丢弃，调用方要能感知）；`-RT_EINVAL` 命令号非法；`-RT_ERROR` 菜单还没启动。

只入队、不阻塞，任意线程可调。真正的切换发生在 `ui` 线程下一次循环里。

## 6. 加一个任务

不用改菜单。三步：

```c
/* 1. 写 User/task/task_xxx.c，照着 task_example.c 填回调 */
const app_task_t task_xxx =
{
    .name       = "XXX",        /* 标题栏文字 + 线程名，<= 7 字符，必须 ASCII */
    .stack_size = 1024,
    .priority   = 12,
    .setup      = xxx_setup,
    .update     = xxx_update,   /* 可 NULL */
    .on_key     = xxx_on_key,   /* 可 NULL */
    .on_set     = xxx_on_set,   /* 可 NULL */
    .render     = xxx_render,   /* 可传 NULL，内部只调一个 xxx_ui_render */
    .teardown   = xxx_teardown, /* 可 NULL */
    .ui_destroy = xxx_ui_destroy,
};

/* 2. User/task/task.h 末尾加一行 */
extern const app_task_t task_xxx;

/* 3. User/menu/task_runner.c 的 s_task_list[] 里加一行 */
static const app_task_t *const s_task_list[] =
{
    &task_example,
    &task_xxx,          /* ← 顺序即菜单顺序 */
};
```

界面另外写 `User/ui/task_xxx_ui.c` + `task_xxx_ui.h`。

### 任务里怎么用 K4

菜单只截走「长按 K4」，**短按 K4 会正常送到任务的 `on_key()`**，可以自己用：

```c
static void xxx_on_key(key_id_t key)
{
    switch(key)
    {
        case KEY_ID_1:     /* K1 短按 */  break;
        case KEY_ID_4:     /* K4 短按，归任务自己用 */  break;
        case KEY_ID_LONG_1:/* K1 长按 */  break;
        case KEY_ID_DBL_1: /* K1 双击 */  break;
        /* KEY_ID_LONG_4 不会到这里，被菜单拿去退出了 */
        default: return;
    }
}
```

### 三条铁律

1. **`lv_*` 只能出现在 `render` 和 `ui/` 目录里**，逻辑线程绝对不许碰。
2. 任务文件只 include `task.h` 和自己的 `xxx_ui.h`，不许 include `menu.h`。
3. 想控制菜单，只能 `menu_post()`，不许碰 `menu.c` 里的内部函数。

## 7. MSH 命令

串口控制台（UART1，115200）：

| 命令 | 作用 |
| --- | --- |
| `menu` | 打印页面、光标、当前任务、丢事件数、任务清单、堆与 LVGL 内存 |
| `menu list` | 同上 |
| `menu open 0` | 打开第 0 个任务（等同列表页按 K3），序号越界会直接报错 |
| `menu exit` | 回列表页（等同长按 K4） |
| `menu next` / `menu prev` | 移动列表页光标 |

都是投递命令后立刻返回，实际切换在 `ui` 线程里发生。

## 8. 编译与烧录

1. 安装 MounRiver Studio 3（自带 `riscv-none-embed-gcc` 8.2.0）。
2. 导入本目录的 `.project` / `.cproject`。
3. Build。`obj/` 是 IDE 生成的编译目录，不入库。

命令行验证（没有 make 时可用）：

```bash
python .workbuddy/mkbuild.py
```

它会读 `obj/makefile` 里的编译/链接选项，全量编译后链接出 `.workbuddy/_build/chk.elf`。当前基线：

| 项 | 占用 |
| --- | --- |
| FLASH | 165896 B（72.3%） |
| RAM | 62248 B（63.3%） |

## 9. 编码与两个坑

- 源码统一 **UTF-8（无 BOM）**，2026-11-17 由 GBK 全量转换而来。换行符 CRLF/LF 混杂，**不做归一化**（`.gitattributes` 里 `* -text diff`）。
- **`HAL/LCD/lcdfont.h` 的 `Index[2]` 必须写 GBK 字节转义**（如 `"\xD6\xD0"` 表示「中」）。写成中文字面量在 UTF-8 下是 3 字节，会撑爆 2 字节数组并导致查表失效。
- **界面文字必须 ASCII**：LVGL 只编了 `montserrat_14`，没有中文字库。中文只出现在注释和串口打印里。

## 10. 说明

仓库包含第三方组件源码（RT-Thread、LVGL、CH32 外设库），请保留其原有许可文件与源码中的版权声明。

---

*本文档描述的是当前架构。历史 README 里的 `APP/`、`Runtime/`、`Tasks/`、`PC_Player/` 等目录已不存在。*
