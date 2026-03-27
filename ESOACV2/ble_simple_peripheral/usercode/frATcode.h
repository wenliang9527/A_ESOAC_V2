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

// MQTT URC类型
typedef enum {
    URC_NONE = 0,
    URC_MQTT_RECV,         // +MQTTrecv:...
    URC_MQTT_CLOSED,       // MQTT连接断开
    URC_MQTT_SUB_OK,       // 订阅成功
    URC_OTHER              // 其他URC/AT响应
} ml307a_urc_type_t;

// MQTT接收状态机
typedef enum {
    MQTT_RECV_IDLE = 0,         // 空闲，未在接收MQTT数据
    MQTT_RECV_WAIT_HEADER,      // 已收到+MQTTrecv:，等待解析头部
    MQTT_RECV_WAIT_DATA         // 已解析头部，等待接收数据体
} mqtt_recv_state_t;

// MQTT URC解析上下文
typedef struct {
    mqtt_recv_state_t state;    // 当前接收状态
    uint16_t data_len;          // 期望的数据长度
    uint16_t received_len;      // 已接收的数据长度
    uint8_t data[MAX_DATA_LENGTH + 32];  // 数据缓冲区
    bool urc_ready;             // URC已就绪，等待处理
    ml307a_urc_type_t urc_type; // URC类型
} mqtt_recv_ctx_t;

typedef struct {
    bool            RECompleted;
    bool            MLouttime;
    MLATConStatus   ML307Con;
    MLATCMDStatus   MLinitflag;
    uint8_t         REcmd_string[128];
    uint8_t         SEcmd_string[128];
    mqtt_recv_ctx_t mqtt_recv;   // MQTT接收上下文
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
extern void ML307A_ProcessURC(void);
extern void ML307A_MQTTReconnect(void);
extern void ML307A_MQTTinit(void);

#endif
