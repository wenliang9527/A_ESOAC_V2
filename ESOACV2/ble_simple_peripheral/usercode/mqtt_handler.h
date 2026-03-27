/**
 * @file mqtt_handler.h
 * @brief MQTT消息处理模块头文件
 * @description 处理来自ML307A 4G模块的MQTT消息
 */

#ifndef _MQTT_HANDLER_H
#define _MQTT_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"

// MQTT事件类型
typedef enum {
    MQTT_EVENT_CONNECTED = 0,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_DATA_RECEIVED,
    MQTT_EVENT_PUBLISHED,
    MQTT_EVENT_ERROR
} mqtt_event_type_t;

// MQTT数据接收回调类型
typedef void (*mqtt_data_callback_t)(uint8_t *data, uint16_t len);

/**
 * @brief MQTT处理器初始化
 */
void mqtt_handler_init(void);

/**
 * @brief MQTT消息到达处理
 * @param topic 主题
 * @param payload 数据
 * @param payload_len 数据长度
 */
void mqtt_handler_message_arrived(const char *topic, uint8_t *payload, uint16_t payload_len);

/**
 * @brief MQTT连接状态回调
 * @param connected true:已连接 false:已断开
 */
void mqtt_handler_connection_callback(bool connected);

/**
 * @brief 发布MQTT消息
 * @param topic 主题
 * @param data 数据
 * @param len 数据长度
 * @return true:成功 false:失败
 */
bool mqtt_handler_publish(const char *topic, uint8_t *data, uint16_t len);

/**
 * @brief 订阅MQTT主题
 * @param topic 主题
 * @return true:成功 false:失败
 */
bool mqtt_handler_subscribe(const char *topic);

/**
 * @brief 取消订阅MQTT主题
 * @param topic 主题
 * @return true:成功 false:失败
 */
bool mqtt_handler_unsubscribe(const char *topic);

/**
 * @brief 注册MQTT数据接收回调
 * @param callback 回调函数指针
 */
void mqtt_handler_register_callback(mqtt_data_callback_t callback);

/**
 * @brief 发送设备状态到MQTT
 */
void mqtt_handler_send_status(void);

/**
 * @brief 发送功率数据到MQTT
 */
void mqtt_handler_send_power(void);

/**
 * @brief 发送温度数据到MQTT
 */
void mqtt_handler_send_temperature(void);

#endif // _MQTT_HANDLER_H
