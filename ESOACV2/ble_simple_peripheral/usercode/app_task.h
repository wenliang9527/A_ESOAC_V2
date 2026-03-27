/* 应用任务事件与初始化接口 */

#ifndef _APP_TASK_H
#define _APP_TASK_H

#include <stdint.h>
#include "os_task.h"

#define APP_EVT_MQTT_DATA_RECEIVED    0x0101
#define APP_EVT_BLE_DATA_RECEIVED     0x0102
#define APP_EVT_MQTT_CONNECTED        0x0103
#define APP_EVT_MQTT_DISCONNECTED     0x0104
#define APP_EVT_SENSOR_DATA_READY     0x0105
#define APP_EVT_IR_LEARN_COMPLETED    0x0106
#define APP_EVT_IR_LEARN_FAILED       0x0107
#define APP_EVT_MQTT_CONFIG_UPDATED   0x0108

extern uint16_t app_task_id;

void app_task_init_early(void);
void app_task_init_mqtt_timers(void);
void app_task_init(void);
void app_task_start_reconnect_timer(void);
void app_task_stop_reconnect_timer(void);
void app_task_send_event(uint16_t event_id, void *param, uint16_t param_size);

#endif
