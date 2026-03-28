/* ESOAC Protocol V2.2: Frame format, command codes, frame assembly/parsing/distribution
 * (BLE, MQTT, UART unified binary frame with device short address)
 *
 * Checksum: 8-bit accumulation sum (not CRC). Frame command is little-endian;
 * CMD_RESPONSE payload original command is big-endian; heartbeat NOTIFY counter
 * is big-endian (see functional specification ¡ì3.1.7).
 *
 * V2.2 Changes:
 * - Added 2-byte device short address (dev_addr) field
 * - data_length = 5 + N (N is business data length), max N = 250
 * - Added CMD_SET_DEV_ADDR (0x0505) and CMD_GET_DEV_ADDR (0x0506)
 * - Added STATUS_ERROR_ADDR (0x06) for address mismatch
 * - Broadcast address: 0xFFFF
 */

#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "aircondata.h"

/* Protocol version */
#define PROTOCOL_VERSION_MAJOR  2
#define PROTOCOL_VERSION_MINOR  2

/* Source identifiers for protocol_send_buffer */
#define PROTOCOL_SRC_BLE   0u
#define PROTOCOL_SRC_MQTT  1u
#define PROTOCOL_SRC_UART  2u

/* Frame constants */
#define FRAME_HEADER_1      0x5A
#define FRAME_HEADER_2      0x7A

/* V2.2: data_length = 5 + N (data_mark(1) + command(2) + dev_addr(2) + Data(N))
 * Max N = 250 to ensure 5+N <= 255 (uint8 max) */
#define MAX_DATA_LENGTH     250

/* Total frame size = header(2) + data_length(1) + data_mark(1) + command(2) + dev_addr(2) + data(N) + checksum(1)
 * Max = 2 + 1 + 1 + 2 + 2 + 250 + 1 = 259 bytes */
#define FRAME_MAX_SIZE      (MAX_DATA_LENGTH + 9)

/* Broadcast address for V2.2 */
#define DEV_ADDR_BROADCAST  0xFFFF

/* Command codes (V2.2) */
typedef enum {
    /* Basic and heartbeat */
    CMD_HEARTBEAT = 0x0001,
    CMD_DEVICE_INFO = 0x0002,

    /* Air conditioner control */
    CMD_SET_POWER = 0x0101,
    CMD_SET_MODE = 0x0102,
    CMD_SET_TEMP = 0x0103,
    CMD_SET_WIND = 0x0104,

    /* Status and sensors */
    CMD_GET_STATUS = 0x0201,
    CMD_GET_POWER = 0x0202,
    CMD_GET_TEMP = 0x0203,
    CMD_SYNC_TIME = 0x0204,
    CMD_GET_ADC_DATA = 0x0205,

    /* Infrared */
    CMD_IR_LEARN_START = 0x0301,
    CMD_IR_LEARN_STOP = 0x0302,
    CMD_IR_SEND = 0x0303,
    CMD_IR_LEARN_RESULT = 0x0304, /* Reserved */
    CMD_IR_READ_DATA = 0x0305,
    CMD_IR_SAVE_KEYS = 0x0306,

    /* Timer (Reserved) */
    CMD_SET_TIMER = 0x0401,
    CMD_GET_TIMER = 0x0402,

    /* Network and device name */
    CMD_SET_BLE_NAME = 0x0501,
    CMD_GET_BLE_NAME = 0x0502,
    CMD_SET_MQTT_CONFIG = 0x0503,
    CMD_GET_MQTT_CONFIG = 0x0504,
    CMD_SET_DEV_ADDR = 0x0505,      /* V2.2: Set device short address */
    CMD_GET_DEV_ADDR = 0x0506,      /* V2.2: Get device short address */

    /* OTA (Reserved) */
    CMD_OTA_START = 0x0601,
    CMD_OTA_DATA = 0x0602,
    CMD_OTA_END = 0x0603,

    /* Response */
    CMD_RESPONSE = 0x8000,
} protocol_cmd_t;

/* Data mark */
typedef enum {
    DATA_MARK_REQUEST = 0x00,
    DATA_MARK_RESPONSE = 0x01,
    DATA_MARK_NOTIFY = 0x02,
    DATA_MARK_ERROR = 0xFF
} data_mark_t;

/* Protocol frame structure (V2.2) */
typedef struct {
    uint8_t frame_header[2];
    uint8_t data_length;
    uint8_t data_mark;
    uint16_t command;
    uint16_t dev_addr;              /* V2.2: Device short address (little-endian) */
    uint8_t data[MAX_DATA_LENGTH];
    uint8_t checksum;
} protocol_frame_t;

/* Response status codes (V2.2) */
typedef enum {
    STATUS_SUCCESS = 0x00,
    STATUS_ERROR_PARAM = 0x01,
    STATUS_ERROR_CMD = 0x02,
    STATUS_ERROR_CHECKSUM = 0x03,
    STATUS_ERROR_BUSY = 0x04,
    STATUS_ERROR_STORAGE = 0x05,
    STATUS_ERROR_ADDR = 0x06,       /* V2.2: Device address mismatch */
    STATUS_ERROR_FAIL = 0xFF
} response_status_t;

/* Legacy compatibility */
#ifndef STATUS_ERROR_CRC
#define STATUS_ERROR_CRC STATUS_ERROR_CHECKSUM
#endif

/* Function prototypes */
void protocol_init(void);
uint8_t protocol_calc_checksum(protocol_frame_t *frame);
bool protocol_verify_checksum(protocol_frame_t *frame);
bool protocol_build_frame(protocol_frame_t *frame, uint16_t command,
                          uint8_t data_mark, uint16_t dev_addr,
                          uint8_t *data, uint8_t data_len);
bool protocol_parse_frame(uint8_t *buffer, uint16_t len, protocol_frame_t *frame);

/* V2.2: Address filtering */
bool protocol_check_dev_addr(uint16_t dev_addr);
bool protocol_is_broadcast_addr(uint16_t dev_addr);

/* Frame processing */
bool protocol_process_frame(protocol_frame_t *frame, uint8_t source);

/* Transmission functions */
/* V2.2: Heartbeat sends power, temp, AC status via all channels (UART/BLE/MQTT) */
void protocol_send_heartbeat(void);
void protocol_send_device_info(uint8_t source);
void protocol_send_status(uint8_t source);
void protocol_send_power(uint8_t source);
void protocol_send_temperature(uint8_t source);
void protocol_send_adc_raw(uint8_t source);
void protocol_send_response(uint16_t command, response_status_t status, uint8_t source);
void protocol_send_ble_name(uint8_t source);
void protocol_send_dev_addr(uint8_t source);    /* V2.2 */

/* Device address management (V2.2) */
void dev_addr_init(void);
uint16_t dev_addr_get(void);
bool dev_addr_set(uint16_t addr);
bool dev_addr_save(void);
bool dev_addr_load(void);

#endif
