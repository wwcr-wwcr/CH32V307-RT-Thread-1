#include "app_speaker.h"
#include "ch32v30x.h"
#include <rtthread.h>
#include <string.h>
#include <rthw.h>
#ifndef APP_SPEAKER_HOST_TEST
#include "ch32v30x_it.h"
#endif

#define SPEAKER_POINTS       32U
#define SPEAKER_CENTER       2048U
#define SPEAKER_VREF_MV      3300U
#define SPEAKER_MAX_VPP_MV   400U

/* 正弦表归一化到±32767，不在实时控制路径调用sin函数。 */
static const int16_t sine_table[SPEAKER_POINTS] =
{
    0,6393,12539,18204,23170,27245,30273,32137,
    32767,32137,30273,27245,23170,18204,12539,6393,
    0,-6393,-12539,-18204,-23170,-27245,-30273,-32137,
    -32767,-32137,-30273,-27245,-23170,-18204,-12539,-6393
};

static struct rt_mutex speaker_mutex;
static uint8_t mutex_ready;
static uint8_t initialized;
static uint16_t dma_wave[SPEAKER_POINTS];
static app_speaker_note_t score[APP_SPEAKER_MAX_NOTES];
static app_speaker_state_t state;
static app_speaker_mode_t paused_mode;
static rt_tick_t stage_started;
static rt_tick_t stage_ticks;
static rt_tick_t paused_elapsed;
static rt_tick_t song_ticks;

#define PCM_HALF_SAMPLES 512U
/* 两个半区各32ms。ready=1才允许DMA读取，生产者仅写ready=0的半区。 */
static uint16_t pcm_buffer[PCM_HALF_SAMPLES * 2U] __attribute__((aligned(4)));
static volatile uint8_t pcm_ready[2], pcm_running, pcm_closed, pcm_started;
static volatile uint8_t pcm_terminal_half;
static volatile int pcm_result;
static uint8_t pcm_fill_half;
static uint16_t pcm_fill_offset;
static uint32_t pcm_accepted, pcm_actual_rate;

/* RT-Thread互斥锁只串行化本模块，不代表其他DAC程序也会遵守此锁。 */
static void speaker_lock_init(void)
{
    rt_enter_critical();
    if (!mutex_ready)
    {
        rt_mutex_init(&speaker_mutex, "speaker", RT_IPC_FLAG_PRIO);
        mutex_ready = 1U;
    }
    rt_exit_critical();
}

static int speaker_lock(void)
{
    if (!initialized) return APP_SPEAKER_ESTATE;
    return rt_mutex_take(&speaker_mutex, RT_WAITING_FOREVER) == RT_EOK ? APP_SPEAKER_OK : APP_SPEAKER_ESTATE;
}

static void speaker_config_dac(uint32_t trigger)
{
    DAC_InitTypeDef dac;
    DAC_StructInit(&dac);
    dac.DAC_Trigger = trigger;
    dac.DAC_WaveGeneration = DAC_WaveGeneration_None;
    dac.DAC_OutputBuffer = DAC_OutputBuffer_Disable;
    DAC_Init(DAC_Channel_1, &dac);
    DAC_Cmd(DAC_Channel_1, ENABLE);
}

/* 改写波形表前必须停止触发及DMA；停止后保持中点以减小直流突变。 */
static void speaker_hardware_stop(void)
{
    NVIC_DisableIRQ(DMA2_Channel3_IRQn);
    DMA_ITConfig(DMA2_Channel3, DMA_IT_HT | DMA_IT_TC | DMA_IT_TE, DISABLE);
    DMA_ClearFlag(DMA2_FLAG_GL3);
    pcm_running = 0;
    TIM_Cmd(TIM8, DISABLE);
    DAC_DMACmd(DAC_Channel_1, DISABLE);
    DMA_Cmd(DMA2_Channel3, DISABLE);
    speaker_config_dac(DAC_Trigger_None);
    DAC_SetChannel1Data(DAC_Align_12b_R, SPEAKER_CENTER);
    state.output_active = 0U;
    state.actual_millihz = 0U;
}

static void speaker_stop_locked(void)
{
    if (state.mode == APP_SPEAKER_PCM && pcm_result == 0) pcm_result = -6;
    speaker_hardware_stop();
    state.mode = APP_SPEAKER_STOPPED;
    state.frequency_hz = 0U;
    state.note_count = 0U;
    state.note_index = 0U;
    state.loop = 0U;
    stage_ticks = 0;
    paused_elapsed = 0;
    song_ticks = 0;
}

/* 生成一个DAC码：幅度固定在中点附近，方波占空比由点数离散表达。 */
static uint16_t speaker_wave_code(uint16_t index)
{
    int32_t shape;
    int32_t sample;
    uint32_t peak;
    uint32_t high_points;

    peak = ((uint32_t)SPEAKER_MAX_VPP_MV * state.volume * 4095U + SPEAKER_VREF_MV * 100U) / (SPEAKER_VREF_MV * 200U);
    switch (state.wave)
    {
        case APP_SPEAKER_SINE:
            shape = sine_table[index];
            break;
        case APP_SPEAKER_TRIANGLE:
            shape = index < 16U ? -32767L + 65534L * (int32_t)index / 16L : 32767L - 65534L * ((int32_t)index - 16L) / 16L;
            break;
        case APP_SPEAKER_SAWTOOTH:
            shape = -32767L + 65534L * index / (SPEAKER_POINTS - 1U);
            break;
        default:
            high_points = (SPEAKER_POINTS * state.square_duty + 50U) / 100U;
            shape = index < high_points ? 32767L : -32767L;
            break;
    }
    sample = (int32_t)SPEAKER_CENTER + (int32_t)peak * shape / 32767L;
    return (uint16_t)sample;
}

/* TIM8只产生内部TRGO，不配置TIM8输出引脚，不需要开启PWM总输出。 */
static int speaker_output(uint16_t frequency_hz)
{
    RCC_ClocksTypeDef clocks;
    TIM_TimeBaseInitTypeDef timer;
    DMA_InitTypeDef dma;
    uint32_t timer_hz;
    uint32_t sample_hz;
    uint32_t divider;
    uint32_t counts;
    uint16_t first;
    uint16_t i;

    speaker_hardware_stop();
    if ((frequency_hz == 0U) || (state.volume == 0U)) return APP_SPEAKER_OK;

    RCC_GetClocksFreq(&clocks);
    timer_hz = clocks.PCLK2_Frequency;
    if (clocks.PCLK2_Frequency != clocks.HCLK_Frequency) timer_hz *= 2U;
    sample_hz = (uint32_t)frequency_hz * SPEAKER_POINTS;
    if (timer_hz == 0U) return APP_SPEAKER_ECLOCK;

    /* 先保证ARR装得下，再按实际时钟四舍五入，避免固定PSC导致音准偏差。 */
    divider = (uint32_t)(((uint64_t)timer_hz + (uint64_t)sample_hz * 65536U - 1U) / ((uint64_t)sample_hz * 65536U));
    if ((divider == 0U) || (divider > 65536U)) return APP_SPEAKER_ECLOCK;
    counts = (uint32_t)(((uint64_t)timer_hz + (uint64_t)divider * sample_hz / 2U) / ((uint64_t)divider * sample_hz));
    if ((counts < 2U) || (counts > 65536U)) return APP_SPEAKER_ECLOCK;

    first = speaker_wave_code(0U);
    /* 第0点先预装，DMA从第1点搬起，避免循环首尾多出一个重复点。 */
    for (i = 0; i < SPEAKER_POINTS; i++) dma_wave[i] = speaker_wave_code((uint16_t)((i + 1U) % SPEAKER_POINTS));

    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = (uint16_t)(divider - 1U);
    timer.TIM_Period = (uint16_t)(counts - 1U);
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_RepetitionCounter = 0U;
    TIM_TimeBaseInit(TIM8, &timer);
    TIM_GenerateEvent(TIM8, TIM_EventSource_Update);
    TIM_ClearFlag(TIM8, TIM_FLAG_Update);
    TIM_SelectOutputTrigger(TIM8, TIM_TRGOSource_Update);

    DMA_DeInit(DMA2_Channel3);
    DMA_StructInit(&dma);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->R12BDHR1;
    dma.DMA_MemoryBaseAddr = (uint32_t)dma_wave;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = SPEAKER_POINTS;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel3, &dma);
    DMA_ClearFlag(DMA2_FLAG_GL3);

    speaker_config_dac(DAC_Trigger_T8_TRGO);
    DAC_SetChannel1Data(DAC_Align_12b_R, first);
    TIM_SetCounter(TIM8, 0U);
    DMA_Cmd(DMA2_Channel3, ENABLE);
    DAC_DMACmd(DAC_Channel_1, ENABLE);
    TIM_Cmd(TIM8, ENABLE);

    state.actual_millihz = (uint32_t)((uint64_t)timer_hz * 1000U / ((uint64_t)divider * counts * SPEAKER_POINTS));
    state.output_active = 1U;
    return APP_SPEAKER_OK;
}

static rt_tick_t speaker_note_ticks(uint16_t index)
{
    uint32_t denominator = (uint32_t)state.bpm * 100U;
    uint32_t ms = (60000UL * score[index].beats_x100 + denominator / 2U) / denominator;
    return rt_tick_from_millisecond((rt_int32_t)ms);
}

static int speaker_gate_open(rt_tick_t now)
{
    if (state.mode == APP_SPEAKER_TONE) return 1;
    if (state.mode != APP_SPEAKER_MELODY) return 0;
    return (rt_tick_t)(now - stage_started) < (rt_tick_t)((uint64_t)stage_ticks * state.gate_percent / 100U);
}

static void speaker_fail(int error)
{
    speaker_stop_locked();
    state.last_error = error;
}

/* 迟到时跳过已过期的音符，不把每次调度误差累加到歌曲节拍。 */
static void speaker_update(rt_tick_t now)
{
    int changed = 0;
    int result;
    rt_tick_t elapsed;

    if (state.mode == APP_SPEAKER_TONE)
    {
        if ((stage_ticks != 0U) && ((rt_tick_t)(now - stage_started) >= stage_ticks)) speaker_stop_locked();
        return;
    }
    if (state.mode != APP_SPEAKER_MELODY) return;

    elapsed = now - stage_started;
    if (state.loop && (song_ticks != 0U) && (elapsed >= song_ticks))
    {
        stage_started += (elapsed / song_ticks) * song_ticks;
        elapsed = now - stage_started;
        changed = 1;
    }
    while (elapsed >= stage_ticks)
    {
        stage_started += stage_ticks;
        state.note_index++;
        if (state.note_index >= state.note_count)
        {
            if (!state.loop) { speaker_stop_locked(); return; }
            state.note_index = 0U;
        }
        state.frequency_hz = score[state.note_index].frequency_hz;
        stage_ticks = speaker_note_ticks(state.note_index);
        elapsed = now - stage_started;
        changed = 1;
    }
    if (changed)
    {
        result = speaker_output(speaker_gate_open(now) ? state.frequency_hz : 0U);
        if (result != APP_SPEAKER_OK) speaker_fail(result);
    }
    else if (state.output_active && !speaker_gate_open(now)) speaker_hardware_stop();
}

int app_speaker_init(void)
{
    GPIO_InitTypeDef gpio;
    speaker_lock_init();
    if (rt_mutex_take(&speaker_mutex, RT_WAITING_FOREVER) != RT_EOK) return APP_SPEAKER_ESTATE;
    if (!initialized)
    {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM8, ENABLE);
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
        RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
        GPIO_StructInit(&gpio);
        gpio.GPIO_Pin = GPIO_Pin_4;
        gpio.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_Init(GPIOA, &gpio);
        TIM_DeInit(TIM8);
        DMA_DeInit(DMA2_Channel3);
        memset(&state, 0, sizeof(state));
        state.wave = APP_SPEAKER_SQUARE;
        state.volume = 50U;
        state.square_duty = 50U;
        state.gate_percent = 90U;
        state.bpm = 120U;
        initialized = 1U;
        speaker_stop_locked();
    }
    rt_mutex_release(&speaker_mutex);
    return APP_SPEAKER_OK;
}

int app_speaker_tone(uint16_t frequency_hz, uint32_t duration_ms)
{
    int result;
    if ((frequency_hz < 50U) || (frequency_hz > 5000U) || (duration_ms > 60000U)) return APP_SPEAKER_EINVAL;
    if (speaker_lock() != APP_SPEAKER_OK) return APP_SPEAKER_ESTATE;
    speaker_stop_locked();
    state.mode = APP_SPEAKER_TONE;
    state.frequency_hz = frequency_hz;
    state.last_error = 0;
    stage_started = rt_tick_get();
    stage_ticks = duration_ms == 0U ? 0U : rt_tick_from_millisecond((rt_int32_t)duration_ms);
    result = speaker_output(frequency_hz);
    if (result != APP_SPEAKER_OK) speaker_fail(result);
    rt_mutex_release(&speaker_mutex);
    return result;
}

void app_speaker_start(void) { (void)app_speaker_tone(1000U, 0U); }
int app_speaker_play_note(uint16_t frequency_hz) { return app_speaker_tone(frequency_hz, 0U); }

void app_speaker_stop(void)
{
    if (speaker_lock() != APP_SPEAKER_OK) return;
    speaker_stop_locked();
    rt_mutex_release(&speaker_mutex);
}

static int speaker_refresh_setting(void)
{
    int result = 0;
    rt_tick_t now = rt_tick_get();
    speaker_update(now);
    if ((state.mode == APP_SPEAKER_TONE) || (state.mode == APP_SPEAKER_MELODY))
    {
        result = speaker_output(speaker_gate_open(now) ? state.frequency_hz : 0U);
        if (result != 0) speaker_fail(result);
    }
    return result;
}

int app_speaker_set_volume(uint8_t volume)
{
    int result;
    if (volume > 100U) return APP_SPEAKER_EINVAL;
    if (speaker_lock() != 0) return APP_SPEAKER_ESTATE;
    state.volume = volume;
    result = speaker_refresh_setting();
    rt_mutex_release(&speaker_mutex);
    return result;
}

int app_speaker_set_waveform(app_speaker_wave_t wave)
{
    int result;
    if ((wave < APP_SPEAKER_SQUARE) || (wave > APP_SPEAKER_SAWTOOTH)) return APP_SPEAKER_EINVAL;
    if (speaker_lock() != 0) return APP_SPEAKER_ESTATE;
    state.wave = wave;
    result = speaker_refresh_setting();
    rt_mutex_release(&speaker_mutex);
    return result;
}

int app_speaker_set_square_duty(uint8_t percent)
{
    int result;
    if ((percent < 5U) || (percent > 95U)) return APP_SPEAKER_EINVAL;
    if (speaker_lock() != 0) return APP_SPEAKER_ESTATE;
    state.square_duty = percent;
    result = speaker_refresh_setting();
    rt_mutex_release(&speaker_mutex);
    return result;
}

int app_speaker_set_gate(uint8_t percent)
{
    int result;
    if ((percent < 20U) || (percent > 100U)) return APP_SPEAKER_EINVAL;
    if (speaker_lock() != 0) return APP_SPEAKER_ESTATE;
    state.gate_percent = percent;
    result = speaker_refresh_setting();
    rt_mutex_release(&speaker_mutex);
    return result;
}

int app_speaker_play(const app_speaker_note_t *notes, uint16_t count, uint16_t bpm, uint8_t loop)
{
    uint16_t i;
    int result;
    if ((notes == 0) || (count == 0U) || (count > APP_SPEAKER_MAX_NOTES) || (bpm < 40U) || (bpm > 240U) || (loop > 1U)) return APP_SPEAKER_EINVAL;
    for (i = 0; i < count; i++)
    {
        if (((notes[i].frequency_hz != 0U) && ((notes[i].frequency_hz < 50U) || (notes[i].frequency_hz > 5000U))) || (notes[i].beats_x100 < 25U) || (notes[i].beats_x100 > 1600U)) return APP_SPEAKER_EINVAL;
    }
    if (speaker_lock() != 0) return APP_SPEAKER_ESTATE;
    speaker_stop_locked();
    memcpy(score, notes, count * sizeof(score[0]));
    state.mode = APP_SPEAKER_MELODY;
    state.note_count = count;
    state.note_index = 0U;
    state.bpm = bpm;
    state.loop = loop;
    state.frequency_hz = score[0].frequency_hz;
    state.last_error = 0;
    song_ticks = 0;
    for (i = 0; i < count; i++) song_ticks += speaker_note_ticks(i);
    stage_ticks = speaker_note_ticks(0U);
    stage_started = rt_tick_get();
    result = speaker_output(state.frequency_hz);
    if (result != 0) speaker_fail(result);
    rt_mutex_release(&speaker_mutex);
    return result;
}

int app_speaker_pause(void)
{
    if (speaker_lock() != 0) return APP_SPEAKER_ESTATE;
    speaker_update(rt_tick_get());
    if ((state.mode != APP_SPEAKER_TONE) && (state.mode != APP_SPEAKER_MELODY))
    {
        rt_mutex_release(&speaker_mutex);
        return APP_SPEAKER_ESTATE;
    }
    paused_mode = state.mode;
    paused_elapsed = rt_tick_get() - stage_started;
    speaker_hardware_stop();
    state.mode = APP_SPEAKER_PAUSED;
    rt_mutex_release(&speaker_mutex);
    return 0;
}

int app_speaker_resume(void)
{
    int result;
    rt_tick_t now;
    if (speaker_lock() != 0) return APP_SPEAKER_ESTATE;
    if (state.mode != APP_SPEAKER_PAUSED)
    {
        rt_mutex_release(&speaker_mutex);
        return APP_SPEAKER_ESTATE;
    }
    now = rt_tick_get();
    stage_started = now - paused_elapsed;
    state.mode = paused_mode;
    result = speaker_output(speaker_gate_open(now) ? state.frequency_hz : 0U);
    if (result != 0) speaker_fail(result);
    rt_mutex_release(&speaker_mutex);
    return result;
}

void app_speaker_process(void)
{
    if (speaker_lock() != 0) return;
    speaker_update(rt_tick_get());
    rt_mutex_release(&speaker_mutex);
}

int app_speaker_get_state(app_speaker_state_t *out)
{
    if (out == 0) return APP_SPEAKER_EINVAL;
    if (speaker_lock() != 0) return APP_SPEAKER_ESTATE;
    *out = state;
    rt_mutex_release(&speaker_mutex);
    return 0;
}

int app_speaker_play_demo(uint8_t loop)
{
    static const app_speaker_note_t melody[] =
    {
        {262,100},{294,100},{330,100},{262,100},
        {262,100},{294,100},{330,100},{262,100},
        {330,100},{349,100},{392,200},
        {330,100},{349,100},{392,200},
        {392,50},{440,50},{392,50},{349,50},{330,100},{262,100},
        {392,50},{440,50},{392,50},{349,50},{330,100},{262,100},
        {262,100},{196,100},{262,200},
        {262,100},{196,100},{262,200}
    };
    return app_speaker_play(melody, (uint16_t)(sizeof(melody) / sizeof(melody[0])), 120U, loop);
}


/* DMA中断只推进半区所有权；不取互斥锁、不打印、不调用LVGL。 */
static void speaker_pcm_irq_stop(int result)
{
    TIM_Cmd(TIM8, DISABLE);
    DAC_DMACmd(DAC_Channel_1, DISABLE);
    DMA_Cmd(DMA2_Channel3, DISABLE);
    DAC_SetChannel1Data(DAC_Align_12b_R, SPEAKER_CENTER);
    /* 触发静音DHR进入真正输出寄存器，避免停在最后一个采样值。 */
    TIM_GenerateEvent(TIM8, TIM_EventSource_Update);
    pcm_running = 0;
    pcm_result = result;
}

static void speaker_pcm_boundary(uint8_t half)
{
    pcm_ready[half] = 0;
    if (pcm_closed && half == pcm_terminal_half) { speaker_pcm_irq_stop(1); return; }
    if (!pcm_ready[half ^ 1U]) speaker_pcm_irq_stop(-4);
}

static void speaker_pcm_interrupt(void)
{
    uint8_t ht = DMA_GetFlagStatus(DMA2_FLAG_HT3) != RESET;
    uint8_t tc = DMA_GetFlagStatus(DMA2_FLAG_TC3) != RESET;
    uint8_t error = DMA_GetFlagStatus(DMA2_FLAG_TE3) != RESET;
    DMA_ClearFlag(DMA2_FLAG_GL3);
    if (!pcm_running) return;
    /* 同时积压两个边界说明延迟超过安全窗口，不能继续使用旧半区。 */
    if (error || (ht && tc)) { speaker_pcm_irq_stop(-5); return; }
    if (ht) speaker_pcm_boundary(0);
    if (tc && pcm_running) speaker_pcm_boundary(1);
}

#ifndef APP_SPEAKER_HOST_TEST
void DMA2_Channel3_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA2_Channel3_IRQHandler(void)
{
    __asm volatile ("csrci mstatus, 8" ::: "memory");
    GET_INT_SP();
    rt_interrupt_enter();
    speaker_pcm_interrupt();
    rt_interrupt_leave();
    FREE_INT_SP();
}
#endif

static int speaker_pcm_start(void)
{
    RCC_ClocksTypeDef clocks;
    TIM_TimeBaseInitTypeDef timer;
    DMA_InitTypeDef dma;
    uint32_t timer_hz, counts;
    RCC_GetClocksFreq(&clocks);
    timer_hz = clocks.PCLK2_Frequency;
    if (clocks.PCLK2_Frequency != clocks.HCLK_Frequency) timer_hz *= 2U;
    counts = (timer_hz + 8000U) / 16000U;
    if (counts < 2U || counts > 65536U) return APP_SPEAKER_ECLOCK;
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = 0;
    timer.TIM_Period = counts - 1U;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM8, &timer);
    TIM_SelectOutputTrigger(TIM8, TIM_TRGOSource_Update);
    TIM_GenerateEvent(TIM8, TIM_EventSource_Update);
    TIM_ClearFlag(TIM8, TIM_FLAG_Update);
    DMA_DeInit(DMA2_Channel3);
    DMA_StructInit(&dma);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->R12BDHR1;
    dma.DMA_MemoryBaseAddr = (uint32_t)pcm_buffer;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = PCM_HALF_SAMPLES * 2U;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel3, &dma);
    DMA_ClearFlag(DMA2_FLAG_GL3);
    speaker_config_dac(DAC_Trigger_T8_TRGO);
    DAC_SetChannel1Data(DAC_Align_12b_R, SPEAKER_CENTER);
    DMA_ITConfig(DMA2_Channel3, DMA_IT_HT | DMA_IT_TC | DMA_IT_TE, ENABLE);
    NVIC_SetPriority(DMA2_Channel3_IRQn, 0xF0);
    NVIC_ClearPendingIRQ(DMA2_Channel3_IRQn);
    pcm_actual_rate = timer_hz / counts;
    pcm_running = 1; pcm_started = 1;
    state.output_active = 1;
    NVIC_EnableIRQ(DMA2_Channel3_IRQn);
    TIM_SetCounter(TIM8, 0);
    DMA_Cmd(DMA2_Channel3, ENABLE);
    DAC_DMACmd(DAC_Channel_1, ENABLE);
    TIM_Cmd(TIM8, ENABLE);
    return 0;
}

int app_speaker_pcm_begin(uint32_t sample_rate, uint8_t volume)
{
    if (sample_rate != 16000U || volume > 100U) return APP_SPEAKER_EINVAL;
    if (speaker_lock() != 0) return APP_SPEAKER_ESTATE;
    speaker_stop_locked();
    pcm_ready[0] = 0; pcm_ready[1] = 0;
    pcm_fill_half = 0; pcm_fill_offset = 0;
    pcm_closed = 0; pcm_started = 0; pcm_result = 0;
    pcm_accepted = 0; pcm_actual_rate = 0;
    state.volume = volume;
    state.mode = APP_SPEAKER_PCM;
    state.last_error = 0;
    rt_mutex_release(&speaker_mutex);
    return 0;
}

int app_speaker_pcm_write(const uint8_t *data, uint16_t length)
{
    uint16_t count, i;
    int32_t sample, peak;
    int result = 0;
    if (data == NULL || (length & 1U)) return APP_SPEAKER_EINVAL;
    if (speaker_lock() != 0) return APP_SPEAKER_ESTATE;
    if (state.mode != APP_SPEAKER_PCM || pcm_closed || pcm_result != 0) { rt_mutex_release(&speaker_mutex); return APP_SPEAKER_ESTATE; }
    if (pcm_ready[pcm_fill_half]) { rt_mutex_release(&speaker_mutex); return 0; }
    count = length / 2U;
    if (count > PCM_HALF_SAMPLES - pcm_fill_offset) count = PCM_HALF_SAMPLES - pcm_fill_offset;
    peak = ((uint32_t)SPEAKER_MAX_VPP_MV * state.volume * 4095U + SPEAKER_VREF_MV * 100U) / (SPEAKER_VREF_MV * 200U);
    for (i = 0; i < count; i++)
    {
        sample = (int16_t)((uint16_t)data[i * 2U] | ((uint16_t)data[i * 2U + 1U] << 8));
        pcm_buffer[pcm_fill_half * PCM_HALF_SAMPLES + pcm_fill_offset + i] = SPEAKER_CENTER + sample * peak / 32768L;
    }
    pcm_fill_offset += count;
    pcm_accepted += count;
    if (pcm_fill_offset == PCM_HALF_SAMPLES)
    {
        /* 样本全部写完，再发布ready；内存屏障防止编译器重排。 */
        __asm volatile ("" ::: "memory");
        pcm_ready[pcm_fill_half] = 1;
        pcm_fill_half ^= 1U; pcm_fill_offset = 0;
        if (!pcm_started && pcm_ready[0] && pcm_ready[1]) result = speaker_pcm_start();
    }
    if (pcm_result < 0) result = pcm_result;
    if (result != 0) { pcm_result = result; speaker_hardware_stop(); }
    rt_mutex_release(&speaker_mutex);
    return result != 0 ? result : (int)count * 2;
}

int app_speaker_pcm_end(void)
{
    int result = 0;
    rt_base_t level;
    if (speaker_lock() != 0) return APP_SPEAKER_ESTATE;
    if (state.mode != APP_SPEAKER_PCM || pcm_closed || pcm_result != 0) { rt_mutex_release(&speaker_mutex); return APP_SPEAKER_ESTATE; }
    if (pcm_fill_offset != 0U)
    {
        uint16_t i;
        for (i = pcm_fill_offset; i < PCM_HALF_SAMPLES; i++) pcm_buffer[pcm_fill_half * PCM_HALF_SAMPLES + i] = SPEAKER_CENTER;
    }
    /* 短歌曲不足一个半区时，后面的未使用半区也先置为静音。 */
    if (!pcm_started && !pcm_ready[1] && (pcm_fill_half == 0 || pcm_fill_offset == 0))
    {
        uint16_t i;
        for (i = PCM_HALF_SAMPLES; i < PCM_HALF_SAMPLES * 2U; i++) pcm_buffer[i] = SPEAKER_CENTER;
    }
    level = rt_hw_interrupt_disable();
    if (pcm_result != 0) result = pcm_result;
    else
    {
        if (pcm_fill_offset != 0U)
        {
            pcm_ready[pcm_fill_half] = 1;
            pcm_fill_half ^= 1U; pcm_fill_offset = 0;
        }
        pcm_terminal_half = pcm_fill_half ^ 1U;
        pcm_closed = 1;
        if (pcm_accepted == 0) pcm_result = 1;
        else if (!pcm_started) result = speaker_pcm_start();
    }
    rt_hw_interrupt_enable(level);
    if (result != 0) { pcm_result = result; speaker_hardware_stop(); }
    rt_mutex_release(&speaker_mutex);
    return result;
}

int app_speaker_pcm_get_state(app_speaker_pcm_state_t *out)
{
    if (out == NULL) return APP_SPEAKER_EINVAL;
    if (speaker_lock() != 0) return APP_SPEAKER_ESTATE;
    out->result = pcm_result;
    if (state.mode != APP_SPEAKER_PCM && pcm_result == 0) out->result = -6;
    out->running = pcm_running;
    out->input_closed = pcm_closed;
    out->accepted_samples = pcm_accepted;
    out->actual_sample_rate = pcm_actual_rate;
    if (state.mode == APP_SPEAKER_PCM && pcm_result != 0)
    {
        state.output_active = 0;
        state.last_error = pcm_result < 0 ? pcm_result : 0;
    }
    rt_mutex_release(&speaker_mutex);
    return 0;
}
