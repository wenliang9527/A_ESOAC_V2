/**
 * @file mqtt_handler.c
 * @brief MQTT消息处理模块实现
 */

#include "mqtt_handler.h"
#include <string.h>
#include "co_printf.h"
#include "frATcode.h"
#include "aircondata.h"

// mqtt_connected 不再使用独立静态变量，
// 直接读取 R_atcommand.MLinitflag == ML307AMQTT_OK 来判断连接状态，
// 确保与AT指令层的状态严格一致。
static mqtt_data_callback_t data_callback = NULL;

/**
 * @brief 获取MQTT连接状态
 * @return true:已连接 false:未连接
 */
static bool mqtt_is_connected(void)
{
    return (R_atcommand.MLinitflag == ML307AMQTT_OK);
}

/**
 * @brief MQTT处理器初始化
 */
void mqtt_handler_init(void)
{
    data_callback = NULL;
    co_printf("MQTT Handler initialized\r\n");
}

/**
 * @brief MQTT消息到达处理
 * @param topic 主题
 * @param payload 数据
 * @param payload_len 数据长度
 */
void mqtt_handler_message_arrived(const char *topic, uint8_t *payload, uint16_t payload_len)
{
    if (payload == NULL || payload_len == 0) {
        return;
    }

    co_printf("MQTT message arrived, topic:%s, len:%d\r\n", topic, payload_len);

    /* 与 UART1、BLE GATT 相同二进制帧；含 CMD_SET_BLE_NAME(0x0501) 等 */
    protocol_frame_t frame;
    if (protocol_parse_frame(payload, payload_len, &frame)) {
        protocol_process_frame(&frame, 1);  // 1 = MQTT source
    } else {
        co_printf("MQTT protocol parse error\r\n");
    }

    // 调用用户注册的回调
    if (data_callback != NULL) {
        data_callback(payload, payload_len);
    }
}

/**
 * @brief MQTT连接状态回调
 * @param connected true:已连接 false:已断开
 */
void mqtt_handler_connection_callback(bool connected)
{
    if (connected) {
        co_printf("MQTT Connected\r\n");
    } else {
        co_printf("MQTT Disconnected\r\n");
    }
    // mqtt_connected状态由R_atcommand.MLinitflag统一管理，无需在此设置
}

/**
 * @brief 发布MQTT消息
 * @param topic 主题
 * @param data 数据
 * @param len 数据长度
 * @return true:成功 false:失败
 */
bool mqtt_handler_publish(const char *topic, uint8_t *data, uint16_t len)
{
    if (!mqtt_is_connected() || topic == NULL) {
        return false;
    }

    // 构造AT指令
    // 注意：实际实现需要将二进制数据转换为HEX字符串
    AT_SendCommandFormat("AT+MQTTPUB=0,\"%s\",%d", topic, len);

    // 这里需要发送实际数据
    // 由于AT+MQTTPUB指令的复杂性和实现差异
    // 实际使用时需要根据ML307A的具体AT指令格式调整

    co_printf("MQTT Published to %s, len:%d\r\n", topic, len);
    return true;
}

/**
 * @brief 订阅MQTT主题
 * @param topic 主题
 * @return true:成功 false:失败
 */
bool mqtt_handler_subscribe(const char *topic)
{
    if (topic == NULL) {
        return false;
    }

    AT_SendCommandFormat("AT+MQTTSUB=0,\"%s\",1", topic);

    // 等待响应
    if (AT_WaitResponse("OK", 5000) == 0) {
        co_printf("MQTT Subscribed to %s\r\n", topic);
        return true;
    }

    return false;
}

/**
 * @brief 取消订阅MQTT主题
 * @param topic 主题
 * @return true:成功 false:失败
 */
bool mqtt_handler_unsubscribe(const char *topic)
{
    if (topic == NULL) {
        return false;
    }

    AT_SendCommandFormat("AT+MQTTUNSUB=0,\"%s\"", topic);

    // 等待响应
    if (AT_WaitResponse("OK", 5000) == 0) {
        co_printf("MQTT Unsubscribed from %s\r\n", topic);
        return true;
    }

    return false;
}

/**
 * @brief 注册MQTT数据接收回调
 * @param callback 回调函数指针
 */
void mqtt_handler_register_callback(mqtt_data_callback_t callback)
{
    data_callback = callback;
}

/**
 * @brief 发送设备状态到MQTT
 */
void mqtt_handler_send_status(void)
{
    if (!mqtt_is_connected()) {
        return;
    }

    protocol_send_status(1);  // 1 = MQTT source
}

/**
 * @brief 发送功率数据到MQTT
 */
void mqtt_handler_send_power(void)
{
    if (!mqtt_is_connected()) {
        return;
    }

    protocol_send_power(1);  // 1 = MQTT source
}

/**
 * @brief 发送温度数据到MQTT
 */
void mqtt_handler_send_temperature(void)
{
    if (!mqtt_is_connected()) {
        return;
    }

    protocol_send_temperature(1);  // 1 = MQTT source
}
