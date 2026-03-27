/**
 * @file app_task.h
 * @brief 应用层主任务头文件
 */

#ifndef _APP_TASK_H
#define _APP_TASK_H

#include <stdint.h>
#include "os_task.h"

// 应用任务事件定义
#define APP_EVT_MQTT_DATA_RECEIVED    0x0101
#define APP_EVT_BLE_DATA_RECEIVED     0x0102
#define APP_EVT_MQTT_CONNECTED        0x0103
#define APP_EVT_MQTT_DISCONNECTED     0x0104
#define APP_EVT_SENSOR_DATA_READY     0x0105
#define APP_EVT_IR_LEARN_COMPLETED    0x0106
#define APP_EVT_IR_LEARN_FAILED       0x0107
#define APP_EVT_MQTT_CONFIG_UPDATED   0x0108

// 全局变量
extern uint16_t app_task_id;

/** 协议与设备配置加载（早于 BLE；不含 MQTT） */
void app_task_init_early(void);

/** MQTT 配置/处理器与周期定时器（BLE 初始化完成之后） */
void app_task_init_mqtt_timers(void);

/** 完整应用任务初始化（early + mqtt_timers） */
void app_task_init(void);

/**
 * @brief MQTT重连定时器启动（断线后调用）
 */
void app_task_start_reconnect_timer(void);

/**
 * @brief MQTT重连定时器停止（连接成功后调用）
 */
void app_task_stop_reconnect_timer(void);

/**
 * @brief 发送事件到应用任务
 * @param event_id 事件ID
 * @param param 参数
 * @param param_size 参数大小
 */
void app_task_send_event(uint16_t event_id, void *param, uint16_t param_size);

#endif // _APP_TASK_H
