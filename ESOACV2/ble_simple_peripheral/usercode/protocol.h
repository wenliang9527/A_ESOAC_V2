/* ESOAC 协议：帧格式、命令码、组帧/解析/分发（BLE、MQTT、UART）
 *
 * 校验：checksum 为 8 位累加和（非 CRC）。帧内 command 为小端；CMD_RESPONSE
 * 载荷中的原命令字为大端；心跳 NOTIFY 计数器为大端（见功能说明 §3.1.7）。
 *
 * data_length 为 uint8，且 data_length = 3 + N（N 为业务数据长度），故 N 最大为
 * 252，与 MAX_DATA_LENGTH 一致，避免 N=254 时 3+N 溢出 8 位。
 */

#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "aircondata.h"

/* 与 protocol_send_buffer 一致 */
#define PROTOCOL_SRC_BLE   0u
#define PROTOCOL_SRC_MQTT  1u
#define PROTOCOL_SRC_UART  2u

#define FRAME_HEADER_1      0x5A
#define FRAME_HEADER_2      0x7A
#define MAX_DATA_LENGTH     252
/* 总长度 = 2+1+(1+2+N)+1 = N+7 */
#define FRAME_MAX_SIZE      (MAX_DATA_LENGTH + 7)

typedef enum {
    CMD_HEARTBEAT = 0x0001,
    CMD_DEVICE_INFO = 0x0002,
    CMD_SET_POWER = 0x0101,
    CMD_SET_MODE = 0x0102,
    CMD_SET_TEMP = 0x0103,
    CMD_SET_WIND = 0x0104,
    CMD_GET_STATUS = 0x0201,
    CMD_GET_POWER = 0x0202,
    CMD_GET_TEMP = 0x0203,
    CMD_IR_LEARN_START = 0x0301,
    CMD_IR_LEARN_STOP = 0x0302,
    CMD_IR_SEND = 0x0303,
    CMD_IR_LEARN_RESULT = 0x0304, /* 预留：protocol_process_frame 未实现 */
    CMD_SET_TIMER = 0x0401,       /* 预留 */
    CMD_GET_TIMER = 0x0402,       /* 预留 */
    /* 载荷 [len][name]；经 UART1 / MQTT / BLE 写 */
    CMD_SET_BLE_NAME = 0x0501,
    CMD_GET_BLE_NAME = 0x0502,
    CMD_SET_MQTT_CONFIG = 0x0503,
    CMD_GET_MQTT_CONFIG = 0x0504,
    CMD_SYNC_TIME = 0x0204,
    CMD_GET_ADC_DATA = 0x0205,
    CMD_IR_READ_DATA = 0x0305,
    CMD_IR_SAVE_KEYS = 0x0306,
    CMD_OTA_START = 0x0601, /* 预留 */
    CMD_OTA_DATA = 0x0602,  /* 预留 */
    CMD_OTA_END = 0x0603,   /* 预留 */
    CMD_RESPONSE = 0x8000,
} protocol_cmd_t;

typedef enum {
    DATA_MARK_REQUEST = 0x00,
    DATA_MARK_RESPONSE = 0x01,
    DATA_MARK_NOTIFY = 0x02,
    DATA_MARK_ERROR = 0xFF
} data_mark_t;

typedef struct {
    uint8_t frame_header[2];
    uint8_t data_length;
    uint8_t data_mark;
    uint16_t command;
    uint8_t data[MAX_DATA_LENGTH];
    uint8_t checksum;
} protocol_frame_t;

typedef enum {
    STATUS_SUCCESS = 0x00,
    STATUS_ERROR_PARAM = 0x01,
    STATUS_ERROR_CMD = 0x02,
    STATUS_ERROR_CHECKSUM = 0x03, /* 8 位累加和校验失败（非 CRC） */
    STATUS_ERROR_BUSY = 0x04,
    STATUS_ERROR_STORAGE = 0x05,
    STATUS_ERROR_FAIL = 0xFF
} response_status_t;

/* 旧名兼容（线值仍为 0x03） */
#ifndef STATUS_ERROR_CRC
#define STATUS_ERROR_CRC STATUS_ERROR_CHECKSUM
#endif

void protocol_init(void);
uint8_t protocol_calc_checksum(protocol_frame_t *frame);
bool protocol_verify_checksum(protocol_frame_t *frame);
bool protocol_build_frame(protocol_frame_t *frame, uint16_t command,
                          uint8_t data_mark, uint8_t *data, uint8_t data_len);
bool protocol_parse_frame(uint8_t *buffer, uint16_t len, protocol_frame_t *frame);
/* source：PROTOCOL_SRC_BLE / _MQTT / _UART */
bool protocol_process_frame(protocol_frame_t *frame, uint8_t source);
void protocol_send_heartbeat(uint8_t source);
void protocol_send_device_info(uint8_t source);
void protocol_send_status(uint8_t source);
void protocol_send_power(uint8_t source);
void protocol_send_temperature(uint8_t source);
/* GET_ADC：4B 小端 uint16 PD4 + uint16 PD5 原始值 */
void protocol_send_adc_raw(uint8_t source);
void protocol_send_response(uint16_t command, response_status_t status, uint8_t source);
void protocol_send_ble_name(uint8_t source);

#endif
