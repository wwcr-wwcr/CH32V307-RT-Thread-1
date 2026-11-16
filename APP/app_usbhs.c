/* USBHS寄存器初始化与端点时序参考WCH CH32V307EVT：
 * USB/USBHS/DEVICE/SimulateCDC/User/USB_Device。
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * 原例程及修改版仅用于沁恒微控制器。
 * 本模块独立实现CDC字节流，不再转发USART2，不占用TIM2。
 */
#include "app_usbhs.h"
#include "app_usbhs_desc.h"
#include "ch32v30x.h"
#include "ch32v30x_usb.h"
#include <rtthread.h>
#include <rthw.h>
#include <string.h>
#ifndef APP_USBHS_HOST_TEST
#include "ch32v30x_it.h"
#include <finsh.h>
#endif

/* USB内部DMA地址要求4字节对齐。OUT包没有读完前保持NAK，
 * 因而只需一个512字节接收包缓冲，无需大环形队列。
 */
static uint8_t ep0_buffer[64] __attribute__((aligned(4)));
static uint8_t rx_buffer[512] __attribute__((aligned(4)));
static uint8_t tx_buffer[512] __attribute__((aligned(4)));
static uint8_t notify_buffer[16] __attribute__((aligned(4)));
static uint8_t echo_buffer[64];
static uint16_t echo_length, echo_offset;
static uint32_t echo_generation;
static volatile uint32_t generation;
static const void *rx_owner;
static volatile uint8_t initialized, configured, suspended, high_speed;
static volatile uint8_t dtr, rts, tx_busy, tx_zlp, notify_busy;
static volatile uint16_t rx_length, rx_offset, tx_length;
static volatile uint32_t rx_bytes, tx_bytes, bus_resets;
/* 115200 / 1停止位 / 无校验 / 8数据位；仅保存CDC配置，不配置USART。 */
static uint8_t line_coding[7] = {0x00,0xC2,0x01,0x00,0,0,8};

typedef enum {CTRL_IDLE, CTRL_DATA_IN, CTRL_LINE_OUT, CTRL_STATUS_IN, CTRL_STATUS_OUT} ctrl_phase_t;
static ctrl_phase_t ctrl_phase;
static const uint8_t *ctrl_pointer;
static uint16_t ctrl_remaining;
static uint8_t ctrl_zlp, pending_address, address_valid;

/* 不调用会重配SysTick的裸机Delay_Us，也不等待未确认可用的mcycle。
 * 2048次NOP在本板96MHz下至少约21us，且循环次数固定，必定退出。
 * 这是USB复位所需的短等待，不用于音频采样时序。
 */
static void usbhs_reset_delay(void)
{
#ifndef APP_USBHS_HOST_TEST
    uint32_t count;
    for (count = 0; count < 2048U; count++)
    {
        __asm volatile ("nop");
    }
#endif
}

/* USBHS硬件自己完成PB6/PB7差分收发，不将这两脚配置成普通GPIO。 */
static uint16_t usbhs_packet_size(void)
{
    return high_speed ? 512U : 64U;
}

/* 清空数据端点：总线复位、取消配置、重新配置时都不能保留旧包。 */
static void usbhs_data_reset(void)
{
    rx_length = 0;
    rx_offset = 0;
    tx_busy = 0;
    tx_zlp = 0;
    tx_length = 0;
    notify_busy = 0;
    dtr = 0;
    rts = 0;
    generation++;
    USBHSD->ENDP_CONFIG = USBHS_UEP2_T_EN | USBHS_UEP2_R_EN | USBHS_UEP3_T_EN;
    USBHSD->BUF_MODE = 0;
    USBHSD->UEP2_MAX_LEN = usbhs_packet_size();
    USBHSD->UEP3_MAX_LEN = 16;
    USBHSD->UEP2_RX_DMA = (uint32_t)(uintptr_t)rx_buffer;
    USBHSD->UEP2_TX_DMA = (uint32_t)(uintptr_t)tx_buffer;
    /* 两个DMA地址均设为同一通知缓冲；端点3只开放IN方向。 */
    USBHSD->UEP3_RX_DMA = (uint32_t)(uintptr_t)notify_buffer;
    USBHSD->UEP3_TX_DMA = (uint32_t)(uintptr_t)notify_buffer;
    USBHSD->UEP2_TX_LEN = 0;
    USBHSD->UEP3_TX_LEN = 0;
    USBHSD->UEP2_TX_CTRL = USBHS_UEP_T_RES_NAK;
    USBHSD->UEP2_RX_CTRL = configured ? USBHS_UEP_R_RES_ACK : USBHS_UEP_R_RES_NAK;
    USBHSD->UEP3_TX_CTRL = USBHS_UEP_T_RES_NAK;
    USBHSD->UEP3_RX_CTRL = USBHS_UEP_R_RES_NAK;
}

static void usbhs_control_stall(void)
{
    ctrl_phase = CTRL_IDLE;
    address_valid = 0;
    USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_STALL;
    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_STALL;
}

/* 发送下一个控制IN包；DATA0/DATA1由调用处依次翻转。 */
static void usbhs_control_next(void)
{
    uint16_t count = ctrl_remaining > sizeof(ep0_buffer) ? sizeof(ep0_buffer) : ctrl_remaining;
    if (count != 0U)
    {
        memcpy(ep0_buffer, ctrl_pointer, count);
        ctrl_pointer += count;
        ctrl_remaining -= count;
    }
    else { ctrl_zlp = 0; }
    USBHSD->UEP0_TX_LEN = count;
    USBHSD->UEP0_TX_CTRL = (USBHSD->UEP0_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
}

static void usbhs_control_in(const uint8_t *data, uint16_t length, uint16_t requested)
{
    if (length > requested) { length = requested; }
    ctrl_pointer = data;
    ctrl_remaining = length;
    ctrl_zlp = (length < requested && (length % 64U) == 0U);
    ctrl_phase = CTRL_DATA_IN;
    usbhs_control_next();
}

/* 无数据的OUT控制请求以一个DATA1零长IN包结束。 */
static void usbhs_control_status(void)
{
    ctrl_phase = CTRL_STATUS_IN;
    USBHSD->UEP0_TX_LEN = 0;
    USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
}

/* 返回0表示端点不存在。控制端点只用于GET_STATUS，不支持halt。 */
static int usbhs_endpoint(uint16_t address)
{
    return address == 0x00U || address == 0x80U || (configured && (address == 0x02U || address == 0x82U || address == 0x83U));
}

static void usbhs_serial_notification(void)
{
    if (!configured || notify_busy) { return; }
    /* CDC SERIAL_STATE：通知电脑虚拟载波/DSR已就绪，不用于物理电平控制。 */
    const uint8_t notification[10] = {0xA1,0x20,0,0,0,0,2,0,3,0};
    memcpy(notify_buffer, notification, sizeof(notification));
    notify_busy = 1;
    USBHSD->UEP3_TX_LEN = sizeof(notification);
    USBHSD->UEP3_TX_CTRL = (USBHSD->UEP3_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
}

/* SETUP包固定8字节，先复制字段，后续EP0发送可安全覆盖原缓冲。 */
static void usbhs_setup(void)
{
    uint8_t type = ep0_buffer[0], request = ep0_buffer[1];
    uint16_t value = ep0_buffer[2] | ((uint16_t)ep0_buffer[3] << 8);
    uint16_t index = ep0_buffer[4] | ((uint16_t)ep0_buffer[5] << 8);
    uint16_t length = ep0_buffer[6] | ((uint16_t)ep0_buffer[7] << 8);
    const uint8_t *descriptor;
    uint16_t descriptor_length;
    uint8_t reply[2] = {0,0};

    ctrl_phase = CTRL_IDLE;
    address_valid = 0;
    USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;
    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;
    high_speed = ((USBHSD->SPEED_TYPE & USBHS_USB_SPEED_TYPE) == USBHS_USB_SPEED_HIGH);

    if ((type & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD)
    {
        switch (request)
        {
            case USB_GET_DESCRIPTOR:
                if (type != 0x80U) { break; }
                descriptor = app_usbhs_descriptor((uint8_t)(value >> 8), (uint8_t)value, high_speed, &descriptor_length);
                if (descriptor == NULL) { break; }
                usbhs_control_in(descriptor, descriptor_length, length);
                return;

            case USB_SET_ADDRESS:
                if (type != 0 || index != 0 || length != 0 || value > 127U || configured) { break; }
                pending_address = (uint8_t)value;
                address_valid = 1;
                usbhs_control_status();
                return;

            case USB_GET_CONFIGURATION:
                if (type != 0x80U || index != 0 || value != 0 || length != 1U) { break; }
                reply[0] = configured;
                usbhs_control_in(reply, 1, length);
                return;

            case USB_SET_CONFIGURATION:
                if (type != 0 || index != 0 || length != 0 || value > 1U) { break; }
                configured = (uint8_t)value;
                usbhs_data_reset();
                usbhs_control_status();
                return;

            case USB_GET_INTERFACE:
                if (type != 0x81U || !configured || index > 1U || value != 0 || length != 1U) { break; }
                usbhs_control_in(reply, 1, length);
                return;

            case USB_SET_INTERFACE:
                if (type != 0x01U || !configured || index > 1U || value != 0 || length != 0) { break; }
                usbhs_control_status();
                return;

            case USB_GET_STATUS:
                if (value != 0 || length != 2U) { break; }
                if (type == 0x80U && index == 0) { /* 总线供电，无远程唤醒。 */ }
                else if (type == 0x81U && configured && index <= 1U) { }
                else if (type == 0x82U && usbhs_endpoint(index))
                {
                    if (index == 0x02U) { reply[0] = (USBHSD->UEP2_RX_CTRL & USBHS_UEP_R_RES_MASK) == USBHS_UEP_R_RES_STALL; }
                    if (index == 0x82U) { reply[0] = (USBHSD->UEP2_TX_CTRL & USBHS_UEP_T_RES_MASK) == USBHS_UEP_T_RES_STALL; }
                    if (index == 0x83U) { reply[0] = (USBHSD->UEP3_TX_CTRL & USBHS_UEP_T_RES_MASK) == USBHS_UEP_T_RES_STALL; }
                }
                else { break; }
                usbhs_control_in(reply, 2, length);
                return;

            case USB_CLEAR_FEATURE:
            case USB_SET_FEATURE:
                if (type != 0x02U || value != USB_REQ_FEAT_ENDP_HALT || length != 0 || !configured || !usbhs_endpoint(index) || (index & 0x0FU) == 0) { break; }
                if (index == 0x02U)
                {
                    if (request == USB_SET_FEATURE) { USBHSD->UEP2_RX_CTRL = (USBHSD->UEP2_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_STALL; }
                    else { USBHSD->UEP2_RX_CTRL = rx_length ? USBHS_UEP_R_RES_NAK : USBHS_UEP_R_RES_ACK; }
                }
                else if (index == 0x82U)
                {
                    if (request == USB_SET_FEATURE) { USBHSD->UEP2_TX_CTRL = (USBHSD->UEP2_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_STALL; }
                    else { tx_busy = 0; tx_zlp = 0; USBHSD->UEP2_TX_CTRL = USBHS_UEP_T_RES_NAK; }
                }
                else
                {
                    if (request == USB_SET_FEATURE) { USBHSD->UEP3_TX_CTRL = (USBHSD->UEP3_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_STALL; }
                    else { notify_busy = 0; USBHSD->UEP3_TX_CTRL = USBHS_UEP_T_RES_NAK; }
                }
                usbhs_control_status();
                return;

            default: break;
        }
    }
    else if ((type & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS && index == 0 && configured)
    {
        if (request == CDC_GET_LINE_CODING && type == 0xA1U && value == 0 && length == 7U)
        {
            usbhs_control_in(line_coding, 7, length);
            return;
        }
        if (request == CDC_SET_LINE_CODING && type == 0x21U && value == 0 && length == 7U)
        {
            ctrl_phase = CTRL_LINE_OUT;
            USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
            return;
        }
        if (request == CDC_SET_LINE_CTLSTE && type == 0x21U && length == 0 && value <= 3U)
        {
            dtr = value & 1U;
            rts = (value >> 1) & 1U;
            usbhs_serial_notification();
            usbhs_control_status();
            return;
        }
        if (request == CDC_SEND_BREAK && type == 0x21U && length == 0)
        {
            /* 接受请求但不产生物理UART BREAK。 */
            usbhs_control_status();
            return;
        }
    }
    /* 不接受任意厂商请求，也不从未初始化的描述符指针取数据。 */
    usbhs_control_stall();
}

static void usbhs_ep0_in(void)
{
    USBHSD->UEP0_TX_CTRL = (USBHSD->UEP0_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
    if (ctrl_phase == CTRL_DATA_IN)
    {
        if (ctrl_remaining != 0U || ctrl_zlp)
        {
            USBHSD->UEP0_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
            usbhs_control_next();
        }
        else
        {
            ctrl_phase = CTRL_STATUS_OUT;
            USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
        }
    }
    else if (ctrl_phase == CTRL_STATUS_IN)
    {
        /* SET_ADDRESS必须等状态包发完后才让新地址生效。 */
        if (address_valid) { USBHSD->DEV_AD = pending_address; address_valid = 0; }
        ctrl_phase = CTRL_IDLE;
    }
}

static void usbhs_ep0_out(uint16_t length)
{
    if (ctrl_phase == CTRL_LINE_OUT && length == 7U)
    {
        /* 接受常用CDC线编码；它不会限制USB的传输带宽。 */
        if (ep0_buffer[4] > 2U || ep0_buffer[5] > 4U || !((ep0_buffer[6] >= 5U && ep0_buffer[6] <= 8U) || ep0_buffer[6] == 16U)) { usbhs_control_stall(); return; }
        memcpy(line_coding, ep0_buffer, 7);
        USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_NAK;
        usbhs_control_status();
    }
    else if (ctrl_phase == CTRL_STATUS_OUT && length == 0)
    {
        ctrl_phase = CTRL_IDLE;
        USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_NAK;
    }
    else { usbhs_control_stall(); }
}

/* 中断只推进USB协议与缓冲状态，不调用LVGL、不打印、不执行音频算法。 */
static void usbhs_interrupt_process(void)
{
    uint8_t flags = USBHSD->INT_FG;
    uint8_t status = USBHSD->INT_ST;
    uint8_t endpoint = status & USBHS_UIS_ENDP_MASK;
    uint8_t token = status & USBHS_UIS_TOKEN_MASK;
    uint16_t length;
    if (flags & USBHS_UIF_BUS_RST)
    {
        configured = 0;
        suspended = 0;
        high_speed = 0;
        ctrl_phase = CTRL_IDLE;
        address_valid = 0;
        bus_resets++;
        USBHSD->DEV_AD = 0;
        USBHSD->UEP0_MAX_LEN = sizeof(ep0_buffer);
        USBHSD->UEP0_DMA = (uint32_t)(uintptr_t)ep0_buffer;
        USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
        USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_ACK;
        usbhs_data_reset();
        /* 复位优先，并丢弃同一时刻旧连接的SETUP/TRANSFER标志。 */
        USBHSD->INT_FG = flags;
        return;
    }
    /* 非EP0传输先处理，不能因同时出现SETUP而丢弃已经ACK的数据包。 */
    if ((flags & USBHS_UIF_SETUP_ACT) && (!(flags & USBHS_UIF_TRANSFER) || endpoint == 0U))
    {
        usbhs_setup();
        /* SETUP会终止之前的控制传输，不能再处理旧的TRANSFER状态。 */
        USBHSD->INT_FG = USBHS_UIF_SETUP_ACT | (flags & USBHS_UIF_TRANSFER);
        return;
    }
    if (flags & USBHS_UIF_TRANSFER)
    {
        if (token == USBHS_UIS_TOKEN_IN)
        {
            if (endpoint == 0U) { usbhs_ep0_in(); }
            if (endpoint == 2U)
            {
                USBHSD->UEP2_TX_CTRL = (USBHSD->UEP2_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
                USBHSD->UEP2_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
                if (tx_busy)
                {
                    tx_bytes += tx_length;
                    tx_length = 0;
                    if (tx_zlp)
                    {
                        tx_zlp = 0;
                        USBHSD->UEP2_TX_LEN = 0;
                        USBHSD->UEP2_TX_CTRL = (USBHSD->UEP2_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
                    }
                    else { tx_busy = 0; }
                }
            }
            if (endpoint == 3U)
            {
                USBHSD->UEP3_TX_CTRL = (USBHSD->UEP3_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
                USBHSD->UEP3_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
                notify_busy = 0;
            }
        }
        else if (token == USBHS_UIS_TOKEN_OUT && (status & USBHS_UIS_TOG_OK))
        {
            length = USBHSD->RX_LEN;
            if (endpoint == 0U) { usbhs_ep0_out(length); }
            if (endpoint == 2U && configured && rx_length == 0U && length <= usbhs_packet_size())
            {
                USBHSD->UEP2_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
                rx_offset = 0;
                rx_length = length;
                rx_bytes += length;
                if (length != 0U) { USBHSD->UEP2_RX_CTRL = (USBHSD->UEP2_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK; }
            }
        }
        USBHSD->INT_FG = USBHS_UIF_TRANSFER;
        return;
    }
    if (flags & USBHS_UIF_SUSPEND)
    {
        suspended = (USBHSD->MIS_ST & USBHS_UMS_SUSPEND) != 0;
        USBHSD->INT_FG = USBHS_UIF_SUSPEND;
        return;
    }
    USBHSD->INT_FG = flags;
}

#ifndef APP_USBHS_HOST_TEST
void USBHS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBHS_IRQHandler(void)
{
    /* 当前RTT端口用mscratch交换中断栈，USB处理中禁止再次嵌套。
     * mret按硬件保存的状态恢复；不在处理过程中主动开中断。
     */
    __asm volatile ("csrci mstatus, 8" ::: "memory");
    /* 与本工程USART等中断保持相同的RT-Thread中断栈约定。 */
    GET_INT_SP();
    rt_interrupt_enter();
    usbhs_interrupt_process();
    rt_interrupt_leave();
    FREE_INT_SP();
}
#endif

int app_usbhs_init(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (initialized) { rt_hw_interrupt_enable(level); return APP_USBHS_OK; }
    /* 本板HSE=8MHz；USB专用PLL参考输入配置为8MHz/2=4MHz。
     * 检查失败直接返回，不能让USB运行在错误时钟下。
     */
    if (HSE_VALUE != 8000000U || RCC_GetFlagStatus(RCC_FLAG_HSERDY) == RESET)
    {
        rt_hw_interrupt_enable(level);
        return APP_USBHS_ECLOCK;
    }
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
    NVIC_DisableIRQ(USBHS_IRQn);
    USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
    usbhs_reset_delay();
    USBHSD->CONTROL &= ~USBHS_UC_RESET_SIE;
    USBHSD->HOST_CTRL = USBHS_UH_PHY_SUSPENDM;
    USBHSD->CONTROL = USBHS_UC_DMA_EN | USBHS_UC_INT_BUSY | USBHS_UC_SPEED_HIGH;
    USBHSD->DEV_AD = 0;
    USBHSD->UEP0_MAX_LEN = sizeof(ep0_buffer);
    USBHSD->UEP0_DMA = (uint32_t)(uintptr_t)ep0_buffer;
    USBHSD->UEP0_TX_LEN = 0;
    USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_ACK;
    configured = 0;
    high_speed = 0;
    suspended = 0;
    ctrl_phase = CTRL_IDLE;
    address_valid = 0;
    rx_bytes = 0;
    tx_bytes = 0;
    bus_resets = 0;
    usbhs_data_reset();
    echo_length = 0;
    echo_offset = 0;
    echo_generation = generation;
    USBHSD->INT_FG = 0xFF;
    USBHSD->INT_EN = USBHS_UIE_SETUP_ACT | USBHS_UIE_TRANSFER | USBHS_UIE_BUS_RST | USBHS_UIE_SUSPEND;
    NVIC_ClearPendingIRQ(USBHS_IRQn);
    /* 与系统节拍/软件调度中断同优先级，USB不抢占它们的中断栈。 */
    NVIC_SetPriority(USBHS_IRQn, 0xF0);
    initialized = 1;
    NVIC_EnableIRQ(USBHS_IRQn);
    USBHSD->CONTROL |= USBHS_UC_DEV_PU_EN;
    rt_hw_interrupt_enable(level);
    return APP_USBHS_OK;
}

void app_usbhs_deinit(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (initialized)
    {
        NVIC_DisableIRQ(USBHS_IRQn);
        USBHSD->INT_EN = 0;
        USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
        usbhs_reset_delay();
        USBHSD->CONTROL = 0;
        NVIC_ClearPendingIRQ(USBHS_IRQn);
        initialized = 0;
        configured = 0;
        suspended = 0;
        high_speed = 0;
        dtr = 0;
        rts = 0;
        rx_length = 0;
        rx_offset = 0;
        tx_busy = 0;
        tx_zlp = 0;
        ctrl_phase = CTRL_IDLE;
        address_valid = 0;
        echo_length = 0;
        echo_offset = 0;
        generation++;
    }
    /* 不关闭共享PHY PLL，避免影响其他潜在的USB时钟使用者。 */
    rt_hw_interrupt_enable(level);
}

int app_usbhs_is_ready(void)
{
    return initialized && configured && !suspended;
}

int app_usbhs_read_owned(const void *owner, void *buffer, uint16_t capacity)
{
    uint16_t count;
    rt_base_t level;
    if (capacity != 0U && buffer == NULL) { return APP_USBHS_EINVAL; }
    level = rt_hw_interrupt_disable();
    if (!initialized) { rt_hw_interrupt_enable(level); return APP_USBHS_ESTATE; }
    if (!app_usbhs_is_ready() || capacity == 0U) { rt_hw_interrupt_enable(level); return 0; }
    if (rx_owner != owner) { rt_hw_interrupt_enable(level); return 0; }
    count = rx_length - rx_offset;
    if (count > capacity) { count = capacity; }
    if (count != 0U)
    {
        memcpy(buffer, rx_buffer + rx_offset, count);
        rx_offset += count;
        if (rx_offset == rx_length)
        {
            rx_offset = 0;
            rx_length = 0;
            /* 保留DATA切换位，且不能擅自解除主机设置的端点HALT。 */
            if ((USBHSD->UEP2_RX_CTRL & USBHS_UEP_R_RES_MASK) != USBHS_UEP_R_RES_STALL)
            {
                USBHSD->UEP2_RX_CTRL = (USBHSD->UEP2_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
            }
        }
    }
    rt_hw_interrupt_enable(level);
    return count;
}

int app_usbhs_read(void *buffer, uint16_t capacity)
{
    return app_usbhs_read_owned(NULL, buffer, capacity);
}

int app_usbhs_rx_claim(const void *owner)
{
    int result = 0;
    rt_base_t level = rt_hw_interrupt_disable();
    if (owner == NULL || (rx_owner != NULL && rx_owner != owner)) result = APP_USBHS_ESTATE;
    else rx_owner = owner;
    rt_hw_interrupt_enable(level);
    return result;
}

void app_usbhs_rx_release(const void *owner)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (owner != NULL && rx_owner == owner) rx_owner = NULL;
    rt_hw_interrupt_enable(level);
}

int app_usbhs_write(const void *buffer, uint16_t length)
{
    uint16_t count;
    rt_base_t level;
    if (length != 0U && buffer == NULL) { return APP_USBHS_EINVAL; }
    level = rt_hw_interrupt_disable();
    if (!initialized) { rt_hw_interrupt_enable(level); return APP_USBHS_ESTATE; }
    if (!app_usbhs_is_ready() || length == 0U || tx_busy || (USBHSD->UEP2_TX_CTRL & USBHS_UEP_T_RES_MASK) == USBHS_UEP_T_RES_STALL)
    {
        rt_hw_interrupt_enable(level);
        return 0;
    }
    count = length > usbhs_packet_size() ? usbhs_packet_size() : length;
    memcpy(tx_buffer, buffer, count);
    tx_length = count;
    tx_zlp = count == usbhs_packet_size();
    tx_busy = 1;
    USBHSD->UEP2_TX_LEN = count;
    USBHSD->UEP2_TX_CTRL = (USBHSD->UEP2_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
    rt_hw_interrupt_enable(level);
    return count;
}

int app_usbhs_get_state(app_usbhs_state_t *state)
{
    rt_base_t level;
    if (state == NULL) { return APP_USBHS_EINVAL; }
    level = rt_hw_interrupt_disable();
    state->connection_id = generation;
    state->initialized = initialized;
    state->configured = configured;
    state->suspended = suspended;
    state->high_speed = high_speed;
    state->dtr = dtr;
    state->rts = rts;
    state->tx_busy = tx_busy;
    state->packet_size = usbhs_packet_size();
    state->rx_available = rx_length - rx_offset;
    state->rx_bytes = rx_bytes;
    state->tx_bytes = tx_bytes;
    state->bus_resets = bus_resets;
    state->line_baudrate = (uint32_t)line_coding[0] | ((uint32_t)line_coding[1] << 8) | ((uint32_t)line_coding[2] << 16) | ((uint32_t)line_coding[3] << 24);
    rt_hw_interrupt_enable(level);
    return APP_USBHS_OK;
}

void app_usbhs_echo_process(void)
{
    int count;
    rt_base_t level = rt_hw_interrupt_disable();
    /* 一次收/发保持在同一短临界区，复位不能夹在两步中间传出旧数据。 */
    if (echo_generation != generation || !initialized || !configured)
    {
        echo_length = 0;
        echo_offset = 0;
        echo_generation = generation;
    }
    if (app_usbhs_is_ready())
    {
        if (echo_length == 0U)
        {
            count = app_usbhs_read_owned(rx_owner == echo_buffer ? echo_buffer : NULL, echo_buffer, sizeof(echo_buffer));
            if (count > 0) { echo_length = count; echo_offset = 0; }
        }
        if (echo_length != 0U)
        {
            count = app_usbhs_write(echo_buffer + echo_offset, echo_length - echo_offset);
            if (count > 0)
            {
                echo_offset += count;
                if (echo_offset == echo_length) { echo_offset = 0; echo_length = 0; }
            }
        }
    }
    rt_hw_interrupt_enable(level);
}

#ifndef APP_USBHS_HOST_TEST
/* 在原USART控制台输入这些命令，无需修改main或新增常驻线程。 */
static void usbhs_init(void)
{
    int result = app_usbhs_init();
    rt_kprintf("[USBHS] init=%d. Connect board USB to PC, then run usbhs_status.\n", result);
}
MSH_CMD_EXPORT(usbhs_init, Initialize USBHS CDC device);

static void usbhs_status(void)
{
    app_usbhs_state_t state;
    app_usbhs_get_state(&state);
    rt_kprintf("[USBHS] init=%u configured=%u suspend=%u HS=%u packet=%u DTR=%u RTS=%u busy=%u\n", state.initialized, state.configured, state.suspended, state.high_speed, state.packet_size, state.dtr, state.rts, state.tx_busy);
    rt_kprintf("[USBHS] RX=%lu TX=%lu pending=%u resets=%lu line_baud=%lu\n", (unsigned long)state.rx_bytes, (unsigned long)state.tx_bytes, state.rx_available, (unsigned long)state.bus_resets, (unsigned long)state.line_baudrate);
}
MSH_CMD_EXPORT(usbhs_status, Show USBHS enumeration and counters);

static void usbhs_deinit(void)
{
    app_usbhs_deinit();
    rt_kprintf("[USBHS] disabled\n");
}
MSH_CMD_EXPORT(usbhs_deinit, Disconnect USBHS CDC device);

static void usbhs_echo(void)
{
    rt_tick_t start;
    if (app_usbhs_rx_claim(echo_buffer) != 0) { rt_kprintf("[USBHS] RX busy. Run usb_audio_off first.\n"); return; }
    if (app_usbhs_init() != 0) { app_usbhs_rx_release(echo_buffer); rt_kprintf("[USBHS] clock/init failed\n"); return; }
    rt_kprintf("[USBHS] Echo test for 30 seconds. Send bytes to NEW USB COM port.\n");
    start = rt_tick_get();
    while ((rt_tick_t)(rt_tick_get() - start) < rt_tick_from_millisecond(30000))
    {
        app_usbhs_echo_process();
        rt_thread_mdelay(1);
    }
    rt_kprintf("[USBHS] Echo test ended\n");
    app_usbhs_rx_release(echo_buffer);
    usbhs_status();
}
MSH_CMD_EXPORT(usbhs_echo, Run USB CDC binary echo test for 30 seconds);
#endif
