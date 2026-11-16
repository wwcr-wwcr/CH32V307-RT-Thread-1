#include "app_usb_audio.h"
#include "app_usbhs.h"
#include "app_speaker.h"
#include <rtthread.h>
#include <string.h>
#ifndef APP_USB_AUDIO_HOST_TEST
#include <finsh.h>
#endif

/* USB是字节流，帧可能跨任意数量USB包；不能假定一次read就是一帧。
 * 帧：APCM + version(1) + type(1) + length(2LE) + seq(4LE) + CRC32(4LE) + payload。
 * CRC覆盖前12字节和payload。DATA最长512字节；所有帧停等ACK，不自动重发。
 * BEGIN=1，DATA=2，END=3，STOP=4；回复ARSP/版本/ACK=0,DONE=1,ERROR=2/错误码/seq/CRC。
 */
static uint8_t frame[16 + 512];
static uint8_t reply[16];
static uint16_t have, need = 16, payload_offset, reply_length, reply_offset;
static uint8_t session, closing, held_frame, claimed;
static volatile uint8_t enabled = 1;
static uint32_t expected_seq, end_seq, connection, total_bytes;
static rt_tick_t last_activity;
static int last_error;

static uint16_t get16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static uint32_t crc_update(uint32_t crc, const uint8_t *data, uint16_t length)
{
    uint16_t i; uint8_t bit;
    for (i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0);
    }
    return crc;
}

static void parser_clear(void) { have = 0; need = 16; payload_offset = 0; held_frame = 0; }

static void audio_reply(uint8_t type, uint16_t code, uint32_t seq)
{
    memcpy(reply, "ARSP", 4); reply[4] = 1; reply[5] = type;
    reply[6] = code; reply[7] = code >> 8; put32(reply + 8, seq);
    put32(reply + 12, crc_update(0xFFFFFFFFUL, reply, 12) ^ 0xFFFFFFFFUL);
    reply_length = 16; reply_offset = 0;
}

static void audio_abort(int code, uint32_t seq, int send_reply)
{
    if (session) app_speaker_stop();
    session = 0; closing = 0; last_error = code;
    parser_clear();
    if (send_reply) audio_reply(2, (uint16_t)code, seq);
}

/* 返回1=此帧已执行，0=PCM缓冲满，保留整帧待下轮继续。 */
static int execute_frame(void)
{
    uint8_t type = frame[5];
    uint16_t length = get16(frame + 6);
    uint32_t seq = get32(frame + 8);
    int count;
    if (type == 4U && length == 0U)
    {
        audio_abort(0, seq, 0); audio_reply(0, 0, seq); return 1;
    }
    if (type == 1U)
    {
        if (seq != 0 || length != 8 || get32(frame + 16) != 16000U || frame[21] != 1 || get16(frame + 22) != 16U || frame[20] > 100U)
        { audio_abort(1, seq, 1); return 1; }
        count = app_speaker_pcm_begin(16000, frame[20]);
        if (count != 0) { audio_abort(2, seq, 1); return 1; }
        session = 1; closing = 0; total_bytes = 0; last_error = 0; expected_seq = 1;
        audio_reply(0, 0, seq); return 1;
    }
    if (!session || closing || seq != expected_seq) { audio_abort(3, seq, 1); return 1; }
    if (type == 2U && length != 0U && (length & 1U) == 0U)
    {
        count = app_speaker_pcm_write(frame + 16 + payload_offset, length - payload_offset);
        if (count < 0) { audio_abort(4, seq, 1); return 1; }
        payload_offset += count; total_bytes += count;
        if (payload_offset != length) return 0;
        expected_seq++; audio_reply(0, 0, seq); return 1;
    }
    if (type == 3U && length == 0U)
    {
        if (app_speaker_pcm_end() != 0) { audio_abort(4, seq, 1); return 1; }
        closing = 1; end_seq = seq; expected_seq++;
        audio_reply(0, 0, seq); return 1;
    }
    audio_abort(1, seq, 1); return 1;
}

int app_usb_audio_init(void)
{
    int result;
    rt_kprintf("[USB AUDIO] speaker init...\n");
    result = app_speaker_init();
    rt_kprintf("[USB AUDIO] speaker result=%d\n", result);
    if (result == 0)
    {
        rt_kprintf("[USB AUDIO] USBHS init...\n");
        result = app_usbhs_init();
        rt_kprintf("[USB AUDIO] USBHS result=%d\n", result);
    }
    if (result != 0) return result;
    claimed = app_usbhs_rx_claim(frame) == 0;
    enabled = 1; session = 0; closing = 0; last_error = 0;
    parser_clear(); reply_length = 0;
    last_activity = rt_tick_get();
    return claimed ? 0 : -2;
}

void app_usb_audio_enable(uint8_t enable) { enabled = enable != 0U; }

void app_usb_audio_process(void)
{
    app_usbhs_state_t usb;
    app_speaker_pcm_state_t pcm;
    int count;
    uint32_t crc;
    if (!enabled)
    {
        if (claimed) { audio_abort(0, 0, 0); reply_length = 0; app_usbhs_rx_release(frame); claimed = 0; }
        return;
    }
    if (!claimed)
    {
        if (app_usbhs_rx_claim(frame) != 0) return;
        claimed = 1; parser_clear(); reply_length = 0;
    }
    app_usbhs_get_state(&usb);
    if (connection != usb.connection_id || !app_usbhs_is_ready())
    {
        if (session) audio_abort(7, 0, 0);
        parser_clear(); reply_length = 0; connection = usb.connection_id;
        return;
    }
    /* 回复可能被USB短包/发送忙拆开，必须记录已发送偏移。 */
    if (reply_length != 0U)
    {
        count = app_usbhs_write(reply + reply_offset, reply_length - reply_offset);
        if (count > 0) reply_offset += count;
        if (reply_offset == reply_length) { reply_length = 0; reply_offset = 0; }
        if (reply_length != 0U) return;
    }
    if (session)
    {
        app_speaker_pcm_get_state(&pcm);
        if (pcm.result < 0)
        {
            int code = pcm.result == -4 ? 8 : (pcm.result == -5 ? 9 : 10);
            audio_abort(code, held_frame ? get32(frame + 8) : expected_seq, 1);
            rt_kprintf("[USB AUDIO] stopped, error=%d\n", code); return;
        }
        if (closing && pcm.result == 1)
        {
            session = 0; closing = 0;
            audio_reply(1, 0, end_seq);
            rt_kprintf("[USB AUDIO] finished, PCM bytes=%lu\n", (unsigned long)total_bytes); return;
        }
    }
    if ((session || have != 0U) && (rt_tick_t)(rt_tick_get() - last_activity) > rt_tick_from_millisecond(2000))
    { audio_abort(6, expected_seq, 1); return; }
    if (held_frame)
    {
        if (execute_frame()) parser_clear();
        return;
    }
    count = app_usbhs_read_owned(frame, frame + have, need - have);
    if (count <= 0) return;
    have += count; last_activity = rt_tick_get();
    /* 按字节滑动寻找帧头，即使上次会话异常中止也能重新同步。 */
    while (have >= 4U && memcmp(frame, "APCM", 4) != 0)
    { memmove(frame, frame + 1, --have); }
    if (have < 16U) return;
    if (frame[4] != 1 || get16(frame + 6) > 512U) { audio_abort(1, get32(frame + 8), 1); return; }
    need = 16U + get16(frame + 6);
    if (have < need) return;
    crc = crc_update(0xFFFFFFFFUL, frame, 12);
    crc = crc_update(crc, frame + 16, get16(frame + 6)) ^ 0xFFFFFFFFUL;
    if (crc != get32(frame + 12)) { audio_abort(5, get32(frame + 8), 1); return; }
    held_frame = 1;
    if (execute_frame()) parser_clear();
}

#ifndef APP_USB_AUDIO_HOST_TEST
static void usb_audio_on(void) { app_usb_audio_enable(1); rt_kprintf("[USB AUDIO] receiver enabled\n"); }
static void usb_audio_off(void) { app_usb_audio_enable(0); rt_kprintf("[USB AUDIO] stop requested; echo available after next service tick\n"); }
static void usb_audio_status(void)
{
    rt_kprintf("[USB AUDIO] enabled=%u claimed=%u session=%u closing=%u bytes=%lu error=%d\n", enabled, claimed, session, closing, (unsigned long)total_bytes, last_error);
}
MSH_CMD_EXPORT(usb_audio_on, Enable PC PCM receiver);
MSH_CMD_EXPORT(usb_audio_off, Stop PC PCM receiver and release USB RX);
MSH_CMD_EXPORT(usb_audio_status, Show PC PCM stream state);
#endif
