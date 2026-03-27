/**
 * @file ML307A_IR_Bridge.c
 * @brief ML307A模块红外数据桥接功能
 * @details 实现红外数据通过4G网络传输的桥接功能
 */

#include "ML307A_IR_Bridge.h"
#include "frIRConversion_optimized.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// 配置参数
#define MQTT_TOPIC_IR_DATA      "device/ir/data"
#define MQTT_TOPIC_IR_COMMAND   "device/ir/command"
#define MQTT_TOPIC_IR_STATUS    "device/ir/status"
#define JSON_BUFFER_SIZE        512
#define MAX_IR_DATA_SIZE        256

// 状态定义
typedef enum {
    IR_BRIDGE_STATE_IDLE = 0,
    IR_BRIDGE_STATE_CONNECTING,
    IR_BRIDGE_STATE_CONNECTED,
    IR_BRIDGE_STATE_PUBLISHING,
    IR_BRIDGE_STATE_ERROR
} IR_BridgeState_t;

// 全局变量
static IR_BridgeState_t g_ir_bridge_state = IR_BRIDGE_STATE_IDLE;
static uint8_t g_ir_data_buffer[IR_MAX_DATA_SIZE] = {0};
static uint16_t g_ir_data_count = 0;
static uint32_t g_last_publish_time = 0;
static bool g_bridge_initialized = false;

// 内部函数声明
static bool IR_Bridge_CreateJSON(const TYPEDEFIRLEARNDATA *ir_data, char *json_buffer, uint16_t buffer_size);
static bool IR_Bridge_ParseJSON(const char *json_data, TYPEDEFIRTRANSMITDATA *transmit_data);
static void IR_Bridge_StatusCallback(const char *status);
static void IR_Bridge_CommandCallback(const char *command_data);

/**
 * @brief 初始化红外数据桥接
 * @return 初始化是否成功
 */
bool ML307A_IR_Bridge_Init(void)
{
    if (g_bridge_initialized) {
        return true;
    }
    
    // 检查ML307A模块状态
    if (!ML307A_IsModuleReady()) {
        IR_LOG("ML307A module not ready");
        return false;
    }
    
    // 订阅红外控制命令主题
    if (!ML307A_MQTT_Subscribe(MQTT_TOPIC_IR_COMMAND, IR_Bridge_CommandCallback)) {
        IR_LOG("Failed to subscribe IR command topic");
        return false;
    }
    
    // 订阅状态查询主题
    if (!ML307A_MQTT_Subscribe(MQTT_TOPIC_IR_STATUS, IR_Bridge_StatusCallback)) {
        IR_LOG("Failed to subscribe IR status topic");
        return false;
    }
    
    g_ir_bridge_state = IR_BRIDGE_STATE_CONNECTED;
    g_bridge_initialized = true;
    g_last_publish_time = 0;
    
    IR_LOG("IR bridge initialized successfully");
    return true;
}

/**
 * @brief 发布红外数据到云端
 * @param ir_data 红外数据
 * @return 发布是否成功
 */
bool ML307A_IR_Bridge_PublishIRData(TYPEDEFIRLEARNDATA *ir_data)
{
    if (!g_bridge_initialized || !ir_data) {
        IR_LOG("Invalid parameters or bridge not initialized");
        return false;
    }
    
    if (g_ir_bridge_state != IR_BRIDGE_STATE_CONNECTED) {
        IR_LOG("Bridge not connected, state: %d", g_ir_bridge_state);
        return false;
    }
    
    char json_buffer[JSON_BUFFER_SIZE] = {0};
    
    // 创建JSON数据
    if (!IR_Bridge_CreateJSON(ir_data, json_buffer, JSON_BUFFER_SIZE)) {
        IR_LOG("Failed to create JSON data");
        return false;
    }
    
    // 发布数据
    g_ir_bridge_state = IR_BRIDGE_STATE_PUBLISHING;
    
    if (!ML307A_MQTT_Publish(MQTT_TOPIC_IR_DATA, json_buffer, strlen(json_buffer), 1)) {
        IR_LOG("Failed to publish IR data");
        g_ir_bridge_state = IR_BRIDGE_STATE_ERROR;
        return false;
    }
    
    g_ir_bridge_state = IR_BRIDGE_STATE_CONNECTED;
    g_last_publish_time = ML307A_GetTickCount();
    
    IR_LOG("IR data published successfully");
    return true;
}

/**
 * @brief 订阅红外控制命令
 * @param topic MQTT主题
 * @param callback 回调函数
 * @return 订阅是否成功
 */
bool ML307A_IR_Bridge_SubscribeCommand(const char *topic, void (*callback)(const char *data))
{
    if (!g_bridge_initialized || !topic) {
        return false;
    }
    
    return ML307A_MQTT_Subscribe(topic, callback);
}

/**
 * @brief 创建红外数据的JSON格式
 * @param ir_data 红外数据
 * @param json_buffer JSON缓冲区
 * @param buffer_size 缓冲区大小
 * @return 创建是否成功
 */
static bool IR_Bridge_CreateJSON(const TYPEDEFIRLEARNDATA *ir_data, char *json_buffer, uint16_t buffer_size)
{
    if (!ir_data || !json_buffer || buffer_size < 100) {
        return false;
    }
    
    // 首先解码红外数据
    int decoded_value = ir_data_to_int((TYPEDEFIRLEARNDATA *)ir_data);
    
    // 创建JSON格式数据
    int written = snprintf(json_buffer, buffer_size,
        "{"
        "\"timestamp\":%lu,"
        "\"protocol\":%d,"
        "\"data_len\":%d,"
        "\"decoded_value\":%d,"
        "\"raw_data\":[",
        ir_data->timestamp,
        ir_data->protocol,
        ir_data->data_len,
        decoded_value
    );
    
    // 添加原始数据（限制数据长度）
    uint16_t data_to_send = (ir_data->data_len > 50) ? 50 : ir_data->data_len;
    
    for (uint16_t i = 0; i < data_to_send; i++) {
        written += snprintf(json_buffer + written, buffer_size - written,
                           "%s%d", (i > 0) ? "," : "", ir_data->raw_data[i]);
        
        if (written >= buffer_size - 10) {
            break;  // 缓冲区空间不足
        }
    }
    
    // 如果数据被截断，添加省略号
    if (ir_data->data_len > data_to_send) {
        written += snprintf(json_buffer + written, buffer_size - written, ",...");
    }
    
    // 关闭JSON
    written += snprintf(json_buffer + written, buffer_size - written, "]}");
    
    return (written < buffer_size - 1);
}

/**
 * @brief 解析JSON格式的红外控制命令
 * @param json_data JSON数据
 * @param transmit_data 输出发射数据
 * @return 解析是否成功
 */
static bool IR_Bridge_ParseJSON(const char *json_data, TYPEDEFIRTRANSMITDATA *transmit_data)
{
    if (!json_data || !transmit_data) {
        return false;
    }
    
    // 简化的JSON解析（实际项目中应使用完整的JSON解析库）
    int address = 0, command = 0, protocol = 0;
    
    // 查找关键字段
    const char *addr_ptr = strstr(json_data, "\"address\":");
    const char *cmd_ptr = strstr(json_data, "\"command\":");
    const char *proto_ptr = strstr(json_data, "\"protocol\":");
    
    if (addr_ptr && cmd_ptr && proto_ptr) {
        sscanf(addr_ptr + 9, "%d", &address);
        sscanf(cmd_ptr + 10, "%d", &command);
        sscanf(proto_ptr + 11, "%d", &protocol);
        
        // 编码红外信号
        if (ir_encode_signal((uint8_t)address, (uint8_t)command, (uint8_t)protocol, transmit_data)) {
            IR_LOG("IR command parsed: addr=%d, cmd=%d, proto=%d", address, command, protocol);
            return true;
        }
    }
    
    IR_LOG("Failed to parse IR command JSON");
    return false;
}

/**
 * @brief 状态查询回调函数
 * @param status 状态数据
 */
static void IR_Bridge_StatusCallback(const char *status)
{
    if (!status) {
        return;
    }
    
    IR_LOG("Status query received: %s", status);
    
    // 创建状态响应
    char response[256];
    snprintf(response, sizeof(response),
        "{"
        "\"ir_bridge_status\":\"%s\","
        "\"learned_data_count\":%d,"
        "\"last_publish_time\":%lu,"
        "\"protocols\":[\"NEC\",\"Sony\",\"RC5\"]"
        "}",
        (g_ir_bridge_state == IR_BRIDGE_STATE_CONNECTED) ? "connected" : "disconnected",
        IR_GetLearnedDataCount(),
        g_last_publish_time
    );
    
    // 发布状态响应
    ML307A_MQTT_Publish(MQTT_TOPIC_IR_STATUS, response, strlen(response), 0);
}

/**
 * @brief 红外控制命令回调函数
 * @param command_data 命令数据
 */
static void IR_Bridge_CommandCallback(const char *command_data)
{
    if (!command_data) {
        return;
    }
    
    IR_LOG("IR command received: %s", command_data);
    
    TYPEDEFIRTRANSMITDATA transmit_data = {0};
    
    // 解析命令
    if (IR_Bridge_ParseJSON(command_data, &transmit_data)) {
        // 发射红外信号
        if (ir_transmit_signal(&transmit_data)) {
            IR_LOG("IR command transmitted successfully");
            
            // 发送确认响应
            char confirm[128];
            snprintf(confirm, sizeof(confirm),
                "{\"status\":\"success\",\"message\":\"IR command transmitted\"}");
            ML307A_MQTT_Publish(MQTT_TOPIC_IR_STATUS, confirm, strlen(confirm), 0);
        } else {
            IR_LOG("Failed to transmit IR command");
            
            // 发送错误响应
            char error[128];
            snprintf(error, sizeof(error),
                "{\"status\":\"error\",\"message\":\"IR transmission failed\"}");
            ML307A_MQTT_Publish(MQTT_TOPIC_IR_STATUS, error, strlen(error), 0);
        }
    } else {
        IR_LOG("Failed to parse IR command");
        
        // 发送解析错误响应
        char error[128];
        snprintf(error, sizeof(error),
            "{\"status\":\"error\",\"message\":\"Invalid IR command format\"}");
        ML307A_MQTT_Publish(MQTT_TOPIC_IR_STATUS, error, strlen(error), 0);
    }
}

/**
 * @brief 定期处理红外桥接任务
 */
void ML307A_IR_Bridge_Process(void)
{
    if (!g_bridge_initialized) {
        return;
    }
    
    // 检查连接状态
    if (!ML307A_IsNetworkConnected()) {
        g_ir_bridge_state = IR_BRIDGE_STATE_ERROR;
        return;
    }
    
    // 如果处于错误状态，尝试重新连接
    if (g_ir_bridge_state == IR_BRIDGE_STATE_ERROR) {
        if (ML307A_IR_Bridge_Init()) {
            g_ir_bridge_state = IR_BRIDGE_STATE_CONNECTED;
            IR_LOG("IR bridge reconnected");
        }
    }
    
    // 定期发送心跳（每30秒）
    uint32_t current_time = ML307A_GetTickCount();
    if (current_time - g_last_publish_time > 30000) {
        char heartbeat[128];
        snprintf(heartbeat, sizeof(heartbeat),
            "{\"type\":\"heartbeat\",\"timestamp\":%lu,\"status\":\"alive\"}",
            current_time);
        
        ML307A_MQTT_Publish(MQTT_TOPIC_IR_STATUS, heartbeat, strlen(heartbeat), 0);
        g_last_publish_time = current_time;
    }
}

/**
 * @brief 去初始化红外桥接
 */
void ML307A_IR_Bridge_Deinit(void)
{
    if (!g_bridge_initialized) {
        return;
    }
    
    // 取消订阅
    ML307A_MQTT_Unsubscribe(MQTT_TOPIC_IR_COMMAND);
    ML307A_MQTT_Unsubscribe(MQTT_TOPIC_IR_STATUS);
    
    // 清理状态
    g_bridge_initialized = false;
    g_ir_bridge_state = IR_BRIDGE_STATE_IDLE;
    g_ir_data_count = 0;
    g_last_publish_time = 0;
    
    IR_LOG("IR bridge deinitialized");
}