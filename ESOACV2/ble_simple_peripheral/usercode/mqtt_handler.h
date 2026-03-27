/* ML307 MQTT：订阅 payload 当协议帧解析；发布走 AT */

#ifndef _MQTT_HANDLER_H
#define _MQTT_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"

typedef enum {
    MQTT_EVENT_CONNECTED = 0,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_DATA_RECEIVED,
    MQTT_EVENT_PUBLISHED,
    MQTT_EVENT_ERROR
} mqtt_event_type_t;

typedef void (*mqtt_data_callback_t)(uint8_t *data, uint16_t len);

void mqtt_handler_init(void);
void mqtt_handler_message_arrived(const char *topic, uint8_t *payload, uint16_t payload_len);
void mqtt_handler_connection_callback(bool connected);
bool mqtt_handler_publish(const char *topic, uint8_t *data, uint16_t len);
bool mqtt_handler_subscribe(const char *topic);
bool mqtt_handler_unsubscribe(const char *topic);
void mqtt_handler_register_callback(mqtt_data_callback_t callback);
void mqtt_handler_send_status(void);
void mqtt_handler_send_power(void);
void mqtt_handler_send_temperature(void);

#endif
