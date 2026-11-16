#include "task01.h"
#include "app_segment.h"
#include "app_key.h"
#include "app_rgb.h"
#include "app_buzzer.h"
#include "../Runtime/task_manager.h"

#define TASK01_START_MINUTES       90U  //开始的时间分钟
#define TASK01_MAX_MINUTES         99U  //最大的分钟数
#define TASK01_SECONDS_PER_MINUTE  60U  //每分钟多少秒
#define TASK01_MS_PER_SECOND       1000U //一秒有多少毫秒
#define TASK01_SPEED_NORMAL        1U   //1x运行计时
#define TASK01_SPEED_DOUBLE        2U   //2x运行计时


#define TASK01_START_SECONDS \
    (TASK01_START_MINUTES * TASK01_SECONDS_PER_MINUTE)  //开始时的秒数 

/* 99 分 59 秒，也就是 5999 秒。 */
#define TASK01_MAX_SECONDS \
    ((TASK01_MAX_MINUTES + 1U) * TASK01_SECONDS_PER_MINUTE - 1U)  //开始时的最大秒数

#define TASK01_TICKS_PER_SECOND \
    (TASK01_MS_PER_SECOND / TASK01_PERIOD_MS)  


#define TASK01_ALARM_MINUTES  89U  //闹钟的定时的分钟数
#define TASK01_ALARM_SECONDS  55U  //闹钟的定时的秒数
#define TASK01_ALARM_TARGET     \
    (TASK01_ALARM_MINUTES * TASK01_SECONDS_PER_MINUTE + TASK01_ALARM_SECONDS)
//闹钟的定时的总秒数
#define TASK01_ALARM_DURATION_MS 2000U
#define TASK01_ALARM_TICKS \
    (TASK01_ALARM_DURATION_MS / TASK01_PERIOD_MS)
#define TASK01_ALARM_BUZZER_HZ   2000U

typedef enum
{
    EV_ENTER,
    EV_TICK,
    EV_KEY,
    EV_TOUCH,
    EV_LEAVE
} task01_event_t;

static uint32_t seconds;
static uint16_t tick_count;  /* 已经数了多少次 EV_TICK */
static uint8_t speed;        /* 当前的速度 */
static uint8_t blink_color;  /* 当前RGB灯的颜色 */

static uint16_t alarm_ticks_left; /* 0 表示没在响，大于 0 表示还要响几轮 */
static uint32_t alarm_sequence;   /* 每次闹钟触发后加一，供 GUI 提示。 */
static uint8_t page_visible;      /* 上次检查时 TASK01 页面是否可见。 */

static uint32_t alarm_target_seconds; /* 用户设定的闹钟时间 */

typedef enum
{
    TIMER_PAUSED,  /* 暂停 */
    TIMER_DOWN,    /* 倒计时 */
    TIMER_UP      /* 正计时 */
} timer_mode_t;
static timer_mode_t mode;

static void refresh_rgb(void)
{
    if (!task_is_attached(TASK01_ID) || task_foreground_active()) return;
    if(alarm_ticks_left > 0U)
        app_rgb_set(APP_RGB_RED);
    else if (mode == TIMER_PAUSED)
        app_rgb_set(APP_RGB_OFF);
    else
        app_rgb_set(blink_color);
}

static void show_time(void)
{
    if (!task_is_attached(TASK01_ID) || task_foreground_active()) return;
    app_segment_display((uint8_t)(seconds / 60U),(uint8_t)(seconds % 60U));
}

static int task01_command(const task_cmd_t *cmd)
{
    if (cmd->code != TASK01_CMD_SET_ALARM ||cmd->value < 0 ||cmd->value > (int32_t)TASK01_MAX_SECONDS)
    {
        return -1;
    }

    alarm_target_seconds = (uint32_t)cmd->value;
    return 0;
}


static void app_event(task01_event_t ev, uint32_t a,uint32_t b)
{
    (void)a;
    (void)b;

    switch (ev) 
    {
        case EV_ENTER:
            seconds = TASK01_START_SECONDS;
            mode = TIMER_PAUSED;
            tick_count = 0;
            speed = TASK01_SPEED_NORMAL;
            alarm_ticks_left = 0U;
            alarm_sequence = 0U;
            alarm_target_seconds = TASK01_ALARM_TARGET; 
            page_visible = 1U;
            app_buzzer_init();
            app_buzzer_stop();
            app_segment_init();
            app_key_init();
            app_rgb_init();
            app_rgb_set(APP_RGB_OFF);
            show_time();

            break;

        case EV_TICK:
        {
            uint8_t visible = task_is_attached(TASK01_ID) && !task_foreground_active();

            if (!page_visible && visible)
            {
                app_segment_init();
                app_key_init();
                app_rgb_init();
                show_time();
                refresh_rgb();
            }
            page_visible = visible;

            if (alarm_ticks_left > 0U)
            {
                alarm_ticks_left--;
                if (alarm_ticks_left == 0U)
                {
                    if (visible) app_buzzer_stop();
                    refresh_rgb();
                }
            }

            if (mode == TIMER_PAUSED)   
            break;

            tick_count += speed;
            if (tick_count < TASK01_TICKS_PER_SECOND)
            break;

            tick_count -= TASK01_TICKS_PER_SECOND;

            if (mode == TIMER_DOWN && seconds > 0U)
                seconds--;
            else if (mode == TIMER_UP && seconds < TASK01_MAX_SECONDS)
                seconds++;
            else
            {
                mode = TIMER_PAUSED;
                refresh_rgb();
                break;
            }

            if ((mode == TIMER_DOWN && seconds == 0U) ||(mode == TIMER_UP && seconds == TASK01_MAX_SECONDS))
                mode = TIMER_PAUSED;

            if (blink_color == APP_RGB_BLUE)
                blink_color = APP_RGB_GREEN;
            else
                blink_color = APP_RGB_BLUE;  
                
            if (seconds == alarm_target_seconds && alarm_ticks_left == 0U)
            {
                alarm_ticks_left = TASK01_ALARM_TICKS;
                alarm_sequence++;
                if (visible) app_buzzer_start(TASK01_ALARM_BUZZER_HZ);
            }

            refresh_rgb();      

            break;
        }

        case EV_LEAVE:
            if (!task_foreground_active())
            {
                app_rgb_set(APP_RGB_OFF);
                app_buzzer_stop();
            }
            break;

        case EV_KEY:
            if      (a == APP_KEY1)  mode = TIMER_DOWN;
            else if (a == APP_KEY2)  mode = TIMER_UP;
            else if (a == APP_KEY4)  mode = TIMER_PAUSED;

            if (a == APP_KEY3)
            {
                if (speed == TASK01_SPEED_NORMAL)
                    speed = TASK01_SPEED_DOUBLE;
                else
                    speed = TASK01_SPEED_NORMAL;

                break;  
            }

            tick_count = 0;
            if (mode != TIMER_PAUSED)
            blink_color = APP_RGB_BLUE;
            refresh_rgb();

            break;



        default:
            break;
    }
}

static int task01_init(void)
{
    app_event(EV_ENTER,0,0);

    return 0;
}

static void task01_step(uint32_t now_ms, task_sample_t *sample)
{
    app_event(EV_TICK, now_ms, 0);

    uint16_t key_events = 0U;

    /* 页面隐藏时，按键由当前前台 TASK 使用。 */
    if (task_is_attached(TASK01_ID) && !task_foreground_active())
        key_events = app_key_scan();

    if (key_events & APP_KEY_CLICK(APP_KEY4))
    app_event(EV_KEY, APP_KEY4, 0);
    
    else if (key_events & APP_KEY_CLICK(APP_KEY1))
    app_event(EV_KEY, APP_KEY1, 0);
    
    else if (key_events & APP_KEY_CLICK(APP_KEY2))
    app_event(EV_KEY, APP_KEY2, 0);

    else if (key_events & APP_KEY_CLICK(APP_KEY3))
    app_event(EV_KEY, APP_KEY3, 0);
    


    sample->value = (int32_t)seconds;
    sample->alarm_sequence = alarm_sequence;
    sample->alarm_target_seconds = alarm_target_seconds;
}

static void task01_stop(void)
{   
    app_event(EV_LEAVE,0,0);
}

const task_ops_t task01_ops =
{
    .init = task01_init,
    .command = task01_command, 
    .step = task01_step,
    .stop = task01_stop,
    .detach = task01_stop
};
