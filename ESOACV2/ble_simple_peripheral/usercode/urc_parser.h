/* URC������ͷ�ļ� - ͳһ����ML307Aģ��ķ���������
 * ���Ŀ�꣺
 * 1. ͳһURC������ڣ����������ظ�
 * 2. ֧���첽��������������UART����
 * 3. ������չ������URC����ֻ������ǰ׺�ʹ�����
 */

#ifndef _URC_PARSER_H
#define _URC_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "co_printf.h"

/* ============================================================================
 * �������� - �Ż��ڴ�ռ��
 * ============================================================================ */

#define URC_QUEUE_SIZE          4       // URC������ȣ������ڴ�ռ�ã�
#define URC_DATA_MAX_LEN        128     // ����URC������󳤶ȣ������ڴ棩
#define URC_TOPIC_MAX_LEN       64      // �����ַ�����󳤶ȣ������ڴ棩
#define URC_PREFIX_TABLE_SIZE   8       // URCǰ׺����С

/* ============================================================================
 * URC����ö��
 * ============================================================================ */

typedef enum {
    URC_NONE = 0,
    URC_MQTT_RECV,         // +MQTTrecv:...  �յ�MQTT����
    URC_MQTT_CLOSED,       // +MQTTclosed:   MQTT���ӶϿ�
    URC_MQTT_SUB_OK,       // +MQTTsub:      ���ĳɹ�
    URC_MQTT_UNSUB_OK,     // +MQTTunsub:    �˶��ɹ�
    URC_MQTT_PUB_OK,       // +MQTTpub:      �����ɹ�
    URC_SIM_READY,         // +CPIN: READY   SIM������
    URC_NET_REGISTERED,    // +CREG: 0,1     ����ע��ɹ�
    URC_OTHER              // ����δ����URC
} urc_type_t;

/* ============================================================================
 * URC��Ŀ�ṹ - �����еĵ���URC���Ż��ڴ沼�֣�
 * ============================================================================ */

typedef struct {
    uint16_t data_len;                  // ���ݳ���
    urc_type_t type;                    // URC����
    char topic[URC_TOPIC_MAX_LEN];      // ������������ (URC_MQTT_RECV��)
    uint8_t data[URC_DATA_MAX_LEN];     // URC��������
} urc_entry_t;

/* ============================================================================
 * URC���ζ���
 * ============================================================================ */

typedef struct {
    uint8_t head;                        // д��λ�� (��һ��д��λ��)
    uint8_t tail;                        // ��ȡλ�� (��һ����ȡλ��)
    uint8_t count;                       // ��ǰ��Ŀ��
    urc_entry_t entries[URC_QUEUE_SIZE]; // ������Ŀ����
} urc_queue_t;

/* ============================================================================
 * URCǰ׺����Ŀ - ����ʶ��URC����
 * ============================================================================ */

typedef struct {
    const char *prefix;     // URCǰ׺�ַ���
    urc_type_t type;        // ��Ӧ��URC����
    bool has_data;          // �Ƿ�������������� (��+MQTTrecv)
    bool has_topic;         // �Ƿ���������ֶ�
} urc_prefix_entry_t;

/* ============================================================================
 * ��������
 * ============================================================================ */

// ���в���
void urc_queue_init(urc_queue_t *queue);
bool urc_queue_push(urc_queue_t *queue, const urc_entry_t *entry);
bool urc_queue_pop(urc_queue_t *queue, urc_entry_t *entry);
bool urc_queue_is_empty(const urc_queue_t *queue);
bool urc_queue_is_full(const urc_queue_t *queue);
uint8_t urc_queue_count(const urc_queue_t *queue);
void urc_queue_clear(urc_queue_t *queue);

// URC����
bool urc_parse(uint8_t *buffer, uint16_t len, urc_entry_t *entry);
urc_type_t urc_identify(const uint8_t *buffer, uint16_t len);
bool urc_parse_mqtt_recv(uint8_t *buffer, uint16_t len, urc_entry_t *entry);

// URC�����ַ�
void urc_process_entry(const urc_entry_t *entry);
const char* urc_type_to_string(urc_type_t type);

#endif /* _URC_PARSER_H */
