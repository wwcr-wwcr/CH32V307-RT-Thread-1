/******************************************************************************
 * 文件名   : task_example.c
 * 说明     : EXAMPLE —— 传感器 + 声音的综合演示
 *
 * 页面内容（UI 在 ui/task_example_ui.c）：
 *     1. 屏幕：麦克风（MIC）和 VEML7700（LUX）的实时曲线，横轴是时间
 *     2. 数码管：从 9999 开始每拍减 1，减到 0 回到 9999
 *     3. 扬声器：循环播放《上山打老虎》（两只老虎的调子）
 *     4. 蜂鸣器：节拍器，1 秒一拍，每 4 拍一个重音
 *
 * 驱动全部来自 HAL，本文件不碰任何寄存器、也不直接调 lv_：
 *     HAL/MIC         mic_init() / mic_amp()          采麦克风峰峰值
 *     HAL/VEML7700    veml7700_init() / _lux_get()    读照度
 *     HAL/DigitalTube ch422_init() / _show_number()   4 位数码管
 *     HAL/AUDIO       audio_music_start() / _pump()   扬声器放歌（DAC+DMA）
 *     HAL/BEEP        beep_freq_set() / beep_stop()   蜂鸣器节拍器（TIM2 PWM）
 *
 * 线程模型（menu.c 那套）：
 *     update() 跑在任务线程，10ms 一次 —— 采样、打拍子、喂音频都在这
 *     render() 跑在 UI 线程 —— 只把快照里的数丢给 UI，不在这里做业务
 ******************************************************************************/

#include "task.h"
#include "ui/task_example_ui.h"
#include "rtthread.h"
#include "rthw.h"
#include "veml7700.h"
#include "MIC/mic.h"
#include "AUDIO/audio.h"
#include "BEEP/beep.h"
#include "DigitalTube/ch422.h"

/**************************************************************************************************
 *                                        参数
 **************************************************************************************************/
#define EX_TICK_MS          10      /* update 的节拍（和 menu.c 的 UI_PERIOD_MS 一致） */
#define EX_SAMPLE_DIV       10      /* 10 × 10ms = 100ms 采一次 -> 10Hz
                                     * 曲线 64 点 × 100ms = 6.4 秒窗口，横轴刻度按这个算 */
#define EX_BEAT_DIV         100     /* 100 × 10ms = 1 秒一拍（蜂鸣器 + 数码管倒数） */
#define EX_MIC_N            512     /* 每次采 512 点算峰峰值，约 1~2ms */
#define EX_BEEP_HOLD        4       /* 每拍"哒"一声保持几个 tick（4 × 10ms = 40ms） */
#define EX_COUNT_START      9999    /* 数码管起始值 */

/* 触摸按钮编号，必须和 ui/task_example_ui.c 里 ui_button(..., id) 对上 */
#define UI_ID_PAUSE         1
#define UI_ID_MUSIC         2
#define UI_ID_BEAT          3

/**************************************************************************************************
 *                                        数据
 **************************************************************************************************/
typedef struct
{
    uint16_t mic;               /* 麦克风峰峰值 0~4095 */
    uint16_t lux;               /* 照度 lux */
    uint16_t count;             /* 数码管当前值 */
    uint8_t  seq;               /* 采样序号，UI 靠它判断"有没有新点" */
    uint8_t  run;               /* 1 = 采样 / 打拍在跑 */
    uint8_t  music;             /* 1 = 扬声器在放歌 */
    uint8_t  beat;              /* 1 = 节拍器开着 */
    uint8_t  err;               /* bit0 VEML 失败，bit1 MIC 失败，bit2 扬声器失败 */
} app_t;

static app_t    app;            /* 任务线程写 */
static app_t    s_view;         /* UI 线程读（view_push 里整份拷过去） */

static uint16_t s_div_s;        /* 采样分频计数 */
static uint16_t s_div_b;        /* 拍子分频计数 */
static uint8_t  s_beep_left;    /* 蜂鸣器还要响几个 tick，0 = 已经停了 */
static uint8_t  s_beat_no;      /* 第几拍，用来做每 4 拍的重音 */

/**************************************************************************************************
 *                                    曲谱：《上山打老虎》
 *
 * 就是《两只老虎》的调子（C 大调，四分音符 400ms）：
 *     1 2 3 1 | 1 2 3 1 | 3 4 5 | 3 4 5 | 5 6 5 4 3 1 | 5 6 5 4 3 1 | 1 5 1 | 1 5 1
 * 词：一二三四五 / 上山打老虎 / 老虎没打着 / 打到小松鼠
 *
 * 注意：hz = 0 是休止符；表必须常驻内存（这里放 Flash 的 const）。
 **************************************************************************************************/
#define N(hz, ms)       { (uint16_t)(hz), (uint16_t)(ms) }

static const audio_note_t g_song[] =
{
    N(262, 400), N(294, 400), N(330, 400), N(262, 400),     /* 一二三四五 */
    N(262, 400), N(294, 400), N(330, 400), N(262, 400),     /* 上山打老虎 */
    N(330, 400), N(349, 400), N(392, 800),                  /* 老虎没打着 */
    N(330, 400), N(349, 400), N(392, 800),                  /* 打到小松鼠 */
    N(392, 200), N(440, 200), N(392, 200), N(349, 200), N(330, 400), N(262, 400),
    N(392, 200), N(440, 200), N(392, 200), N(349, 200), N(330, 400), N(262, 400),
    N(262, 400), N(196, 400), N(262, 800),                  /* 松鼠有几只 */
    N(262, 400), N(196, 400), N(262, 800),                  /* 让我数一数 */
};

#define SONG_NUM        ((uint16_t)(sizeof(g_song) / sizeof(g_song[0])))

/**************************************************************************************************
 *                                        小工具
 **************************************************************************************************/
static void view_push(void)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    s_view = app;
    rt_hw_interrupt_enable(level);
}                                                               //把更新推给

static int music_start(void)
{
    return audio_music_start(g_song, SONG_NUM);
}

/**
 * @brief  开/关节拍器（关的时候顺手把还没响完的那一下掐掉）
 */
static void beat_set(uint8_t on)
{
    app.beat = (on != 0u) ? 1u : 0u;

    if(app.beat == 0u)
    {
        s_beep_left = 0;
        beep_stop();
    }
}

/**
 * @brief  开/关扬声器
 */
static void music_set(uint8_t on)
{
    if(on != 0u)
    {
        app.music = (music_start() == 0) ? 1u : 0u;
    }
    else
    {
        app.music = 0;
        audio_music_stop();
    }
}

/**************************************************************************************************
 *                                        事件
 **************************************************************************************************/
typedef enum
{
    EV_ENTER = 0,           /* 进页：初始化硬件 + 复位状态 */
    EV_TICK,                /* 周期：update 每 10ms 一次 */
    EV_KEY,                 /* 按键：a = key_id_t */
    EV_TOUCH,               /* 触摸：a = 控件 id */
    EV_LEAVE                /* 退页：恢复硬件 */
} ev_t;


static void app_event(ev_t ev, int32_t a, int32_t b)
{
    switch(ev)
    {
        /* ---------------- 进页 ---------------- */
        case EV_ENTER:
        {
            app.mic   = 0;
            app.lux   = 0;
            app.count = EX_COUNT_START;
            app.seq   = 0;
            app.run   = 1;
            app.music = 0;
            app.beat  = 0;
            app.err   = 0;

            s_div_s     = 0;
            s_div_b     = 0;
            s_beep_left = 0;
            s_beat_no   = 0;

            /* 传感器 */
            if(veml7700_init() != 0)
            {
                app.err |= 0x01;
                rt_kprintf("example: veml7700 init failed\r\n");
            }
            veml7700_set_it(VEML7700_IT_50MS);      /* 20Hz 更新，配 10Hz 采样足够 */

            if(mic_init(MIC_DEFAULT_CH) < 0)
            {
                app.err |= 0x02;
                rt_kprintf("example: mic init failed\r\n");
            }

            /* 数码管：上电先摆出 9999 */
            ch422_init();
            ch422_show_number(app.count);

            /* 蜂鸣器：初始化后是静音的 */
            beep_init();
            beep_stop();

            /* 扬声器：音量 60%，开始放歌 */
            audio_init();
            audio_volume(60);
            if(music_start() != 0)
            {
                app.err |= 0x04;
                rt_kprintf("example: speaker init failed\r\n");
            }
            else
            {
                app.music = 1;
            }

            beat_set(1);

            view_push();                    /* 先把第一帧推给 UI */
            return;
        }
        /* ---------------- 周期 ---------------- */
        case EV_TICK:
        {
            /* 1. 音频续命：每 10ms 补一次样本，放完自动从头再来 */
            audio_music_pump();

            if((app.music != 0u) && (audio_music_playing() == 0u))
            {
                if(music_start() != 0)
                {
                    app.music = 0;          /* 起不来了就别死循环重试 */
                }
            }

            /* 2. 蜂鸣器"哒"一下的收尾（不阻塞，所以放 tick 里数） */
            if((s_beep_left > 0u) && (--s_beep_left == 0u))
            {
                beep_stop();
            }

            if(app.run == 0u)
            {
                return;                     /* 暂停：不采样、不打拍、不刷 UI */
            }

            /* 3. 打拍：数码管 -1 + 蜂鸣器响一下 */
            if(++s_div_b >= EX_BEAT_DIV)
            {
                s_div_b = 0;

                app.count = (app.count == 0u) ? EX_COUNT_START
                                              : (uint16_t)(app.count - 1u);
                ch422_show_number(app.count);

                if(app.beat != 0u)
                {
                    s_beat_no++;

                    /* 每 4 拍一个重音，听得出小节 */
                    beep_freq_set(((s_beat_no & 0x03u) == 1u) ? 3000u : 2000u);
                    s_beep_left = EX_BEEP_HOLD;
                }
            }

            /* 4. 采样：麦克风 + 照度，推给曲线 */
            if(++s_div_s >= EX_SAMPLE_DIV)
            {
                s_div_s = 0;

                if((app.err & 0x02u) == 0u)
                {
                    app.mic = mic_amp(EX_MIC_N);
                }
                if((app.err & 0x01u) == 0u)
                {
                    app.lux = veml7700_lux_get();
                }

                app.seq++;
            }
            break;
        }
        /* ---------------- 按键 ---------------- */
        case EV_KEY:
        {
            switch((key_id_t)a)
            {
                case KEY_ID_1:                      /* K1 运行 / 暂停 */
                    app.run = (uint8_t)!app.run;
                    if(app.run == 0u)
                    {
                        s_beep_left = 0;
                        beep_stop();
                    }
                    break;

                case KEY_ID_2:                      /* K2 音乐 */
                    music_set((uint8_t)!app.music);
                    break;

                case KEY_ID_3:                      /* K3 节拍器 */
                    beat_set((uint8_t)!app.beat);
                    break;

                default:
                    return;                         /* K4 归 menu 管，别在这处理 */
            }
            break;
        }
        /* ---------------- 触摸 ---------------- */
        case EV_TOUCH:
        {
            switch((uint8_t)a)
            {
                case UI_ID_PAUSE:
                    app.run = (uint8_t)!app.run;
                    if(app.run == 0u)
                    {
                        s_beep_left = 0;
                        beep_stop();
                    }
                    break;

                case UI_ID_MUSIC:
                    music_set((uint8_t)!app.music);
                    break;

                case UI_ID_BEAT:
                    beat_set((uint8_t)!app.beat);
                    break;

                default:
                    return;
            }
            break;
        }
        /* ---------------- 退页 ---------------- */
        case EV_LEAVE:
        {
            app.run   = 0;
            app.music = 0;
            app.beat  = 0;
            app.mic   = 0;
            app.lux   = 0;

            audio_music_stop();
            audio_off();                    /* 关 DAC，顺便把功放那路拉回静音 */
            beep_stop();
            ch422_clear();
            return;
        }
        default:
            return;
    }

    view_push();
}

/**************************************************************************************************
 *                                        任务接口
 **************************************************************************************************/
static void ex_setup(void)                  { app_event(EV_ENTER, 0, 0); }
static void ex_update(void)                 { app_event(EV_TICK,  0, 0); }
static void ex_on_key(key_id_t key)         { app_event(EV_KEY,  (int32_t)key, 0); }
static void ex_on_set(uint8_t id, int32_t v){ app_event(EV_TOUCH, (int32_t)id, v); }
static void ex_teardown(void)               { app_event(EV_LEAVE, 0, 0); }

/**
 * @brief  UI 线程：每 10ms 一次
 * @note   只拷快照 + 调 UI，不碰驱动
 */
static void ex_render(void)
{
    app_t     v;
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    v = s_view;
    rt_hw_interrupt_enable(level);

    example_ui_render(v.mic, v.lux, v.count, v.run, v.music, v.beat, v.seq);
}

const app_task_t task_example =
{
    .name       = "EXAMPLE",        /* 菜单显示名 + 线程名，<= 7 字符 */
    .stack_size = 1024,             /* 不打印，1024 够 mic/音频那几层调用 */
    .priority   = 12,               /* key=8, ui=9，任务一般 12 */

    .setup      = ex_setup,
    .update     = ex_update,
    .on_key     = ex_on_key,
    .on_set     = ex_on_set,
    .render     = ex_render,
    .teardown   = ex_teardown,
    .ui_destroy = example_ui_destroy,
};
