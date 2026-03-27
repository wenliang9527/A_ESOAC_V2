/**
 * @file protocol.c
 * @brief ESOAC空调节能终端统一协议解析模块实现
 * @description 处理BLE/MQTT/UART三通道的协议帧，统一命令码体系
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

// 来源定义
#define SOURCE_BLE  0
#define SOURCE_MQTT 1
#define SOURCE_UART 2

// 全局变量
static uint32_t heartbeat_counter = 0;

// 文档协议字段编码转换（协议字段按文档定义：0=关/自动等）
static uint8_t internal_power_to_doc(AIRSwitchkey st)
{
    return (st == airSW_ON) ? 1 : 0; // 文档：1=开
}

static AIRSwitchkey doc_power_to_internal(uint8_t doc_power)
{
    return (doc_power == 1) ? airSW_ON : airSW_OFF;
}

static AIRmodekey doc_mode_to_internal(uint8_t doc_mode)
{
    // 文档：0=自动,1=制冷,2=制热,3=除湿,4=送风,5=睡眠
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
    // internal enum：0=制冷,1=制热,2=除湿,3=送风,4=自动,5=睡眠
    static const uint8_t map[6] = {1, 2, 3, 4, 0, 5};
    if (mode > airmode_Sleep) {
        return 0;
    }
    return map[mode];
}

// 等待红外发送完成（轮询 IR_PWM_TIM.IR_Busy）
static bool air_ir_send_key_and_wait(uint8_t keynumber, uint32_t timeout_ms)
{
    // 直接发射（内部会检查 busy 状态）
    ESOAAIR_IRsend(keynumber);

    // 轮询等待发送结束
    uint32_t elapsed_us = 0;
    const uint32_t step_us = 10; // 10us 轮询一次
    const uint32_t timeout_us = timeout_ms * 1000;

    while (IR_PWM_TIM.IR_Busy && elapsed_us < timeout_us) {
        co_delay_100us(step_us);
        elapsed_us += step_us;
    }

    return (IR_PWM_TIM.IR_Busy == 0);
}

// ========== 帧基础操作 ==========

void protocol_init(void)
{
    heartbeat_counter = 0;
}

uint8_t protocol_calc_checksum(protocol_frame_t *frame)
{
    uint8_t checksum = 0;
    uint16_t i;
    uint8_t data_len = 0;

    // Data Length 字段语义（按文档）：DataMark(1B) + Cmd(2B) + Data(N)
    // 因此业务数据长度 N = data_length - 3
    if (frame->data_length >= 3) {
        data_len = frame->data_length - 3;
    }

    checksum += frame->frame_header[0];
    checksum += frame->frame_header[1];
    checksum += frame->data_length;
    checksum += frame->data_mark;
    checksum += (uint8_t)(frame->command & 0xFF);
    checksum += (uint8_t)((frame->command >> 8) & 0xFF);

    for (i = 0; i < data_len; i++) {
        checksum += frame->data[i];
    }

    return checksum;
}

bool protocol_verify_checksum(protocol_frame_t *frame)
{
    return (protocol_calc_checksum(frame) == frame->checksum);
}

bool protocol_build_frame(protocol_frame_t *frame, uint16_t command,
                          uint8_t data_mark, uint8_t *data, uint8_t data_len)
{
    if (data_len > MAX_DATA_LENGTH) {
        return false;
    }

    frame->frame_header[0] = FRAME_HEADER_1;
    frame->frame_header[1] = FRAME_HEADER_2;
    // Data Length 字段语义（按文档）：DataMark(1B) + Cmd(2B) + Data(N)
    frame->data_length = data_len + 3;
    frame->data_mark = data_mark;
    frame->command = command;

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
    if (len < 6) {
        return false;
    }

    // 查找帧头
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

    // 总长度 = 2(header) + 1(length) + data_length + 1(checksum) = data_length + 4
    uint8_t frame_len = buffer[i + 2] + 4;
    if (i + frame_len > len) {
        return false;
    }

    frame->frame_header[0] = buffer[i];
    frame->frame_header[1] = buffer[i + 1];
    frame->data_length = buffer[i + 2];
    frame->data_mark = buffer[i + 3];
    frame->command = (buffer[i + 5] << 8) | buffer[i + 4];

    uint8_t data_len = 0;
    if (frame->data_length >= 3) {
        data_len = frame->data_length - 3;
    }
    if (data_len > 0 && data_len <= MAX_DATA_LENGTH) {
        memcpy(frame->data, &buffer[i + 6], data_len);
    } else {
        memset(frame->data, 0, MAX_DATA_LENGTH);
    }

    frame->checksum = buffer[i + 6 + data_len];

    if (!protocol_verify_checksum(frame)) {
        co_printf("Protocol checksum error\r\n");
        return false;
    }

    return true;
}

// ========== 发送公共函数 ==========

static uint16_t protocol_frame_to_buffer(protocol_frame_t *frame, uint8_t *buffer)
{
    uint16_t len = 0;
    buffer[len++] = frame->frame_header[0];
    buffer[len++] = frame->frame_header[1];
    buffer[len++] = frame->data_length;
    buffer[len++] = frame->data_mark;
    buffer[len++] = frame->command & 0xFF;
    buffer[len++] = (frame->command >> 8) & 0xFF;
    if (frame->data_length > 3) {
        uint8_t data_len = frame->data_length - 3;
        memcpy(&buffer[len], frame->data, data_len);
        len += data_len;
    }
    buffer[len++] = frame->checksum;
    return len;
}

static void protocol_send_buffer(uint8_t *buffer, uint16_t len, uint8_t source)
{
    if (source == SOURCE_BLE) {
        ESAIR_gatt_report_notify(0, buffer, len);
    } else if (source == SOURCE_MQTT) {
        if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
            bool ret = ML307A_MQTTPublish(g_mqtt_config.publish_topic, buffer, len);
            if (!ret) {
                co_printf("ERR: MQTT publish failed\r\n");
            }
        } else {
            co_printf("WARN: MQTT not connected, skip publish\r\n");
        }
    } else if (source == SOURCE_UART) {
        // UART 调试/桥接通道：直接回传协议帧
        uart_write(UART1, buffer, len);
    }
    // SOURCE_UART: 当前实现为直接回传（用于调试/桥接）
}

static bool protocol_build_and_send(uint16_t command, uint8_t data_mark,
                                     uint8_t *data, uint8_t data_len, uint8_t source)
{
    protocol_frame_t frame;
    if (!protocol_build_frame(&frame, command, data_mark, data, data_len)) {
        return false;
    }
    uint8_t buffer[FRAME_MAX_SIZE];
    uint16_t buf_len = protocol_frame_to_buffer(&frame, buffer);
    protocol_send_buffer(buffer, buf_len, source);
    return true;
}

// ========== 统一命令处理 ==========

// MQTT配置辅助函数前向声明
static bool validate_string_field(const char *str, uint8_t len, uint8_t min_len, uint8_t max_len);
static uint8_t build_mqtt_config_tlv(uint8_t *buf, uint8_t buf_size);

bool protocol_process_frame(protocol_frame_t *frame, uint8_t source)
{
    if (frame == NULL) {
        return false;
    }

    uint16_t cmd = frame->command;
    uint8_t payload_len = 0;
    if (frame->data_length >= 3) {
        payload_len = frame->data_length - 3; // Data 字节长度
    }

    switch (cmd) {
        case CMD_HEARTBEAT:
            protocol_send_response(CMD_HEARTBEAT, STATUS_SUCCESS, source);
            break;

        case CMD_DEVICE_INFO:
            protocol_send_device_info(source);
            break;

        // ---- 空调控制 ----
        case CMD_SET_POWER:
            if (payload_len >= 1) {
                if (frame->data[0] > 1) {
                    protocol_send_response(CMD_SET_POWER, STATUS_ERROR_PARAM, source);
                    break;
                }
                // 按文档：0=关, 1=开
                AIRSwitchkey desired = doc_power_to_internal(frame->data[0]);

                // 仅当目标状态与当前状态不一致时才发射红外按键
                if (desired != ESAirdata.AIRStatus) {
                    // key index 映射假设（需与上位机学习/下发约定一致）
                    // 0: 电源开/关
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
                // 按文档：0=自动,1=制冷,2=制热,3=除湿,4=送风,5=睡眠
                uint8_t target_doc_mode = frame->data[0];
                AIRmodekey desired = doc_mode_to_internal(target_doc_mode);

                if (desired != ESAirdata.AIRMODE) {
                    // 1: 模式切换按键（通过循环按键前进到目标模式）
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
                        // 2: 温度+  3: 温度-
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
                    // 4: 风速切换按键（按键前进到目标风速）
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

        // ---- 状态查询 ----
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
            protocol_send_response(CMD_GET_ADC_DATA, STATUS_SUCCESS, source);
            break;

        // ---- 红外遥控 ----
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
                TYPEDEFIRLEARNDATA *learn = &ESairkey.airbutton[keynumber];

                // bit0: 1=已学习
                if ((learn->IR_learn_state & BIT(0)) == 0) {
                    protocol_send_response(CMD_IR_READ_DATA, STATUS_ERROR_FAIL, source);
                    break;
                }

                // payload:
                // [0] learned_state (bit0)
                // [1..4] ir_carrier_fre (LE)
                // [5] total_cnt (原始学习数据个数)
                // [6..] pulses[0..P-1] (每个 4B, LE)，P 受 MAX_DATA_LENGTH 限制
                uint8_t payload[MAX_DATA_LENGTH];
                memset(payload, 0, sizeof(payload));

                payload[0] = learn->IR_learn_state;

                uint32_t carrier = learn->ir_carrier_fre;
                payload[1] = (uint8_t)(carrier & 0xFF);
                payload[2] = (uint8_t)((carrier >> 8) & 0xFF);
                payload[3] = (uint8_t)((carrier >> 16) & 0xFF);
                payload[4] = (uint8_t)((carrier >> 24) & 0xFF);

                payload[5] = learn->ir_learn_data_cnt;

                uint8_t max_pulses = (MAX_DATA_LENGTH - 6) / 4;
                uint8_t pulses = learn->ir_learn_data_cnt;
                if (pulses > max_pulses) {
                    pulses = max_pulses;
                }

                uint16_t pos = 6;
                for (uint8_t i = 0; i < pulses; i++) {
                    uint32_t v = learn->ir_learn_Date[i];
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

        // ---- 设备配置 ----
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

                // ble_update_device_name 内部完成: 校验 + Flash保存 + 更新BLE广播
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

        case CMD_SET_MQTT_CONFIG:
            {
                // TLV格式: [type(1)][len(1)][value(N)]...
                // type: 0=server_addr, 1=server_port, 2=client_id, 3=username,
                //       4=password, 5=subscribe_topic, 6=publish_topic
                // 至少需要3字节（一个完整TLV: type+len+value至少1字节）
                if (payload_len < 2) {
                    protocol_send_response(CMD_SET_MQTT_CONFIG, STATUS_ERROR_PARAM, source);
                    break;
                }

                // avail = 业务数据长度（不含DataMark/Command字段）
                uint8_t avail = payload_len;
                uint8_t *cfg_data = &frame->data[0];
                uint16_t pos = 0;
                bool any_field_valid = false;
                bool parse_error = false;
                // TLV 部分更新语义：只有命中的字段才更新；未提交字段保持原值不变
                bool updated[7] = {0};

                // 临时存储新配置
                char new_addr[MQTT_ADDR_MAX_LEN]     = {0};
                char new_port[MQTT_PORT_MAX_LEN]     = {0};
                char new_cid[MQTT_CLIENT_ID_MAX_LEN] = {0};
                char new_user[MQTT_USER_MAX_LEN]     = {0};
                char new_pass[MQTT_PASS_MAX_LEN]     = {0};
                char new_sub[MQTT_TOPIC_MAX_LEN]     = {0};
                char new_pub[MQTT_TOPIC_MAX_LEN]     = {0};

                while (pos + 2 <= avail) {
                    uint8_t type = cfg_data[pos];
                    uint8_t len  = cfg_data[pos + 1];

                    if (pos + 2 + len > avail) {
                        parse_error = true;
                        break;
                    }

                    char *value = (char *)&cfg_data[pos + 2];

                    switch (type) {
                        case 0: // server_addr
                            if (validate_string_field(value, len, 1, MQTT_ADDR_MAX_LEN - 1)) {
                                memcpy(new_addr, value, len);
                                new_addr[len] = '\0';
                                any_field_valid = true;
                                updated[0] = true;
                            }
                            break;
                        case 1: // server_port
                            if (validate_string_field(value, len, 1, MQTT_PORT_MAX_LEN - 1)) {
                                memcpy(new_port, value, len);
                                new_port[len] = '\0';
                                any_field_valid = true;
                                updated[1] = true;
                            }
                            break;
                        case 2: // client_id
                            if (validate_string_field(value, len, 1, MQTT_CLIENT_ID_MAX_LEN - 1)) {
                                memcpy(new_cid, value, len);
                                new_cid[len] = '\0';
                                any_field_valid = true;
                                updated[2] = true;
                            }
                            break;
                        case 3: // username
                            if (len == 0 || validate_string_field(value, len, 1, MQTT_USER_MAX_LEN - 1)) {
                                if (len > 0) memcpy(new_user, value, len);
                                new_user[len] = '\0';
                                any_field_valid = true;
                                updated[3] = true; // 支持提交 len=0 清空
                            }
                            break;
                        case 4: // password
                            // 密码允许任意字节（除控制字符），更宽松校验
                            if (len > 0 && len <= MQTT_PASS_MAX_LEN - 1) {
                                memcpy(new_pass, value, len);
                                new_pass[len] = '\0';
                                any_field_valid = true;
                                updated[4] = true;
                            }
                            break;
                        case 5: // subscribe_topic
                            if (validate_string_field(value, len, 1, MQTT_TOPIC_MAX_LEN - 1)) {
                                memcpy(new_sub, value, len);
                                new_sub[len] = '\0';
                                any_field_valid = true;
                                updated[5] = true;
                            }
                            break;
                        case 6: // publish_topic
                            if (validate_string_field(value, len, 1, MQTT_TOPIC_MAX_LEN - 1)) {
                                memcpy(new_pub, value, len);
                                new_pub[len] = '\0';
                                any_field_valid = true;
                                updated[6] = true;
                            }
                            break;
                        default:
                            // 未知字段类型，跳过
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

                // 更新全局配置：仅更新“提交过的字段”
                if (updated[0]) strncpy(g_mqtt_config.server_addr, new_addr, sizeof(g_mqtt_config.server_addr));
                if (updated[1]) strncpy(g_mqtt_config.server_port, new_port, sizeof(g_mqtt_config.server_port));
                if (updated[2]) strncpy(g_mqtt_config.client_id, new_cid, sizeof(g_mqtt_config.client_id));
                if (updated[3]) strncpy(g_mqtt_config.username, new_user, sizeof(g_mqtt_config.username));
                if (updated[4]) strncpy(g_mqtt_config.password, new_pass, sizeof(g_mqtt_config.password));
                if (updated[5]) strncpy(g_mqtt_config.subscribe_topic, new_sub, sizeof(g_mqtt_config.subscribe_topic));
                if (updated[6]) strncpy(g_mqtt_config.publish_topic, new_pub, sizeof(g_mqtt_config.publish_topic));

                // 保存到Flash
                if (mqtt_config_save()) {
                    co_printf("MQTT config updated & saved\r\n");
                    co_printf("  addr=%s, port=%s\r\n", g_mqtt_config.server_addr, g_mqtt_config.server_port);
                    protocol_send_response(CMD_SET_MQTT_CONFIG, STATUS_SUCCESS, source);

                    // 通知应用层：MQTT配置已更新，需要重连
                    app_task_send_event(APP_EVT_MQTT_CONFIG_UPDATED, NULL, 0);
                } else {
                    co_printf("MQTT config save FAILED\r\n");
                    protocol_send_response(CMD_SET_MQTT_CONFIG, STATUS_ERROR_FAIL, source);
                }
            }
            break;

        case CMD_GET_MQTT_CONFIG:
            {
                uint8_t cfg_buf[MAX_DATA_LENGTH];
                uint8_t cfg_len = build_mqtt_config_tlv(cfg_buf, sizeof(cfg_buf));
                protocol_build_and_send(CMD_GET_MQTT_CONFIG, DATA_MARK_RESPONSE,
                                        cfg_buf, cfg_len, source);
            }
            break;

        // ---- 时间同步 ----
        case CMD_SYNC_TIME:
            {
                // 数据格式: data[0..1]=年, data[2]=月, data[3]=日,
                //           data[4]=时, data[5]=分, data[6]=秒, data[7]=周
                // payload_len = 年(2B) + 月(1B)+日(1B)+时(1B)+分(1B)+秒(1B)+周(1B)=8B
                if (payload_len < 8) {
                    protocol_send_response(CMD_SYNC_TIME, STATUS_ERROR_PARAM, source);
                    break;
                }
                ESAirdata.EStime.ES_Year = (frame->data[0] << 8) | frame->data[1];
                ESAirdata.EStime.ES_Moon = frame->data[2];
                ESAirdata.EStime.ES_Day = frame->data[3];
                ESAirdata.EStime.ES_Hours = frame->data[4];
                ESAirdata.EStime.ES_Minutes = frame->data[5];
                ESAirdata.EStime.ES_Second = frame->data[6];
                ESAirdata.EStime.ES_Week = frame->data[7];
                protocol_send_response(CMD_SYNC_TIME, STATUS_SUCCESS, source);
            }
            break;

        default:
            co_printf("Unknown command: 0x%04X\r\n", cmd);
            protocol_send_response(cmd, STATUS_ERROR_CMD, source);
            return false;
    }

    return true;
}

// ========== 主动上报函数 ==========

void protocol_send_heartbeat(uint8_t source)
{
    uint8_t data[4];
    data[0] = (heartbeat_counter >> 24) & 0xFF;
    data[1] = (heartbeat_counter >> 16) & 0xFF;
    data[2] = (heartbeat_counter >> 8) & 0xFF;
    data[3] = heartbeat_counter & 0xFF;
    heartbeat_counter++;
    protocol_build_and_send(CMD_HEARTBEAT, DATA_MARK_NOTIFY, data, 4, source);
}

void protocol_send_device_info(uint8_t source)
{
    uint8_t data[14] = {0};

    // 设备ID (6字节)
    memcpy(data, ESAirdata.MUConlyID, 6);
    // 固件版本 (4字节)
    data[6] = 0x02;  // v2.0.0.0
    // 硬件版本 (4字节)
    data[10] = 0x01; // v1.0.0.0

    protocol_build_and_send(CMD_DEVICE_INFO, DATA_MARK_RESPONSE, data, 14, source);
}

void protocol_send_status(uint8_t source)
{
    uint8_t data[5];
    data[0] = internal_power_to_doc(ESAirdata.AIRStatus);
    data[1] = internal_mode_to_doc(ESAirdata.AIRMODE);
    data[2] = ESAirdata.AIRTemperature;
    data[3] = ESAirdata.AIRWindspeed;

    // 连接状态：任意连接为 1（BLE 或 MQTT）
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

/* ============================================================================
 *                      MQTT配置命令辅助函数
 * ============================================================================ */

/**
 * @brief 安全字符串校验（非空、可打印ASCII）
 */
static bool validate_string_field(const char *str, uint8_t len, uint8_t min_len, uint8_t max_len)
{
    if (str == NULL || len < min_len || len > max_len) {
        return false;
    }
    for (uint8_t i = 0; i < len; i++) {
        if ((uint8_t)str[i] < 0x20 || (uint8_t)str[i] > 0x7E) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 构建 MQTT 配置 TLV 数据
 * @param buf  输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 数据长度
 *
 * TLV格式: [type(1)][len(1)][value(N)]...
 * type: 0=server_addr, 1=server_port, 2=client_id, 3=username,
 *       4=password, 5=subscribe_topic, 6=publish_topic
 */
static uint8_t build_mqtt_config_tlv(uint8_t *buf, uint8_t buf_size)
{
    uint16_t pos = 0;
    struct { uint8_t type; const char *str; } fields[] = {
        {0, g_mqtt_config.server_addr},
        {1, g_mqtt_config.server_port},
        {2, g_mqtt_config.client_id},
        {3, g_mqtt_config.username},
        {4, g_mqtt_config.password},
        {5, g_mqtt_config.subscribe_topic},
        {6, g_mqtt_config.publish_topic},
    };

    for (int i = 0; i < 7 && pos + 2 < buf_size; i++) {
        uint8_t slen = (uint8_t)strlen(fields[i].str);
        buf[pos++] = fields[i].type;
        buf[pos++] = slen;
        if (pos + slen > buf_size) slen = buf_size - pos;
        memcpy(&buf[pos], fields[i].str, slen);
        pos += slen;
    }

    return (uint8_t)pos;
}

void protocol_send_response(uint16_t command, response_status_t status, uint8_t source)
{
    uint8_t data[3];
    data[0] = (command >> 8) & 0xFF;
    data[1] = command & 0xFF;
    data[2] = (uint8_t)status;
    protocol_build_and_send(CMD_RESPONSE, DATA_MARK_RESPONSE, data, 3, source);
}
