#ifndef _FRATCODE_H
#define _FRATCODE_H

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "driver_system.h"
#include "driver_gpio.h"
#include "driver_timer.h"
#include "driver_uart.h"
#include "os_task.h"
#include "os_msg_q.h"
#include "os_timer.h"
#include "co_printf.h"
#include "driver_pmu.h"
#include "frusart.h"
#include "sys_utils.h"
#include "frADC.h"
#include "protocol.h"
#include "urc_parser.h"  // 新增：URC解析器

#define PING_REQ_TIME 60
#define KEEP_ALIVE_TIME 125

typedef enum {
    MLAT_RE,
    MLAT_TX
} MLATREStatus;

typedef enum {
    NET_INITIALIZING = 0,
    DIS_CONNECTING,
    PING_RESP,
    PING_REQ,
    KEEP_ALIVE,
    QUERY_PING_REQ,
    QUERY_KEEP_ALIVE,
    QUERY_PING_RESP,
    CONNECTTING,
    SUBSCRIBING,
    ML_MQTT_OK,
    ML_ERROR
} MLATConStatus;

typedef enum {
    ML307A_Idle = 0,
    ML307AHardware_OK,
    ML307AMQTT_OK,
    ML307AMQTT_Subscribe,
    ML307AMQTT_ERR,
    ML307AMQTT_inttimeout
} MLATCMDStatus;

/* 注意：以下旧的URC相关定义已被urc_parser.h取代，保留用于兼容性
 * - ml307a_urc_type_t  -> 使用 urc_type_t
 * - mqtt_recv_state_t  -> 已移除，使用urc_queue_t
 * - mqtt_recv_ctx_t    -> 已移除，使用urc_queue_t
 */

// 保留旧的mqtt_recv_ctx_t定义用于兼容性（可选，建议后续移除）
// 新代码应直接使用urc_parser.h中的urc_queue_t

typedef struct {
    bool            RECompleted;
    bool            MLouttime;
    MLATConStatus   ML307Con;
    MLATCMDStatus   MLinitflag;
    uint8_t         REcmd_string[128];
    uint8_t         SEcmd_string[128];
    // 使用新的URC队列替代旧的mqtt_recv_ctx_t
    urc_queue_t     urc_queue;      // URC环形队列
    os_timer_t      urc_timer;      // URC处理定时器
    bool            urc_timer_inited; // 定时器初始化标志
} at_command_t;

typedef struct {
    char server_addr[128];
    char server_port[6];
    char client_id[128];
    char username[128];
    char password[128];
    char subscribe_topic[128];
    char publish_topic[128];
} mqtt_config_t;

extern at_command_t R_atcommand;
extern mqtt_config_t g_mqtt_config;
extern uint16_t MLAT_task_id;

extern uint8_t AT_WaitResponse(const char *expected, uint32_t timeout);
extern void MLHardwareINIT(void);
extern bool ML307A_MQTTPublish(const char *topic, uint8_t *data, uint16_t len);
extern void AT_SendCommandFormat(const char *format, ...);

// URC处理函数（已重写，使用新的URC队列机制）
extern void ML307A_ProcessURC(void);
extern void ML307A_URC_Init(void);
extern void ML307A_URC_StartTimer(void);
extern void ML307A_URC_StopTimer(void);

extern void ML307A_MQTTReconnect(void);
extern void ML307A_MQTTinit(void);

#endif
