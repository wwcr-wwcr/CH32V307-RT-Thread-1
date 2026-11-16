#ifndef APP_MIC_H
#define APP_MIC_H

#include <stdint.h>

/*
 * 功能：初始化麦克风采集电路。
 * 参数：无。
 * 返回：0表示本阶段初始化完成。
 * 配置GPIO、OPA3、ADC1及TIM3时基；默认保持软件单点读取模式。
 * 初始化失败返回负数；不自动启动DMA。
 * 使用：在线程或main中调用一次。
 */
int app_mic_init(void);

/*
 * 功能：读取一次麦克风ADC原始值。
 * 参数：value用于接收结果，不能为NULL。
 * 返回：0成功；负数失败。
 * 注意：结果是ADC计数，不是音量，也不是分贝。
 */
int app_mic_read_raw(uint16_t *value);


/* TIM3更新触发频率8kHz；默认256点约32ms，最大512点约64ms。 */
#define APP_MIC_SAMPLE_RATE_HZ 8000U
#define APP_MIC_FRAME_SAMPLES 256U
#define APP_MIC_DMA_MAX_SAMPLES 512U

/* DMA采集：buffer指向count个uint16_t，且须按2字节对齐。
 * count=1..512，DMA普通模式，一组采满即停止。buffer必须在函数返回前一直有效。
 * 返回0成功；-1参数错误；-2未初始化；-3超时；-4 DMA错误。
 * 失败时buffer可能仅部分填充，不能用于峰峰值计算。
 * 等待期间让出CPU；只在线程中调用，不在GUI回调或中断中调用。
 * raw/DMA接口应由同一个业务线程串行调用，不支持多个线程并发访问。
 * 独占ADC1、DMA1_CH1、TIM3，不能同时运行NTC/其他ADC1采样或TIM3任务。
 * 返回前恢复软件触发和ADC开启状态，后续仍可调用app_mic_read_raw。
 * DMA保存未经逐点偏移修正的12位原始值；常数偏置不影响峰峰值。
 */
int app_mic_read_dma(uint16_t *buffer, uint16_t count);

/* 纯计算：最大值减最小值，参数错误返回-1；对有效ADC数据返回0..4095。
 * count>0，buffer包含count项；不修改数组，不访问硬件。
 * 返回单位为ADC计数，不能直接当作分贝。
 */
int app_mic_peak_to_peak(const uint16_t *buffer, uint16_t count);


/* 按帧进行能量平滑；4表示新能量占1/4，旧能量占3/4。
 * 1表示关闭跨帧平滑；数字越大响应越慢。不是音频A计权滤波。
 */
#define APP_MIC_FILTER_DIV 4U

typedef struct
{
    uint16_t minimum;
    uint16_t maximum;
    uint16_t peak_to_peak;
    uint16_t mean;                  /* 本组直流平均值，四舍五入。 */
    uint16_t rail_samples;          /* 等于0或4095的样本数，提示ADC达到量程端点。 */
    float rms_counts;              /* 去直流后的原始有效值，单位ADC计数。 */
    float filtered_rms_counts;     /* 平滑能量开平方后的有效值。 */
    float dbfs;                    /* 未平滑RMS分贝。 */
    float filtered_dbfs;           /* 平滑RMS分贝，推荐用于显示。 */
    int32_t filtered_dbfs_x10;      /* 平滑分贝乘10再四舍五入，方便rt_kprintf显示。 */
} app_mic_level_t;

/* 处理一次成功采满的DMA数据：去直流->RMS能量->指数平滑->分贝。
 * count=2..512，所有样本必须是0..4095；out不能为NULL。
 * 返回0成功，-1参数错误；失败不修改输出，也不更新滤波状态。
 * 不采集、不访问硬件、不修改buffer；每一组新数据只调用一次。
 * 内部只有一份平滑状态，须与采集接口一样由一个线程串行调用。
 * dBFS采用RMS/2047.5作为参考：满幅方波约0dBFS，满幅正弦约-3.01dBFS。
 * 纯直流对应无交流能量，显示下限-120dBFS；不是测量精度或真实噪声底。
 * 未实现声压标定与A/C计权，不可把返回值标成dB SPL、dBA。
 * EMA按帧更新，响应时间随采集周期变化；它平滑显示，不等于去除所有噪声。
 */
int app_mic_analyze(const uint16_t *buffer, uint16_t count, app_mic_level_t *out);

/* 清除上一段声音的滤波历史，下一帧直接作为初始值；init会自动调用。 */
void app_mic_filter_reset(void);

/* 获取失败专用标记，不与有效范围-1200..0混淆。 */
#define APP_MIC_DB_ERROR INT32_MIN

/* 一次调用完成DMA采集、去直流、RMS和能量平滑，返回平滑dBFS乘10。
 * 示例：-483代表-48.3dBFS，0代表0.0dBFS；失败返回APP_MIC_DB_ERROR。
 * 使用前调用app_mic_init；正常采集约32ms，超时约100ms，等待期间让出CPU。
 * 内部固定缓冲占512字节RAM，不占用调用者线程栈，不申请动态内存。
 * 只允许一个业务线程串行调用；不在中断或LVGL回调中调用。
 * 不要再对同一组数据重复analyze；本接口每调用一次就推进一次滤波。
 * 返回的是相对dBFS，不是未经校准即可得到的环境声压级dB SPL。
 */
int32_t app_mic_get_db_x10(void);

#endif