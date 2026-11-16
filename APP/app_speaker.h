#ifndef APP_SPEAKER_H
#define APP_SPEAKER_H

#include <stdint.h>

#define APP_SPEAKER_OK          0
#define APP_SPEAKER_EINVAL      (-1)
#define APP_SPEAKER_ESTATE      (-2)
#define APP_SPEAKER_ECLOCK      (-3)
#define APP_SPEAKER_MAX_NOTES   64U

/* 相对音量是DAC交流峰峰值比例，不是声压级百分比或dB。 */
typedef enum
{
    APP_SPEAKER_SQUARE = 0,
    APP_SPEAKER_SINE,
    APP_SPEAKER_TRIANGLE,
    APP_SPEAKER_SAWTOOTH
} app_speaker_wave_t;

typedef enum
{
    APP_SPEAKER_STOPPED = 0,
    APP_SPEAKER_TONE,
    APP_SPEAKER_MELODY,
    APP_SPEAKER_PAUSED,
    APP_SPEAKER_PCM
} app_speaker_mode_t;

typedef struct
{
    uint16_t frequency_hz; /* 50～5000Hz；0表示休止符。 */
    uint16_t beats_x100;   /* 四分音符=100，八分音符=50，附点四分音符=150。 */
} app_speaker_note_t;

typedef struct
{
    app_speaker_mode_t mode;
    app_speaker_wave_t wave;
    uint8_t volume;             /* 0～100，相对DAC幅度；0为静音。 */
    uint8_t square_duty;        /* 5～95%，仅方波有效，32点表会产生量化。 */
    uint8_t gate_percent;       /* 音符时长中实际发声的比例，20～100%。 */
    uint8_t loop;
    uint8_t output_active;      /* 当前定时器/DMA是否在产生周期信号。 */
    uint16_t bpm;               /* 每分钟四分音符拍数，40～240。 */
    uint16_t note_index;        /* 从0开始的当前音符下标。 */
    uint16_t note_count;
    uint16_t frequency_hz;      /* 当前请求频率，休止符为0。 */
    uint32_t actual_millihz;    /* 按定时器参数算出的实际基频，单位0.001Hz。 */
    int last_error;
} app_speaker_state_t;

/* PA4/DAC通道1 + TIM8_TRGO + DMA2通道3，初始停止，默认方波/50%相对音量。
 * 返回0或负错误码。仅在线程中调用，模块内部使用互斥锁。
 * init在所有其他接口前调用一次；重复init不打断当前播放。
 * 本模块独占这些资源，不能与其他DAC/TIM8/DMA2_CH3程序同时使用。
 */
int app_speaker_init(void);

/* 保留旧接口：以1000Hz持续发声，stop终止。 */
void app_speaker_start(void);
void app_speaker_stop(void);

/* 持续播放指定频率；替换正在播放的旋律。返回0或负错误码。 */
int app_speaker_play_note(uint16_t frequency_hz);

/* 播放单音：频率50～5000Hz；duration_ms=0持续，否则1～60000ms后停止。
 * 此函数立即返回；自动停止依赖process约每10ms执行一次。
 */
int app_speaker_tone(uint16_t frequency_hz, uint32_t duration_ms);

/* 下列配置可在线程中实时修改；不会重置旋律节拍或退出暂停。
 * volume=0只静音，时间继续走；pause才冻结播放进度。
 * 100%对应本工程约400mV的DAC理论峰峰值，并非DAC全幅或实测声压。
 */
int app_speaker_set_volume(uint8_t volume);
int app_speaker_set_waveform(app_speaker_wave_t wave);
int app_speaker_set_square_duty(uint8_t percent);
int app_speaker_set_gate(uint8_t percent);

/* 复制1～64个音符到内部固定数组，调用后原数组可失效。
 * BPM为40～240；loop只能为0或1；每个音符beats_x100为25～1600。
 * 单音符时长按60000*beats_x100/(BPM*100)换算，不在函数内延时。
 * 返回0或负错误码；参数错误不打断原播放。
 */
int app_speaker_play(const app_speaker_note_t *notes, uint16_t count, uint16_t bpm, uint8_t loop);
int app_speaker_pause(void);
int app_speaker_resume(void);

/* 单个固定线程/main循环约每10ms调用，推进定时提示音、旋律与音符间隔。
 * 硬件持续输出不依赖它逐点刷新；节拍切换和自动停止依赖它及时调用。
 * 不在中断中调用；LVGL回调应提交命令，由工作线程调用这些接口。
 */
void app_speaker_process(void);
int app_speaker_get_state(app_speaker_state_t *state);

/* 《两只老虎》器乐演示，120BPM；立即返回，仍需process。 */
int app_speaker_play_demo(uint8_t loop);

/* 16kHz/16位小端/单声道流式PCM。固定双缓冲2048字节RAM；不写Flash。
 * begin清除旧播放，volume=0..100，DAC幅度沿用现有400mVpp上限。
 * write返回接受的字节数，0=缓冲满，负数=错误；长度须为偶数。
 * end标记输入结束，补静音后异步播完；get_state查询结果。
 * 旧pause/resume不支持PCM；stop可随时取消。所有API在线程中调用。
 */
typedef struct
{
    int result; /* 0进行中，1播完；-4欠载，-5 DMA错误，-6取消。 */
    uint8_t running, input_closed;
    uint32_t accepted_samples, actual_sample_rate;
} app_speaker_pcm_state_t;
int app_speaker_pcm_begin(uint32_t sample_rate, uint8_t volume);
int app_speaker_pcm_write(const uint8_t *data, uint16_t length);
int app_speaker_pcm_end(void);
int app_speaker_pcm_get_state(app_speaker_pcm_state_t *out);

#endif
