#ifndef _FRUSART_H
#define _FRUSART_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "driver_system.h"
#include "driver_gpio.h"
#include "driver_timer.h"
#include "driver_uart.h"
#include "os_task.h"
#include "os_msg_q.h"
#include "os_timer.h"
#include "co_printf.h"

#define FRAME_FIRST_BYTE  0x5A
#define FRAME_SECOND_BYTE 0x7A
#define MAXFRAMBUFFLEN     10
#define MAXFRAMELEN        256

typedef __packed union {
    uint16_t Value;
    uint8_t  Bytes[2];
} TWordRec, *PWordRec;

typedef enum {
    pRec_Idlesse,
    pRec_HeadByte_H_Ok,
    pRec_HeadByte_L_Ok,
    pRec_FrameLen_H_OK,
    pRec_FrameHead_Ok,
    pRec_Complete
} TFrameRecState;

typedef __packed struct {
    TFrameRecState FrameRecState;
    uint16_t       RecPoint;
    uint16_t       TimeOutms;
    bool           SendCompleted;
    void           (*USdataHandle)(void);
} TCommRecBuf;

typedef __packed struct {
    uint16_t FramLen;
    uint8_t  FrameBuf[MAXFRAMELEN];
    uint16_t Point;
} TCommDataPacket;

typedef __packed struct {
    TCommDataPacket Field[MAXFRAMBUFFLEN];
    uint16_t        R_Point;
    uint16_t        W_Point;
    uint16_t        buflen;
} TCommSentListname;

extern TCommDataPacket AIRresdata;

extern TCommDataPacket ATUSART_0_RXbuf;
extern TCommDataPacket ATUSART_0_TXbuf;
extern TCommRecBuf Usart0UsbHead;
extern TCommSentListname USART_0_LIST;

extern TCommDataPacket ATUSART_1_RXbuf;
extern TCommDataPacket ATUSART_1_TXbuf;
extern TCommRecBuf Usart1UsbHead;
extern TCommSentListname USART_1_LIST;

extern uint8_t ESOACSum(TCommDataPacket buf);

extern void fruart1_init(void);
extern void USART_1_listADD(TCommDataPacket buf);

extern void fruart0_init(void);
extern void USART_0_listADD(TCommDataPacket buf);
extern void uart0_recv_timer_func(void *arg);

extern void fros_uarttmr_INIT(void);

#endif
