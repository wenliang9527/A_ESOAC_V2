/* UART0：ML307 AT / MQTT URC；UART1：二进制协议帧。环形发送队列 + 接收空闲定时组帧 */

#include "frusart.h"
#include "frATcode.h"
#include "protocol.h"
#include "mqtt_handler.h"
#include "app_task.h"

#define frUSART0   1
#define frUSART1   1


os_timer_t uart0_send_timer;  /* UART0 发送调度 */
os_timer_t uart0_recv_timer;  /* 接收空闲超时，触发处理 */
os_timer_t uart1_send_timer;
os_timer_t uart1_recv_timer;

/* 协议帧校验和：从第 3 字节累加到校验字节前 */
uint8_t ESOACSum(TCommDataPacket buf)
{
  uint8_t i;
  uint8_t ByteSum = 0;
  for (i = 3; i < buf.FramLen - 1; i++) {
    ByteSum += buf.FrameBuf[i];
  }
  return ByteSum;
}
#if frUSART0
TCommDataPacket ATUSART_0_RXbuf;
TCommDataPacket ATUSART_0_TXbuf;
TCommRecBuf Usart0UsbHead;
TCommSentListname USART_0_LIST;

void fruart0_init(void)
{
    system_set_port_pull(GPIO_PA0 | GPIO_PA7, true);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_0, PORTA0_FUNC_UART0_RXD);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_7, PORTA7_FUNC_UART0_TXD);
    uart_init(UART0, BAUD_RATE_115200);
    NVIC_EnableIRQ(UART0_IRQn);
}

__attribute__((section("ram_code"))) void uart0_isr_ram(void)
{
    uint8_t int_id;
    volatile struct uart_reg_t *const uart_reg_ram = (volatile struct uart_reg_t *)UART0_BASE;
    int_id = uart_reg_ram->u3.iir.int_id;

    /* 0x04 有数据，0x0c 超时：均读 FIFO */
    if (int_id == 0x04 || int_id == 0x0c) {
        while (uart_reg_ram->lsr & 0x01) {
            ATUSART_0_RXbuf.FrameBuf[ATUSART_0_RXbuf.Point] = uart_reg_ram->u1.data;
            ATUSART_0_RXbuf.Point++;
            os_timer_start(&uart0_recv_timer, 5, false);  /* 约 50ms 空闲视为一帧结束 */
        }
    } else if (int_id == 0x06) {
        uart_reg_ram->lsr = uart_reg_ram->lsr;
    }
}

void USART_0_listADD(TCommDataPacket buf)
{
    if (USART_0_LIST.W_Point < MAXFRAMBUFFLEN) {
        USART_0_LIST.Field[USART_0_LIST.W_Point] = buf;
        USART_0_LIST.W_Point++;
        if (USART_0_LIST.W_Point >= MAXFRAMBUFFLEN) {
            USART_0_LIST.W_Point = 0;  /* 写指针环形回绕 */
        }
    }
}

void uart0_send_timer_func(void *arg)
{
    if (USART_0_LIST.W_Point != USART_0_LIST.R_Point) {
        uart_write(UART0, USART_0_LIST.Field[USART_0_LIST.R_Point].FrameBuf,
                   USART_0_LIST.Field[USART_0_LIST.R_Point].FramLen);
        USART_0_LIST.R_Point++;
        if (USART_0_LIST.R_Point >= MAXFRAMBUFFLEN) {
            USART_0_LIST.R_Point = 0;
        }
    }
}

void uart0_recv_timer_func(void *arg)
{
    uint16_t rx_len = ATUSART_0_RXbuf.Point;
    ATUSART_0_RXbuf.FramLen = rx_len;

    /* 先识别 MQTT URC，避免当普通 AT 应答处理 */
    if (rx_len > 0) {
        uint8_t tmp_null = ATUSART_0_RXbuf.FrameBuf[rx_len < MAXFRAMELEN ? rx_len : MAXFRAMELEN - 1];
        ATUSART_0_RXbuf.FrameBuf[rx_len < MAXFRAMELEN ? rx_len : MAXFRAMELEN - 1] = '\0';

        if (strstr((char *)ATUSART_0_RXbuf.FrameBuf, "+MQTTrecv:") != NULL) {
            ATUSART_0_RXbuf.FrameBuf[rx_len < MAXFRAMELEN ? rx_len : MAXFRAMELEN - 1] = tmp_null;

            char *recv_ptr = (char *)ATUSART_0_RXbuf.FrameBuf;
            char *urc_start = strstr(recv_ptr, "+MQTTrecv:");
            if (urc_start != NULL) {
                char *comma1 = strchr(urc_start + 10, ',');
                char *comma2 = NULL;
                uint16_t data_len = 0;

                if (comma1 != NULL) {
                    char *topic_start = comma1 + 1;
                    if (*topic_start == '"') {
                        topic_start++;
                        comma2 = strchr(topic_start, '"');
                        if (comma2 != NULL) {
                            comma2++;
                            if (*comma2 == ',') {
                                comma2++;
                            }
                        }
                    } else {
                        comma2 = strchr(topic_start, ',');
                        if (comma2 != NULL) {
                            comma2++;
                        }
                    }
                }

                if (comma2 != NULL) {
                    data_len = (uint16_t)atoi(comma2);
                }

                char *data_start = strstr(recv_ptr, "\r\n");
                if (data_start != NULL) {
                    data_start += 2;
                    uint16_t actual_data_offset = (uint16_t)(data_start - recv_ptr);
                    uint16_t actual_data_len = rx_len - actual_data_offset;

                    co_printf("URC MQTT recv, expect_len:%d, actual:%d\r\n", data_len, actual_data_len);

                    if (data_len > 0 && actual_data_len > 0) {
                        mqtt_handler_message_arrived(g_mqtt_config.subscribe_topic,
                                                     &ATUSART_0_RXbuf.FrameBuf[actual_data_offset],
                                                     actual_data_len > data_len ? data_len : actual_data_len);
                    }
                } else {
                    co_printf("URC MQTT recv header only\r\n");
                }
            }

            ATUSART_0_RXbuf.Point = 0;
            return;
        }

        if (strstr((char *)ATUSART_0_RXbuf.FrameBuf, "+MQTTclosed:") != NULL) {
            ATUSART_0_RXbuf.FrameBuf[rx_len < MAXFRAMELEN ? rx_len : MAXFRAMELEN - 1] = tmp_null;
            co_printf("URC MQTT closed\r\n");
            if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
                R_atcommand.MLinitflag = ML307A_Idle;
                app_task_send_event(APP_EVT_MQTT_DISCONNECTED, NULL, 0);
            }
            ATUSART_0_RXbuf.Point = 0;
            return;
        }

        ATUSART_0_RXbuf.FrameBuf[rx_len < MAXFRAMELEN ? rx_len : MAXFRAMELEN - 1] = tmp_null;
    }

    R_atcommand.RECompleted = true;
    memcpy(R_atcommand.REcmd_string, ATUSART_0_RXbuf.FrameBuf, ATUSART_0_RXbuf.FramLen);
    ATUSART_0_RXbuf.Point = 0;
}
#endif

#if frUSART1

TCommDataPacket ATUSART_1_RXbuf;
TCommDataPacket ATUSART_1_TXbuf;
TCommRecBuf Usart1UsbHead;
TCommSentListname USART_1_LIST;

void fruart1_init(void)
{
    system_set_port_pull(GPIO_PA2, true);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_2, PORTA2_FUNC_UART1_RXD);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_3, PORTA3_FUNC_UART1_TXD);
    uart_init(UART1, BAUD_RATE_115200);
    uart_param_t param = {
        .baud_rate = 115200,
        .data_bit_num = 8,
        .pari = 0,
        .stop_bit = 1,
    };
    uart_init1(UART1, param);
    NVIC_EnableIRQ(UART1_IRQn);
}

__attribute__((section("ram_code"))) void uart1_isr_ram(void)
{
    uint8_t int_id;
    volatile struct uart_reg_t *uart_reg = (volatile struct uart_reg_t *)UART1_BASE;
    int_id = uart_reg->u3.iir.int_id;
    if (int_id == 0x04 || int_id == 0x0c) {
        while (uart_reg->lsr & 0x01) {
            ATUSART_1_RXbuf.FrameBuf[ATUSART_1_RXbuf.Point] = uart_reg->u1.data;
            ATUSART_1_RXbuf.Point++;
            os_timer_start(&uart1_recv_timer, 5, false);
        }
    } else if (int_id == 0x06) {
        volatile uint32_t line_status = uart_reg->lsr;
        (void)line_status;
    }
}

void USART_1_listADD(TCommDataPacket buf)
{
    if (USART_1_LIST.W_Point < MAXFRAMBUFFLEN) {
        USART_1_LIST.Field[USART_1_LIST.W_Point] = buf;
        USART_1_LIST.W_Point++;
        if (USART_1_LIST.W_Point >= MAXFRAMBUFFLEN) {
            USART_1_LIST.W_Point = 0;
        }
    }
}

void uart1_send_timer_func(void *arg)
{
    if (USART_1_LIST.W_Point != USART_1_LIST.R_Point) {
        uart_write(UART1, USART_1_LIST.Field[USART_1_LIST.R_Point].FrameBuf,
                   USART_1_LIST.Field[USART_1_LIST.R_Point].FramLen);
        USART_1_LIST.R_Point++;
        if (USART_1_LIST.R_Point >= MAXFRAMBUFFLEN) {
            USART_1_LIST.R_Point = 0;
        }
    }
}

/* 与 uart1_send_timer（10ms）配合：空闲后解析一帧，长度受 MAXFRAMELEN 约束 */
void uart1_recv_timer_func(void *arg)
{
    ATUSART_1_RXbuf.FramLen = ATUSART_1_RXbuf.Point;
    protocol_frame_t frame;
    if (protocol_parse_frame(ATUSART_1_RXbuf.FrameBuf, ATUSART_1_RXbuf.FramLen, &frame)) {
        protocol_process_frame(&frame, 2);  /* UART 源 */
    }
    ATUSART_1_RXbuf.Point = 0;
}
#endif

void fros_uarttmr_INIT(void)
{
#if frUSART1
    os_timer_init(&uart1_recv_timer, uart1_recv_timer_func, NULL);
    os_timer_init(&uart1_send_timer, uart1_send_timer_func, NULL);
    os_timer_start(&uart1_send_timer, 10, true);
#endif
#if frUSART0
    os_timer_init(&uart0_recv_timer, uart0_recv_timer_func, NULL);
    os_timer_init(&uart0_send_timer, uart0_send_timer_func, NULL);
    os_timer_start(&uart0_send_timer, 10, true);
#endif
}
