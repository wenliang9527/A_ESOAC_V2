/* ESOAC Protocol V2.2 Implementation: Frame parsing, checksum, command dispatch
 * Supports BLE, MQTT, UART with unified V2.2 frame format (with dev_addr)
 */

#include "protocol.h"
#include <string.h>
#include "co_printf.h"
#include "frATcode.h"
#include "frIRConversion.h"
#include "frADC.h"
#include "ESAIRble_service.h"
#include "device_config.h"
#include "ble_simple_peripheral.h"
#include "app_task.h"
#include "gap_api.h"
#include "sys_utils.h"
#include "ll.h"
#include "jump_table.h"
#include "driver_efuse.h"
#include "frspi.h"

/* Protocol V2.2: Device short address storage */
static uint16_t g_dev_addr = 0;
static bool g_dev_addr_initialized = false;

/* Protocol V2.2: Flash storage address for device address */
#define DEV_ADDR_FLASH_ADDR     (4096 * 3)   /* Use Flash data area 3 */
#define DEV_ADDR_MAGIC          0x44455631   /* Magic "DEV1" */

typedef struct {
    uint32_t magic;
    uint16_t dev_addr;
    uint16_t reserved;
} dev_addr_storage_t;

/* Forward declarations */
static uint8_t internal_power_to_doc(AIRSwitchkey st);
static AIRSwitchkey doc_power_to_internal(uint8_t doc_power);
static AIRmodekey doc_mode_to_internal(uint8_t doc_mode);
static uint8_t internal_mode_to_doc(AIRmodekey mode);
static bool air_ir_send_key_and_wait(uint8_t keynumber, uint32_t timeout_ms);
static uint16_t protocol_frame_to_buffer(protocol_frame_t *frame, uint8_t *buffer);
static void protocol_send_buffer(uint8_t *buffer, uint16_t len, uint8_t source);
static bool protocol_build_and_send(uint16_t command, uint8_t data_mark,
                                     uint8_t *data, uint8_t data_len, uint8_t source);

/*=============================================================================
 * Device Address Management (V2.2)
 *===========================================================================*/

void dev_addr_init(void)
{
    if (g_dev_addr_initialized) {
        return;
    }

    /* Try to load from Flash first */
    if (!dev_addr_load()) {
        /* Use default: last 2 bytes of UID (little-endian) */
        struct chip_unique_id_t id_data;
        efuse_get_chip_unique_id(&id_data);
        /* UID[4] = low byte, UID[5] = high byte */
        g_dev_addr = (uint16_t)id_data.unique_id[4] | ((uint16_t)id_data.unique_id[5] << 8);
        co_printf("DEV_ADDR: Using default from UID: 0x%04X\r\n", g_dev_addr);
    } else {
        co_printf("DEV_ADDR: Loaded from Flash: 0x%04X\r\n", g_dev_addr);
    }

    g_dev_addr_initialized = true;
}

uint16_t dev_addr_get(void)
{
    if (!g_dev_addr_initialized) {
        dev_addr_init();
    }
    return g_dev_addr;
}

bool dev_addr_set(uint16_t addr)
{
    g_dev_addr = addr;
    co_printf("DEV_ADDR: Set to 0x%04X\r\n", g_dev_addr);
    return true;
}

bool dev_addr_save(void)
{
    if (!spi_flash_is_present()) {
        co_printf("DEV_ADDR: save skipped (no W25Q)\r\n");
        return false;
    }

    dev_addr_storage_t storage;
    storage.magic = DEV_ADDR_MAGIC;
    storage.dev_addr = g_dev_addr;
    storage.reserved = 0;

    uint32_t sector = DEV_ADDR_FLASH_ADDR / SPIF_SECTOR_SIZE;
    SpiFlash_Erase_Sector(sector);
    SpiFlash_Write((uint8_t *)&storage, DEV_ADDR_FLASH_ADDR, sizeof(storage));

    dev_addr_storage_t verify;
    SpiFlash_Read((uint8_t *)&verify, DEV_ADDR_FLASH_ADDR, sizeof(verify));

    if (verify.magic == DEV_ADDR_MAGIC && verify.dev_addr == g_dev_addr) {
        co_printf("DEV_ADDR: Saved to Flash: 0x%04X\r\n", g_dev_addr);
        return true;
    }

    co_printf("DEV_ADDR: Save to Flash FAILED\r\n");
    return false;
}

bool dev_addr_load(void)
{
    if (!spi_flash_is_present()) {
        return false;
    }

    dev_addr_storage_t storage;
    SpiFlash_Read((uint8_t *)&storage, DEV_ADDR_FLASH_ADDR, sizeof(storage));

    if (storage.magic == DEV_ADDR_MAGIC) {
        g_dev_addr = storage.dev_addr;
        return true;
    }

    return false;
}

/*=============================================================================
 * Address Filtering (V2.2)
 *===========================================================================*/

bool protocol_is_broadcast_addr(uint16_t dev_addr)
{
    return (dev_addr == DEV_ADDR_BROADCAST);
}

bool protocol_check_dev_addr(uint16_t dev_addr)
{
    /* Broadcast address is always accepted */
    if (protocol_is_broadcast_addr(dev_addr)) {
        return true;
    }

    /* Check against local address */
    return (dev_addr == dev_addr_get());
}

/*=============================================================================
 * Protocol Core Functions
 *===========================================================================*/

void protocol_init(void)
{
    dev_addr_init();
}

uint8_t protocol_calc_checksum(protocol_frame_t *frame)
{
    uint8_t checksum = 0;
    uint8_t data_len = 0;

    /* V2.2: data_length = 5 + N, N = data_length - 5 */
    if (frame->data_length >= 5) {
        data_len = frame->data_length - 5;
    }

    /* Frame header */
    checksum += frame->frame_header[0];
    checksum += frame->frame_header[1];
    checksum += frame->data_length;
    checksum += frame->data_mark;

    /* Command (little-endian) */
    checksum += (uint8_t)(frame->command & 0xFF);
    checksum += (uint8_t)((frame->command >> 8) & 0xFF);

    /* V2.2: Device address (little-endian) */
    checksum += (uint8_t)(frame->dev_addr & 0xFF);
    checksum += (uint8_t)((frame->dev_addr >> 8) & 0xFF);

    /* Data */
    for (uint16_t i = 0; i < data_len; i++) {
        checksum += frame->data[i];
    }

    return checksum;
}

bool protocol_verify_checksum(protocol_frame_t *frame)
{
    return (protocol_calc_checksum(frame) == frame->checksum);
}

bool protocol_build_frame(protocol_frame_t *frame, uint16_t command,
                          uint8_t data_mark, uint16_t dev_addr,
                          uint8_t *data, uint8_t data_len)
{
    if (data_len > MAX_DATA_LENGTH) {
        return false;
    }

    /* V2.2: data_length = 5 + N */
    frame->frame_header[0] = FRAME_HEADER_1;
    frame->frame_header[1] = FRAME_HEADER_2;
    frame->data_length = data_len + 5;
    frame->data_mark = data_mark;
    frame->command = command;
    frame->dev_addr = dev_addr;  /* V2.2 */

    if (data && data_len > 0) {
        memcpy(frame->data, data, data_len);
    } else {
        memset(frame->data, 0, data_len);
    }

    frame->checksum = protocol_calc_checksum(frame);
    return true;
}

bool protocol_parse_frame(uint8_t *buffer, uint16_t len, protocol_frame_t *frame)
{
    if (len < 9) {  /* V2.2: Minimum frame = 2+1+1+2+2+0+1 = 9 bytes */
        return false;
    }

    /* Find frame header 0x5A 0x7A */
    uint16_t i = 0;
    while (i < len - 1) {
        if (buffer[i] == FRAME_HEADER_1 && buffer[i + 1] == FRAME_HEADER_2) {
            break;
        }
        i++;
    }

    if (i >= len - 1) {
        return false;
    }

    /* V2.2: Whole frame = 4 + data_length */
    uint8_t frame_len = buffer[i + 2] + 4;
    if (i + frame_len > len) {
        return false;
    }

    /* Parse fields */
    frame->frame_header[0] = buffer[i];
    frame->frame_header[1] = buffer[i + 1];
    frame->data_length = buffer[i + 2];
    frame->data_mark = buffer[i + 3];
    frame->command = (uint16_t)buffer[i + 5] << 8 | buffer[i + 4];

    /* V2.2: Parse dev_addr (little-endian) */
    frame->dev_addr = (uint16_t)buffer[i + 7] << 8 | buffer[i + 6];

    /* V2.2: Calculate data length N = data_length - 5 */
    uint8_t data_len = 0;
    if (frame->data_length >= 5) {
        data_len = frame->data_length - 5;
    }

    if (data_len > 0 && data_len <= MAX_DATA_LENGTH) {
        memcpy(frame->data, &buffer[i + 8], data_len);
    } else {
        memset(frame->data, 0, MAX_DATA_LENGTH);
    }

    frame->checksum = buffer[i + 8 + data_len];

    if (!protocol_verify_checksum(frame)) {
        co_printf("Protocol: Checksum error\r\n");
        return false;
    }

    return true;
}

/*=============================================================================
 * Frame Conversion and Transmission
 *===========================================================================*/

static uint16_t protocol_frame_to_buffer(protocol_frame_t *frame, uint8_t *buffer)
{
    uint16_t len = 0;
    uint8_t data_len = 0;

    /* V2.2: Calculate data length */
    if (frame->data_length >= 5) {
        data_len = frame->data_length - 5;
    }

    buffer[len++] = frame->frame_header[0];
    buffer[len++] = frame->frame_header[1];
    buffer[len++] = frame->data_length;
    buffer[len++] = frame->data_mark;

    /* Command (little-endian) */
    buffer[len++] = frame->command & 0xFF;
    buffer[len++] = (frame->command >> 8) & 0xFF;

    /* V2.2: Device address (little-endian) */
    buffer[len++] = frame->dev_addr & 0xFF;
    buffer[len++] = (frame->dev_addr >> 8) & 0xFF;

    /* Data */
    if (data_len > 0) {
        memcpy(&buffer[len], frame->data, data_len);
        len += data_len;
    }

    buffer[len++] = frame->checksum;
    return len;
}

static void protocol_send_buffer(uint8_t *buffer, uint16_t len, uint8_t source)
{
    if (source == PROTOCOL_SRC_BLE) {
        ESAIR_gatt_report_notify(0, buffer, len);
    } else if (source == PROTOCOL_SRC_MQTT) {
        if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
            bool ret = ML307A_MQTTPublish(g_mqtt_config.publish_topic, buffer, len);
            if (!ret) {
                co_printf("ERR: MQTT publish failed\r\n");
            }
        } else {
            co_printf("WARN: MQTT not connected, skip publish\r\n");
        }
    } else if (source == PROTOCOL_SRC_UART) {
        uart_write(UART1, buffer, len);
    }
}

static bool protocol_build_and_send(uint16_t command, uint8_t data_mark,
                                     uint8_t *data, uint8_t data_len, uint8_t source)
{
    protocol_frame_t frame;
    uint16_t dev_addr = dev_addr_get();  /* V2.2: Use local device address */

    if (!protocol_build_frame(&frame, command, data_mark, dev_addr, data, data_len)) {
        return false;
    }

    uint8_t buffer[FRAME_MAX_SIZE];
    uint16_t buf_len = protocol_frame_to_buffer(&frame, buffer);
    protocol_send_buffer(buffer, buf_len, source);
    return true;
}

/*=============================================================================
 * Command Processing
 *===========================================================================*/

bool protocol_process_frame(protocol_frame_t *frame, uint8_t source)
{
    if (frame == NULL) {
        return false;
    }

    /* V2.2: Address filtering for request frames */
    if (frame->data_mark == DATA_MARK_REQUEST) {
        if (!protocol_check_dev_addr(frame->dev_addr)) {
            co_printf("Protocol: Address mismatch, rx=0x%04X, local=0x%04X\r\n",
                      frame->dev_addr, dev_addr_get());
            protocol_send_response(frame->command, STATUS_ERROR_ADDR, source);
            return false;
        }
    }

    uint16_t cmd = frame->command;
    uint8_t payload_len = 0;

    /* V2.2: Calculate payload length N = data_length - 5 */
    if (frame->data_length >= 5) {
        payload_len = frame->data_length - 5;
    }

    switch (cmd) {
        case CMD_HEARTBEAT:
            protocol_send_response(CMD_HEARTBEAT, STATUS_SUCCESS, source);
            break;

        case CMD_DEVICE_INFO:
            protocol_send_device_info(source);
            break;

        /* Air conditioner control */
        case CMD_SET_POWER:
            if (payload_len >= 1) {
                if (frame->data[0] > 1) {
                    protocol_send_response(CMD_SET_POWER, STATUS_ERROR_PARAM, source);
                    break;
                }
                AIRSwitchkey desired = doc_power_to_internal(frame->data[0]);

                if (desired != ESAirdata.AIRStatus) {
                    if (!ESairkey.keyExistence[0]) {
                        protocol_send_response(CMD_SET_POWER, STATUS_ERROR_FAIL, source);
                        break;
                    }
                    if (!air_ir_send_key_and_wait(0, 2000)) {
                        protocol_send_response(CMD_SET_POWER, STATUS_ERROR_BUSY, source);
                        break;
                    }
                }

                ESAirdata.AIRStatus = desired;
                protocol_send_response(CMD_SET_POWER, STATUS_SUCCESS, source);
            } else {
                protocol_send_response(CMD_SET_POWER, STATUS_ERROR_PARAM, source);
            }
            break;

        case CMD_SET_MODE:
            if (payload_len >= 1 && frame->data[0] <= 5) {
                uint8_t target_doc_mode = frame->data[0];
                AIRmodekey desired = doc_mode_to_internal(target_doc_mode);

                if (desired != ESAirdata.AIRMODE) {
                    if (!ESairkey.keyExistence[1]) {
                        protocol_send_response(CMD_SET_MODE, STATUS_ERROR_FAIL, source);
                        break;
                    }

                    uint8_t cur_doc_mode = internal_mode_to_doc(ESAirdata.AIRMODE);
                    uint8_t steps = (target_doc_mode + 6 - cur_doc_mode) % 6;

                    for (uint8_t i = 0; i < steps; i++) {
                        if (!air_ir_send_key_and_wait(1, 2000)) {
                            protocol_send_response(CMD_SET_MODE, STATUS_ERROR_BUSY, source);
                            return false;
                        }
                        cur_doc_mode = (cur_doc_mode + 1) % 6;
                        ESAirdata.AIRMODE = doc_mode_to_internal(cur_doc_mode);
                    }
                }

                ESAirdata.AIRMODE = desired;
                protocol_send_response(CMD_SET_MODE, STATUS_SUCCESS, source);
            } else {
                protocol_send_response(CMD_SET_MODE, STATUS_ERROR_PARAM, source);
            }
            break;

        case CMD_SET_TEMP:
            if (payload_len >= 1) {
                uint8_t temp = frame->data[0];
                if (temp >= 16 && temp <= 30) {
                    if (temp != ESAirdata.AIRTemperature) {
                        int16_t diff = (int16_t)temp - (int16_t)ESAirdata.AIRTemperature;
                        if (diff > 0) {
                            if (!ESairkey.keyExistence[2]) {
                                protocol_send_response(CMD_SET_TEMP, STATUS_ERROR_FAIL, source);
                                break;
                            }
                            while (diff-- > 0) {
                                if (!air_ir_send_key_and_wait(2, 2000)) {
                                    protocol_send_response(CMD_SET_TEMP, STATUS_ERROR_BUSY, source);
                                    return false;
                                }
                                ESAirdata.AIRTemperature++;
                            }
                        } else {
                            if (!ESairkey.keyExistence[3]) {
                                protocol_send_response(CMD_SET_TEMP, STATUS_ERROR_FAIL, source);
                                break;
                            }
                            while (diff++ < 0) {
                                if (!air_ir_send_key_and_wait(3, 2000)) {
                                    protocol_send_response(CMD_SET_TEMP, STATUS_ERROR_BUSY, source);
                                    return false;
                                }
                                ESAirdata.AIRTemperature--;
                            }
                        }
                    }
                    protocol_send_response(CMD_SET_TEMP, STATUS_SUCCESS, source);
                } else {
                    protocol_send_response(CMD_SET_TEMP, STATUS_ERROR_PARAM, source);
                }
            } else {
                protocol_send_response(CMD_SET_TEMP, STATUS_ERROR_PARAM, source);
            }
            break;

        case CMD_SET_WIND:
            if (payload_len >= 1 && frame->data[0] <= airws_max) {
                uint8_t target_wind = frame->data[0];
                if (target_wind != ESAirdata.AIRWindspeed) {
                    if (!ESairkey.keyExistence[4]) {
                        protocol_send_response(CMD_SET_WIND, STATUS_ERROR_FAIL, source);
                        break;
                    }

                    uint8_t cur = ESAirdata.AIRWindspeed;
                    uint8_t iter = 0;
                    while (cur != target_wind && iter < 3) {
                        if (!air_ir_send_key_and_wait(4, 2000)) {
                            protocol_send_response(CMD_SET_WIND, STATUS_ERROR_BUSY, source);
                            return false;
                        }
                        cur = (cur + 1) % 3;
                        ESAirdata.AIRWindspeed = cur;
                        iter++;
                    }
                    ESAirdata.AIRWindspeed = target_wind;
                }
                protocol_send_response(CMD_SET_WIND, STATUS_SUCCESS, source);
            } else {
                protocol_send_response(CMD_SET_WIND, STATUS_ERROR_PARAM, source);
            }
            break;

        /* Status queries */
        case CMD_GET_STATUS:
            protocol_send_status(source);
            break;

        case CMD_GET_POWER:
            protocol_send_power(source);
            break;

        case CMD_GET_TEMP:
            protocol_send_temperature(source);
            break;

        case CMD_GET_ADC_DATA:
            fr_ADC_send();
            protocol_send_adc_raw(source);
            break;

        /* Infrared */
        case CMD_IR_LEARN_START:
            if (payload_len >= 1 && frame->data[0] < AIRkeyNumber) {
                if (!ESairkey.EIRlearnStatus) {
                    ESOAAIR_IRskeystudy(frame->data[0]);
                    protocol_send_response(CMD_IR_LEARN_START, STATUS_SUCCESS, source);
                } else {
                    protocol_send_response(CMD_IR_LEARN_START, STATUS_ERROR_BUSY, source);
                }
            } else {
                protocol_send_response(CMD_IR_LEARN_START, STATUS_ERROR_PARAM, source);
            }
            break;

        case CMD_IR_LEARN_STOP:
            IR_stop_learn();
            protocol_send_response(CMD_IR_LEARN_STOP, STATUS_SUCCESS, source);
            break;

        case CMD_IR_SEND:
            if (payload_len >= 1 && frame->data[0] < AIRkeyNumber) {
                ESOAAIR_IRsend(frame->data[0]);
                protocol_send_response(CMD_IR_SEND, STATUS_SUCCESS, source);
            } else {
                protocol_send_response(CMD_IR_SEND, STATUS_ERROR_PARAM, source);
            }
            break;

        case CMD_IR_READ_DATA:
            if (payload_len >= 1 && frame->data[0] < AIRkeyNumber) {
                uint8_t keynumber = frame->data[0];
                TYPEDEFIRLEARNDATA learn_copy;

                GLOBAL_INT_DISABLE();
                memcpy(&learn_copy, &ESairkey.airbutton[keynumber], sizeof(learn_copy));
                GLOBAL_INT_RESTORE();

                if ((learn_copy.IR_learn_state & BIT(0)) == 0) {
                    protocol_send_response(CMD_IR_READ_DATA, STATUS_ERROR_FAIL, source);
                    break;
                }

                uint8_t payload[MAX_DATA_LENGTH];
                memset(payload, 0, sizeof(payload));

                payload[0] = learn_copy.IR_learn_state;

                uint32_t carrier = learn_copy.ir_carrier_fre;
                payload[1] = (uint8_t)(carrier & 0xFF);
                payload[2] = (uint8_t)((carrier >> 8) & 0xFF);
                payload[3] = (uint8_t)((carrier >> 16) & 0xFF);
                payload[4] = (uint8_t)((carrier >> 24) & 0xFF);

                payload[5] = learn_copy.ir_learn_data_cnt;

                uint8_t max_pulses = (MAX_DATA_LENGTH - 6) / 4;
                uint8_t pulses = learn_copy.ir_learn_data_cnt;
                if (pulses > max_pulses) {
                    pulses = max_pulses;
                }

                uint16_t pos = 6;
                for (uint8_t i = 0; i < pulses; i++) {
                    uint32_t v = learn_copy.ir_learn_Date[i];
                    payload[pos++] = (uint8_t)(v & 0xFF);
                    payload[pos++] = (uint8_t)((v >> 8) & 0xFF);
                    payload[pos++] = (uint8_t)((v >> 16) & 0xFF);
                    payload[pos++] = (uint8_t)((v >> 24) & 0xFF);
                }

                protocol_build_and_send(CMD_IR_READ_DATA, DATA_MARK_RESPONSE, payload, (uint8_t)pos, source);
            } else {
                protocol_send_response(CMD_IR_READ_DATA, STATUS_ERROR_PARAM, source);
            }
            break;

        case CMD_IR_SAVE_KEYS:
            ESOAAIR_Savekey();
            protocol_send_response(CMD_IR_SAVE_KEYS, STATUS_SUCCESS, source);
            break;

        /* Device configuration */
        case CMD_SET_BLE_NAME:
            {
                if (payload_len < 1) {
                    protocol_send_response(CMD_SET_BLE_NAME, STATUS_ERROR_PARAM, source);
                    break;
                }
                uint8_t name_len = frame->data[0];
                uint8_t avail_data = payload_len;
                if (name_len == 0 || name_len > avail_data - 1) {
                    protocol_send_response(CMD_SET_BLE_NAME, STATUS_ERROR_PARAM, source);
                    break;
                }
                char *name = (char *)&frame->data[1];

                int ret = ble_update_device_name(name, name_len);
                if (ret == 0) {
                    protocol_send_response(CMD_SET_BLE_NAME, STATUS_SUCCESS, source);
                } else {
                    response_status_t err = STATUS_ERROR_FAIL;
                    if (ret == -2) {
                        err = STATUS_ERROR_PARAM;
                    } else if (ret == -3) {
                        err = STATUS_ERROR_STORAGE;
                    }
                    protocol_send_response(CMD_SET_BLE_NAME, err, source);
                }
            }
            break;

        case CMD_GET_BLE_NAME:
            protocol_send_ble_name(source);
            break;

        /* V2.2: Device address commands */
        case CMD_SET_DEV_ADDR:
            {
                if (payload_len >= 2) {
                    uint16_t new_addr = (uint16_t)frame->data[0] | ((uint16_t)frame->data[1] << 8);
                    dev_addr_set(new_addr);
                    if (dev_addr_save()) {
                        protocol_send_response(CMD_SET_DEV_ADDR, STATUS_SUCCESS, source);
                    } else {
                        protocol_send_response(CMD_SET_DEV_ADDR, STATUS_ERROR_STORAGE, source);
                    }
                } else {
                    protocol_send_response(CMD_SET_DEV_ADDR, STATUS_ERROR_PARAM, source);
                }
            }
            break;

        case CMD_GET_DEV_ADDR:
            protocol_send_dev_addr(source);
            break;

        case CMD_SET_MQTT_CONFIG:
            {
                /* V2.2: MQTT配置TLV解析与保存
                 * TLV格式: [type(1B)][len(1B)][value(len)]...
                 * type: 0=server_addr, 1=server_port, 2=client_id, 3=username, 4=password, 5=subscribe_topic, 6=publish_topic
                 */
                if (payload_len < 2) {
                    protocol_send_response(CMD_SET_MQTT_CONFIG, STATUS_ERROR_PARAM, source);
                    break;
                }

                uint8_t *cfg_data = &frame->data[0];
                uint16_t pos = 0;
                bool any_field_valid = false;
                bool parse_error = false;
                bool updated[7] = {0};

                /* 临时缓冲 */
                char new_addr[128] = {0};
                char new_port[6] = {0};
                char new_cid[128] = {0};
                char new_user[128] = {0};
                char new_pass[128] = {0};
                char new_sub[128] = {0};
                char new_pub[128] = {0};

                while (pos + 2 <= payload_len) {
                    uint8_t type = cfg_data[pos];
                    uint8_t len = cfg_data[pos + 1];

                    if (pos + 2 + len > payload_len) {
                        parse_error = true;
                        break;
                    }

                    char *value = (char *)&cfg_data[pos + 2];

                    switch (type) {
                        case 0: /* server_addr */
                            if (len > 0 && len < sizeof(new_addr)) {
                                memcpy(new_addr, value, len);
                                new_addr[len] = '\0';
                                any_field_valid = true;
                                updated[0] = true;
                            }
                            break;
                        case 1: /* server_port */
                            if (len > 0 && len < sizeof(new_port)) {
                                memcpy(new_port, value, len);
                                new_port[len] = '\0';
                                any_field_valid = true;
                                updated[1] = true;
                            }
                            break;
                        case 2: /* client_id */
                            if (len > 0 && len < sizeof(new_cid)) {
                                memcpy(new_cid, value, len);
                                new_cid[len] = '\0';
                                any_field_valid = true;
                                updated[2] = true;
                            }
                            break;
                        case 3: /* username */
                            if (len < sizeof(new_user)) {
                                if (len > 0) memcpy(new_user, value, len);
                                new_user[len] = '\0';
                                any_field_valid = true;
                                updated[3] = true;
                            }
                            break;
                        case 4: /* password */
                            if (len < sizeof(new_pass)) {
                                if (len > 0) memcpy(new_pass, value, len);
                                new_pass[len] = '\0';
                                any_field_valid = true;
                                updated[4] = true;
                            }
                            break;
                        case 5: /* subscribe_topic */
                            if (len > 0 && len < sizeof(new_sub)) {
                                memcpy(new_sub, value, len);
                                new_sub[len] = '\0';
                                any_field_valid = true;
                                updated[5] = true;
                            }
                            break;
                        case 6: /* publish_topic */
                            if (len > 0 && len < sizeof(new_pub)) {
                                memcpy(new_pub, value, len);
                                new_pub[len] = '\0';
                                any_field_valid = true;
                                updated[6] = true;
                            }
                            break;
                        default:
                            /* 未知type，跳过 */
                            break;
                    }

                    pos += 2 + len;
                }

                if (parse_error) {
                    protocol_send_response(CMD_SET_MQTT_CONFIG, STATUS_ERROR_PARAM, source);
                    break;
                }

                if (!any_field_valid) {
                    protocol_send_response(CMD_SET_MQTT_CONFIG, STATUS_ERROR_PARAM, source);
                    break;
                }

                /* 合并到g_mqtt_config */
                if (updated[0]) strncpy(g_mqtt_config.server_addr, new_addr, sizeof(g_mqtt_config.server_addr));
                if (updated[1]) strncpy(g_mqtt_config.server_port, new_port, sizeof(g_mqtt_config.server_port));
                if (updated[2]) strncpy(g_mqtt_config.client_id, new_cid, sizeof(g_mqtt_config.client_id));
                if (updated[3]) strncpy(g_mqtt_config.username, new_user, sizeof(g_mqtt_config.username));
                if (updated[4]) strncpy(g_mqtt_config.password, new_pass, sizeof(g_mqtt_config.password));
                if (updated[5]) strncpy(g_mqtt_config.subscribe_topic, new_sub, sizeof(g_mqtt_config.subscribe_topic));
                if (updated[6]) strncpy(g_mqtt_config.publish_topic, new_pub, sizeof(g_mqtt_config.publish_topic));

                /* 保存到Flash */
                if (mqtt_config_save()) {
                    co_printf("MQTT config updated & saved\r\n");
                    protocol_send_response(CMD_SET_MQTT_CONFIG, STATUS_SUCCESS, source);
                    /* 通知应用层重新连接MQTT */
                    app_task_send_event(APP_EVT_MQTT_CONFIG_UPDATED, NULL, 0);
                } else {
                    co_printf("MQTT config save FAILED\r\n");
                    protocol_send_response(CMD_SET_MQTT_CONFIG, STATUS_ERROR_STORAGE, source);
                }
            }
            break;

        case CMD_GET_MQTT_CONFIG:
            {
                /* V2.2: 构建MQTT配置TLV响应 */
                uint8_t cfg_buf[MAX_DATA_LENGTH];
                uint8_t cfg_len = 0;
                struct { uint8_t type; const char *str; } fields[] = {
                    {0, g_mqtt_config.server_addr},
                    {1, g_mqtt_config.server_port},
                    {2, g_mqtt_config.client_id},
                    {3, g_mqtt_config.username},
                    {4, g_mqtt_config.password},
                    {5, g_mqtt_config.subscribe_topic},
                    {6, g_mqtt_config.publish_topic},
                };

                for (int i = 0; i < 7 && cfg_len + 2 < sizeof(cfg_buf); i++) {
                    uint8_t slen = (uint8_t)strlen(fields[i].str);
                    cfg_buf[cfg_len++] = fields[i].type;
                    cfg_buf[cfg_len++] = slen;
                    if (cfg_len + slen > sizeof(cfg_buf)) slen = sizeof(cfg_buf) - cfg_len;
                    memcpy(&cfg_buf[cfg_len], fields[i].str, slen);
                    cfg_len += slen;
                }

                protocol_build_and_send(CMD_GET_MQTT_CONFIG, DATA_MARK_RESPONSE, cfg_buf, cfg_len, source);
            }
            break;

        case CMD_SYNC_TIME:
            {
                if (payload_len >= 8) {
                    ESAirdata.EStime.ES_Year = (frame->data[0] << 8) | frame->data[1];
                    ESAirdata.EStime.ES_Moon = frame->data[2];
                    ESAirdata.EStime.ES_Day = frame->data[3];
                    ESAirdata.EStime.ES_Hours = frame->data[4];
                    ESAirdata.EStime.ES_Minutes = frame->data[5];
                    ESAirdata.EStime.ES_Second = frame->data[6];
                    ESAirdata.EStime.ES_Week = frame->data[7];
                    protocol_send_response(CMD_SYNC_TIME, STATUS_SUCCESS, source);
                } else {
                    protocol_send_response(CMD_SYNC_TIME, STATUS_ERROR_PARAM, source);
                }
            }
            break;

        default:
            co_printf("Protocol: Unknown command: 0x%04X\r\n", cmd);
            protocol_send_response(cmd, STATUS_ERROR_CMD, source);
            return false;
    }

    return true;
}

/*=============================================================================
 * Transmission Functions
 *===========================================================================*/

/* V2.2: Heartbeat sends power, temp, AC status via all channels (UART/BLE/MQTT)
 * Data format (12 bytes):
 *   [0-3]: Power (float, IEEE754 little-endian)
 *   [4-7]: Temperature (float, IEEE754 little-endian)
 *   [8]: Power status (0=OFF, 1=ON)
 *   [9]: Mode (0=Auto, 1=Cold, 2=Hot, 3=Dry, 4=Fan, 5=Sleep)
 *   [10]: Set temperature (16-30)
 *   [11]: Wind speed (0-2)
 */
void protocol_send_heartbeat(void)
{
    uint8_t data[12];
    float power = ESAirdata.AIRpowervalue;
    float temp = ESAirdata.temp_celsius;

    /* Build heartbeat data */
    memcpy(&data[0], &power, 4);           /* Power (float, little-endian) */
    memcpy(&data[4], &temp, 4);            /* Temperature (float, little-endian) */
    data[8] = (ESAirdata.AIRStatus == airSW_ON) ? 1 : 0;  /* Power status */
    data[9] = internal_mode_to_doc(ESAirdata.AIRMODE);    /* Mode */
    data[10] = ESAirdata.AIRTemperature;   /* Set temperature */
    data[11] = ESAirdata.AIRWindspeed;     /* Wind speed */

    /* UART debug print - use integer to avoid float printf issues */
    const char *mode_str;
    switch (data[9]) {
        case 0: mode_str = "Auto"; break;
        case 1: mode_str = "Cold"; break;
        case 2: mode_str = "Hot"; break;
        case 3: mode_str = "Dry"; break;
        case 4: mode_str = "Fan"; break;
        case 5: mode_str = "Sleep"; break;
        default: mode_str = "Unknown"; break;
    }
    int power_int = (int)power;
    int power_dec = (int)((power - power_int) * 10);
    int temp_int = (int)temp;
    int temp_dec = (int)((temp - temp_int) * 10);
    co_printf("[Heartbeat] Power:%d.%dW Temp:%d.%dC Status:%s Mode:%s SetTemp:%d Wind:%d\r\n",
              power_int, power_dec, temp_int, temp_dec,
              data[8] ? "ON" : "OFF",
              mode_str,
              data[10], data[11]);

    /* Build and print protocol frame */
    protocol_frame_t frame;
    uint16_t dev_addr = dev_addr_get();
    protocol_build_frame(&frame, CMD_HEARTBEAT, DATA_MARK_NOTIFY, dev_addr, data, 12);

    /* Print protocol frame data */
    co_printf("[HB Frame] ");
    uint8_t frame_buf[FRAME_MAX_SIZE];
    uint16_t frame_len = 0;
    frame_buf[frame_len++] = frame.frame_header[0];
    frame_buf[frame_len++] = frame.frame_header[1];
    frame_buf[frame_len++] = frame.data_length;
    frame_buf[frame_len++] = frame.data_mark;
    frame_buf[frame_len++] = frame.command & 0xFF;
    frame_buf[frame_len++] = (frame.command >> 8) & 0xFF;
    frame_buf[frame_len++] = frame.dev_addr & 0xFF;
    frame_buf[frame_len++] = (frame.dev_addr >> 8) & 0xFF;
    for (uint8_t i = 0; i < 12; i++) {
        frame_buf[frame_len++] = data[i];
    }
    frame_buf[frame_len++] = frame.checksum;

    for (uint16_t i = 0; i < frame_len; i++) {
        co_printf("%02X ", frame_buf[i]);
    }
    co_printf("\r\n");

    /* Send via UART */
    protocol_send_buffer(frame_buf, frame_len, PROTOCOL_SRC_UART);

    /* Send via BLE */
    protocol_send_buffer(frame_buf, frame_len, PROTOCOL_SRC_BLE);

    /* Send via MQTT (only if connected) */
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        protocol_send_buffer(frame_buf, frame_len, PROTOCOL_SRC_MQTT);
    }
}

void protocol_send_device_info(uint8_t source)
{
    uint8_t data[14] = {0};

    /* 6B Device ID */
    memcpy(data, ESAirdata.MUConlyID, 6);
    /* 4B Software / 4B Hardware version placeholders */
    data[6] = 0x02;
    data[10] = 0x01;

    protocol_build_and_send(CMD_DEVICE_INFO, DATA_MARK_RESPONSE, data, 14, source);
}

void protocol_send_status(uint8_t source)
{
    uint8_t data[5];
    data[0] = internal_power_to_doc(ESAirdata.AIRStatus);
    data[1] = internal_mode_to_doc(ESAirdata.AIRMODE);
    data[2] = ESAirdata.AIRTemperature;
    data[3] = ESAirdata.AIRWindspeed;

    /* Connection status */
    uint8_t connected_any = 0;
    if (gap_get_connect_num() > 0) {
        connected_any = 1;
    }
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        connected_any = 1;
    }
    data[4] = connected_any;

    protocol_build_and_send(CMD_GET_STATUS, DATA_MARK_RESPONSE, data, 5, source);
}

void protocol_send_power(uint8_t source)
{
    float power = ESAirdata.AIRpowervalue;
    protocol_build_and_send(CMD_GET_POWER, DATA_MARK_RESPONSE,
                            (uint8_t *)&power, 4, source);
}

void protocol_send_temperature(uint8_t source)
{
    float temp = ESAirdata.temp_celsius;
    protocol_build_and_send(CMD_GET_TEMP, DATA_MARK_RESPONSE,
                            (uint8_t *)&temp, 4, source);
}

void protocol_send_adc_raw(uint8_t source)
{
    uint8_t d[4];
    uint16_t p = ESAirdata.AIRpowerADCvalue;
    uint16_t n = ESAirdata.AIRntcADCvalue;

    d[0] = (uint8_t)(p & 0xFF);
    d[1] = (uint8_t)((p >> 8) & 0xFF);
    d[2] = (uint8_t)(n & 0xFF);
    d[3] = (uint8_t)((n >> 8) & 0xFF);

    protocol_build_and_send(CMD_GET_ADC_DATA, DATA_MARK_RESPONSE, d, 4, source);
}

void protocol_send_ble_name(uint8_t source)
{
    uint8_t data[BLE_NAME_MAX_LEN + 1];
    const char *name = ble_get_device_name();
    uint8_t name_len = strlen(name);

    data[0] = name_len;
    memcpy(&data[1], name, name_len);

    protocol_build_and_send(CMD_GET_BLE_NAME, DATA_MARK_RESPONSE,
                            data, name_len + 1, source);
}

/* V2.2: Send device address */
void protocol_send_dev_addr(uint8_t source)
{
    uint8_t data[2];
    uint16_t addr = dev_addr_get();
    data[0] = addr & 0xFF;        /* Low byte */
    data[1] = (addr >> 8) & 0xFF; /* High byte */

    protocol_build_and_send(CMD_GET_DEV_ADDR, DATA_MARK_RESPONSE, data, 2, source);
}

void protocol_send_response(uint16_t command, response_status_t status, uint8_t source)
{
    uint8_t data[3];
    /* V2.2: Original command in big-endian */
    data[0] = (command >> 8) & 0xFF;
    data[1] = command & 0xFF;
    data[2] = (uint8_t)status;

    protocol_build_and_send(CMD_RESPONSE, DATA_MARK_RESPONSE, data, 3, source);
}

/*=============================================================================
 * Helper Functions
 *===========================================================================*/

static uint8_t internal_power_to_doc(AIRSwitchkey st)
{
    return (st == airSW_ON) ? 1 : 0;
}

static AIRSwitchkey doc_power_to_internal(uint8_t doc_power)
{
    return (doc_power == 1) ? airSW_ON : airSW_OFF;
}

static AIRmodekey doc_mode_to_internal(uint8_t doc_mode)
{
    static const AIRmodekey map[6] = {
        airmode_Auto,
        airmode_cold,
        airmode_hot,
        airmode_Dehumidification,
        airmode_Supply,
        airmode_Sleep
    };
    if (doc_mode > 5) {
        return airmode_Sleep;
    }
    return map[doc_mode];
}

static uint8_t internal_mode_to_doc(AIRmodekey mode)
{
    static const uint8_t map[6] = {4, 0, 1, 2, 3, 5};
    if (mode > airmode_Sleep) {
        return 0;
    }
    return map[mode];
}

static bool air_ir_send_key_and_wait(uint8_t keynumber, uint32_t timeout_ms)
{
    ESOAAIR_IRsend(keynumber);

    uint32_t elapsed_us = 0;
    const uint32_t step_us = 5000;
    const uint32_t timeout_us = timeout_ms * 1000;

    while (IR_PWM_TIM.IR_Busy && elapsed_us < timeout_us) {
        co_delay_100us(50);
        elapsed_us += step_us;
    }

    return (IR_PWM_TIM.IR_Busy == 0);
}
