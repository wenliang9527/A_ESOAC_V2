/* MQTT：与 R_atcommand.MLinitflag 一致判断连接；payload 同 BLE/UART 二进制帧 */

#include "mqtt_handler.h"
#include <string.h>
#include "co_printf.h"
#include "frATcode.h"
#include "aircondata.h"

/* 连接态以 ML307AMQTT_OK 为准，不用单独静态变量 */
static mqtt_data_callback_t data_callback = NULL;

static bool mqtt_is_connected(void)
{
    return (R_atcommand.MLinitflag == ML307AMQTT_OK);
}

void mqtt_handler_init(void)
{
    data_callback = NULL;
    co_printf("MQTT Handler initialized\r\n");
}

void mqtt_handler_message_arrived(const char *topic, uint8_t *payload, uint16_t payload_len)
{
    if (payload == NULL || payload_len == 0) {
        return;
    }

    co_printf("MQTT message arrived, topic:%s, len:%d\r\n", topic, payload_len);

    protocol_frame_t frame;
    if (protocol_parse_frame(payload, payload_len, &frame)) {
        protocol_process_frame(&frame, PROTOCOL_SRC_MQTT);
    } else {
        co_printf("MQTT protocol parse error\r\n");
    }

    if (data_callback != NULL) {
        data_callback(payload, payload_len);
    }
}

void mqtt_handler_connection_callback(bool connected)
{
    if (connected) {
        co_printf("MQTT Connected\r\n");
    } else {
        co_printf("MQTT Disconnected\r\n");
    }
}

bool mqtt_handler_publish(const char *topic, uint8_t *data, uint16_t len)
{
    if (!mqtt_is_connected() || topic == NULL || data == NULL || len == 0) {
        return false;
    }

    return ML307A_MQTTPublish(topic, data, len);
}

bool mqtt_handler_subscribe(const char *topic)
{
    if (topic == NULL) {
        return false;
    }

    AT_SendCommandFormat("AT+MQTTSUB=0,\"%s\",1", topic);

    if (AT_WaitResponse("OK", 5000) == 0) {
        co_printf("MQTT Subscribed to %s\r\n", topic);
        return true;
    }

    return false;
}

bool mqtt_handler_unsubscribe(const char *topic)
{
    if (topic == NULL) {
        return false;
    }

    AT_SendCommandFormat("AT+MQTTUNSUB=0,\"%s\"", topic);

    if (AT_WaitResponse("OK", 5000) == 0) {
        co_printf("MQTT Unsubscribed from %s\r\n", topic);
        return true;
    }

    return false;
}

void mqtt_handler_register_callback(mqtt_data_callback_t callback)
{
    data_callback = callback;
}

void mqtt_handler_send_status(void)
{
    if (!mqtt_is_connected()) {
        return;
    }

    protocol_send_status(PROTOCOL_SRC_MQTT);
}

void mqtt_handler_send_power(void)
{
    if (!mqtt_is_connected()) {
        return;
    }

    protocol_send_power(PROTOCOL_SRC_MQTT);
}

void mqtt_handler_send_temperature(void)
{
    if (!mqtt_is_connected()) {
        return;
    }

    protocol_send_temperature(PROTOCOL_SRC_MQTT);
}
