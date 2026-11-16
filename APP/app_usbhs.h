#ifndef APP_USBHS_H
#define APP_USBHS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_USBHS_OK          0
#define APP_USBHS_EINVAL      (-1)
#define APP_USBHS_ESTATE      (-2)
#define APP_USBHS_ECLOCK      (-3)
#define APP_USBHS_MAX_PACKET  512U

typedef struct
{
    uint8_t initialized;    /* 已调用init；不代表电脑已识别。 */
    uint8_t configured;     /* 电脑已选择配置1，USB枚举完成。 */
    uint8_t suspended;      /* 总线挂起；不能据此判断插头是否拔出。 */
    uint8_t high_speed;     /* 1=实际协商为480Mbps；0=12Mbps或尚未枚举。 */
    uint8_t dtr;            /* 电脑串口软件设置的DTR，不等同于物理连接。 */
    uint8_t rts;
    uint8_t tx_busy;        /* 当前发送包或其结束零长包尚未完成。 */
    uint16_t packet_size;   /* 当前批量端点单包上限：HS=512，FS=64。 */
    uint16_t rx_available;  /* 已收到但尚未读出的字节数。 */
    uint32_t rx_bytes;      /* 本次init以来累计接收字节，32位自然回绕。 */
    uint32_t tx_bytes;      /* 已被主机确认接收的有效字节数。 */
    uint32_t bus_resets;    /* USB总线复位次数，不表示MCU复位次数。 */
    uint32_t line_baudrate; /* CDC线编码参数；不是USB链路速率。 */
    uint32_t connection_id; /* 复位/重新配置时变化，用于丢弃旧音频会话。 */
} app_usbhs_state_t;

/* USBHS设备模式，CDC虚拟串口；PB7=D+，PB6=D-，本板8MHz HSE。
 * 仅在线程中调用；不创建线程，不占用USART、TIM或通用DMA通道。
 * USB控制器内部DMA需要约1.2KB固定RAM；无需动态申请。
 * init重复调用不打断连接。返回0或负错误码；init成功不代表枚举成功。
 * 与其他USBHS驱动互斥，不能同时运行官方CDC原例程。
 */
int app_usbhs_init(void);

/* 断开本USB功能并丢弃未完成的收发数据；以后可以重新init。 */
void app_usbhs_deinit(void);

/* 1=配置完成且未挂起；0=未就绪。并非VBUS/物理插拔检测。 */
int app_usbhs_is_ready(void);

/* 非阻塞读取二进制数据：buffer可写，capacity为容量（字节）。
 * 返回>0实际读出字节；0暂无数据/未就绪；<0参数错误或尚未init。
 * 不添加字符串结束符。capacity=0允许buffer=NULL。
 * 缓冲未读完时端点NAK，电脑USB控制器稍后重试，防止覆盖。
 * 建议只有一个业务线程读取，避免多个功能分走同一条字节流。
 */
int app_usbhs_read(void *buffer, uint16_t capacity);

/* 接收流所有权：owner为持久非NULL标记地址；0成功，-2已被其他消费者占用。
 * 音频和回显不能同时消费同一条USB流。普通read仅在未占用时可读。
 */
int app_usbhs_rx_claim(const void *owner);
void app_usbhs_rx_release(const void *owner);
int app_usbhs_read_owned(const void *owner, void *buffer, uint16_t capacity);

/* 非阻塞发送：复制最多一个USB包，返回实际接受的字节数。
 * 0=忙/未就绪/length为0；<0=参数错误或尚未init。
 * 返回后原buffer即可修改；返回>0只代表排队成功，尚未确认送达。
 * 长数据必须根据返回值移动指针并重试，不能假设一次发送完。
 * 满包后自动补零长包；协议按字节流处理，不依赖USB包边界。
 */
int app_usbhs_write(const void *buffer, uint16_t length);

/* 在一个短临界区内读取一致的状态快照，state不能为NULL。 */
int app_usbhs_get_state(app_usbhs_state_t *state);

/* 非阻塞回显测试服务，约每1ms调用一次；不主动创建线程。
 * 此函数会消费接收到的数据，正常业务/音频接收时不要调用它。
 * 内含64字节待发缓冲，发送忙时保留，断开/复位后自动清除。
 */
void app_usbhs_echo_process(void);

#ifdef __cplusplus
}
#endif
#endif
