/* UART0��ML307 AT / MQTT URC��UART1��������Э��֡�����η��Ͷ��� + ���տ��ж�ʱ��֡ */

#include "frusart.h"
#include "frATcode.h"
#include "protocol.h"
#include "mqtt_handler.h"
#include "app_task.h"

#define frUSART0   1
#define frUSART1   1


os_timer_t uart0_send_timer;  /* UART0 ���͵��� */
os_timer_t uart0_recv_timer;  /* ���տ��г�ʱ���������� */
os_timer_t uart1_send_timer;
os_timer_t uart1_recv_timer;

/* Э��֡У��ͼ��㣨V2.2�汾��
 * ���㷶Χ��֡ͷ(2B) + data_length(1B) + data_mark(1B) + command(2B) + dev_addr(2B) + Data(N)
 * V2.2���������dev_addr�ֶ�(2�ֽ�)��data_length = 5 + N
 */
uint8_t ESOACSum(TCommDataPacket buf)
{
    uint8_t ByteSum = 0;
    uint16_t i;

    /* У��ͼ��㷶Χ����֡ͷ(ƫ��0)��У���ֽ�ǰ(ƫ��FramLen-1) */
    for (i = 0; i < buf.FramLen - 1; i++) {
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

    /* 0x04 �����ݣ�0x0c ��ʱ������ FIFO */
    if (int_id == 0x04 || int_id == 0x0c) {
        while (uart_reg_ram->lsr & 0x01) {
            ATUSART_0_RXbuf.FrameBuf[ATUSART_0_RXbuf.Point] = uart_reg_ram->u1.data;
            ATUSART_0_RXbuf.Point++;
            os_timer_start(&uart0_recv_timer, 5, false);  /* Լ 50ms ������Ϊһ֡���� */
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
            USART_0_LIST.W_Point = 0;  /* дָ�뻷�λ��� */
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
    (void)arg;
    uint16_t rx_len = ATUSART_0_RXbuf.Point;
    
    if (rx_len == 0) {
        return;
    }
    
    ATUSART_0_RXbuf.FramLen = rx_len;
    
    /* ============================================================================
     * �µ�URC�������� - ʹ��URC������
     * 1. ���Խ���ΪURC
     * 2. �����URC����������ɶ�ʱ���첽����
     * 3. �������URC����ΪAT��Ӧ����
     * ============================================================================ */
    
    urc_entry_t entry;
    if (urc_parse(ATUSART_0_RXbuf.FrameBuf, rx_len, &entry)) {
        // ��URC���������
        if (urc_queue_push(&R_atcommand.urc_queue, &entry)) {
            co_printf("URC queued: type=%d, queue_count=%d\r\n", 
                      entry.type, urc_queue_count(&R_atcommand.urc_queue));
        } else {
            co_printf("URC queue full, dropped type=%d\r\n", entry.type);
        }
    } else {
        // ����URC����ΪAT��Ӧ����
        R_atcommand.RECompleted = true;
        if (rx_len < sizeof(R_atcommand.REcmd_string)) {
            memcpy(R_atcommand.REcmd_string, ATUSART_0_RXbuf.FrameBuf, rx_len);
            R_atcommand.REcmd_string[rx_len] = '\0';
        } else {
            memcpy(R_atcommand.REcmd_string, ATUSART_0_RXbuf.FrameBuf, 
                   sizeof(R_atcommand.REcmd_string) - 1);
            R_atcommand.REcmd_string[sizeof(R_atcommand.REcmd_string) - 1] = '\0';
        }
    }
    
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

/* �� uart1_send_timer��10ms����ϣ����к����һ֡�������� MAXFRAMELEN Լ�� */
void uart1_recv_timer_func(void *arg)
{
    ATUSART_1_RXbuf.FramLen = ATUSART_1_RXbuf.Point;
    protocol_frame_t frame;
    if (protocol_parse_frame(ATUSART_1_RXbuf.FrameBuf, ATUSART_1_RXbuf.FramLen, &frame)) {
        protocol_process_frame(&frame, PROTOCOL_SRC_UART);  /* UART Դ */
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
