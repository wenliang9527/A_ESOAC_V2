/* URC解析器实现 - 统一处理ML307A模块的非请求结果码
 * 功能：URC队列管理、URC识别与解析、URC分发处理
 * 优化：使用静态缓冲区避免栈溢出
 */

#include "urc_parser.h"
#include "mqtt_handler.h"
#include "app_task.h"
#include "frATcode.h"
#include <stdlib.h>

/* ============================================================================
 * 静态缓冲区 - 避免栈溢出
 * ============================================================================ */

static char g_urc_temp_buf[URC_DATA_MAX_LEN];  // 全局临时缓冲区

/* ============================================================================
 * URC前缀表 - 用于识别URC类型
 * 新增URC类型只需在此表中添加条目
 * ============================================================================ */

static const urc_prefix_entry_t urc_prefix_table[] = {
    // MQTT相关URC
    {"+MQTTrecv:",    URC_MQTT_RECV,      true,  true },   // 收到MQTT数据，包含主题和数据
    {"+MQTTclosed:",  URC_MQTT_CLOSED,    false, false},   // MQTT连接断开
    {"+MQTTsub:",     URC_MQTT_SUB_OK,    false, false},   // 订阅成功
    {"+MQTTunsub:",   URC_MQTT_UNSUB_OK,  false, false},   // 退订成功
    {"+MQTTpub:",     URC_MQTT_PUB_OK,    false, false},   // 发布成功
    
    // 网络相关URC
    {"+CPIN: READY",  URC_SIM_READY,      false, false},   // SIM卡就绪
    {"+CREG: 0,1",    URC_NET_REGISTERED, false, false},   // 网络注册成功
    {"+CREG: 0,5",    URC_NET_REGISTERED, false, false},   // 网络注册成功(漫游)
    
    // 可以在此添加更多URC类型...
};

#define URC_PREFIX_COUNT (sizeof(urc_prefix_table) / sizeof(urc_prefix_table[0]))

/* ============================================================================
 * 队列操作函数
 * ============================================================================ */

/**
 * @brief 初始化URC队列
 */
void urc_queue_init(urc_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    
    memset(queue, 0, sizeof(urc_queue_t));
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

/**
 * @brief 向队列中添加URC条目
 * @return true成功，false队列已满
 */
bool urc_queue_push(urc_queue_t *queue, const urc_entry_t *entry)
{
    if (queue == NULL || entry == NULL) {
        return false;
    }
    
    if (urc_queue_is_full(queue)) {
        co_printf("URC queue full, dropping URC type=%d\r\n", entry->type);
        return false;
    }
    
    // 复制条目到队列
    memcpy(&queue->entries[queue->head], entry, sizeof(urc_entry_t));
    
    // 更新头指针
    queue->head = (queue->head + 1) % URC_QUEUE_SIZE;
    queue->count++;
    
    return true;
}

/**
 * @brief 从队列中取出URC条目
 * @return true成功，false队列空
 */
bool urc_queue_pop(urc_queue_t *queue, urc_entry_t *entry)
{
    if (queue == NULL || entry == NULL) {
        return false;
    }
    
    if (urc_queue_is_empty(queue)) {
        return false;
    }
    
    // 复制条目
    memcpy(entry, &queue->entries[queue->tail], sizeof(urc_entry_t));
    
    // 更新尾指针
    queue->tail = (queue->tail + 1) % URC_QUEUE_SIZE;
    queue->count--;
    
    return true;
}

/**
 * @brief 检查队列是否为空
 */
bool urc_queue_is_empty(const urc_queue_t *queue)
{
    return (queue == NULL) || (queue->count == 0);
}

/**
 * @brief 检查队列是否已满
 */
bool urc_queue_is_full(const urc_queue_t *queue)
{
    return (queue == NULL) || (queue->count >= URC_QUEUE_SIZE);
}

/**
 * @brief 获取队列中条目数
 */
uint8_t urc_queue_count(const urc_queue_t *queue)
{
    return (queue == NULL) ? 0 : queue->count;
}

/**
 * @brief 清空队列
 */
void urc_queue_clear(urc_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    memset(queue->entries, 0, sizeof(queue->entries));
}

/* ============================================================================
 * URC解析函数
 * ============================================================================ */

/**
 * @brief 识别URC类型
 * @param buffer 接收缓冲区
 * @param len 数据长度
 * @return URC类型，如果不是URC返回URC_NONE
 */
urc_type_t urc_identify(const uint8_t *buffer, uint16_t len)
{
    if (buffer == NULL || len == 0) {
        return URC_NONE;
    }
    
    // 使用全局静态缓冲区，避免栈溢出
    uint16_t check_len = (len < URC_DATA_MAX_LEN - 1) ? len : (URC_DATA_MAX_LEN - 1);
    memcpy(g_urc_temp_buf, buffer, check_len);
    g_urc_temp_buf[check_len] = '\0';
    
    // 遍历前缀表匹配
    for (int i = 0; i < URC_PREFIX_COUNT; i++) {
        if (strstr(g_urc_temp_buf, urc_prefix_table[i].prefix) != NULL) {
            return urc_prefix_table[i].type;
        }
    }
    
    return URC_NONE;
}

/**
 * @brief 解析+MQTTrecv: URC
 * 格式: +MQTTrecv:0,"topic",len\r\n<binary_data>
 */
bool urc_parse_mqtt_recv(uint8_t *buffer, uint16_t len, urc_entry_t *entry)
{
    if (buffer == NULL || entry == NULL || len == 0) {
        return false;
    }
    
    // 使用全局静态缓冲区，避免栈溢出
    uint16_t temp_len = (len < URC_DATA_MAX_LEN) ? len : URC_DATA_MAX_LEN;
    memcpy(g_urc_temp_buf, buffer, temp_len);
    g_urc_temp_buf[temp_len - 1] = '\0';
    
    // 查找+MQTTrecv:
    char *urc_start = strstr(g_urc_temp_buf, "+MQTTrecv:");
    if (urc_start == NULL) {
        return false;
    }
    
    // 跳过"+MQTTrecv:" (10个字符)
    char *parse_ptr = urc_start + 10;
    
    // 跳过linkid (第一个逗号前)
    char *comma1 = strchr(parse_ptr, ',');
    if (comma1 == NULL) {
        return false;
    }
    
    // 解析topic (第一个和第二个逗号之间，可能被引号包围)
    char *topic_start = comma1 + 1;
    char *comma2 = NULL;
    
    if (*topic_start == '"') {
        // 带引号的topic
        topic_start++;  // 跳过左引号
        char *quote_end = strchr(topic_start, '"');
        if (quote_end == NULL) {
            return false;
        }
        
        // 提取topic
        uint16_t topic_len = (uint16_t)(quote_end - topic_start);
        if (topic_len >= URC_TOPIC_MAX_LEN) {
            topic_len = URC_TOPIC_MAX_LEN - 1;
        }
        memcpy(entry->topic, topic_start, topic_len);
        entry->topic[topic_len] = '\0';
        
        comma2 = strchr(quote_end, ',');
    } else {
        // 不带引号的topic
        comma2 = strchr(topic_start, ',');
        if (comma2 != NULL) {
            uint16_t topic_len = (uint16_t)(comma2 - topic_start);
            if (topic_len >= URC_TOPIC_MAX_LEN) {
                topic_len = URC_TOPIC_MAX_LEN - 1;
            }
            memcpy(entry->topic, topic_start, topic_len);
            entry->topic[topic_len] = '\0';
        }
    }
    
    if (comma2 == NULL) {
        return false;
    }
    
    // 解析data_len
    uint16_t expect_data_len = (uint16_t)atoi(comma2 + 1);
    
    // 查找\r\n后的数据起始位置
    char *crlf = strstr(g_urc_temp_buf, "\r\n");
    if (crlf == NULL) {
        co_printf("URC MQTT recv: no CRLF found\r\n");
        return false;
    }
    
    // 数据从\r\n后开始
    uint16_t data_offset = (uint16_t)(crlf - g_urc_temp_buf + 2);
    if (data_offset >= len) {
        co_printf("URC MQTT recv: header only\r\n");
        return false;
    }
    
    // 计算实际数据长度
    uint16_t actual_data_len = len - data_offset;
    if (actual_data_len > expect_data_len) {
        actual_data_len = expect_data_len;
    }
    if (actual_data_len > URC_DATA_MAX_LEN) {
        actual_data_len = URC_DATA_MAX_LEN;
    }
    
    // 复制数据
    entry->data_len = actual_data_len;
    memcpy(entry->data, buffer + data_offset, actual_data_len);
    
    co_printf("URC MQTT recv parsed: topic=%s, expect=%d, actual=%d\r\n", 
              entry->topic, expect_data_len, actual_data_len);
    
    return true;
}

/**
 * @brief 解析URC
 * @param buffer 接收缓冲区
 * @param len 数据长度
 * @param entry 输出解析结果
 * @return true是URC且解析成功，false不是URC或解析失败
 */
bool urc_parse(uint8_t *buffer, uint16_t len, urc_entry_t *entry)
{
    if (buffer == NULL || entry == NULL || len == 0) {
        return false;
    }
    
    // 清空输出结构
    memset(entry, 0, sizeof(urc_entry_t));
    
    // 限制长度，避免溢出
    if (len > URC_DATA_MAX_LEN) {
        len = URC_DATA_MAX_LEN;
    }
    
    // 识别URC类型
    urc_type_t type = urc_identify(buffer, len);
    if (type == URC_NONE) {
        return false;
    }
    
    entry->type = type;
    
    // 根据类型进行具体解析
    switch (type) {
        case URC_MQTT_RECV:
            // +MQTTrecv: 需要特殊解析，提取topic和数据
            return urc_parse_mqtt_recv(buffer, len, entry);
            
        case URC_MQTT_CLOSED:
        case URC_MQTT_SUB_OK:
        case URC_MQTT_UNSUB_OK:
        case URC_MQTT_PUB_OK:
        case URC_SIM_READY:
        case URC_NET_REGISTERED:
        case URC_OTHER:
        default:
            // 简单URC，直接复制整个内容
            entry->data_len = len;
            memcpy(entry->data, buffer, len);
            return true;
    }
}

/* ============================================================================
 * URC处理分发函数
 * ============================================================================ */

/**
 * @brief 将URC类型转换为字符串（用于调试）
 */
const char* urc_type_to_string(urc_type_t type)
{
    switch (type) {
        case URC_MQTT_RECV:         return "MQTT_RECV";
        case URC_MQTT_CLOSED:       return "MQTT_CLOSED";
        case URC_MQTT_SUB_OK:       return "MQTT_SUB_OK";
        case URC_MQTT_UNSUB_OK:     return "MQTT_UNSUB_OK";
        case URC_MQTT_PUB_OK:       return "MQTT_PUB_OK";
        case URC_SIM_READY:         return "SIM_READY";
        case URC_NET_REGISTERED:    return "NET_REGISTERED";
        case URC_OTHER:             return "OTHER";
        case URC_NONE:
        default:                    return "NONE";
    }
}

/**
 * @brief 处理单个URC条目 - 统一的分发入口
 * 此函数集中处理所有URC类型，消除代码重复
 */
void urc_process_entry(const urc_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }
    
    co_printf("URC process: type=%s\r\n", urc_type_to_string(entry->type));
    
    switch (entry->type) {
        case URC_MQTT_RECV: {
            // MQTT数据接收 - 调用mqtt_handler处理
            if (entry->data_len > 0) {
                mqtt_handler_message_arrived(entry->topic, (uint8_t *)entry->data, entry->data_len);
            }
            break;
        }
        
        case URC_MQTT_CLOSED: {
            // MQTT连接断开 - 统一处理，消除重复代码
            co_printf("URC: MQTT connection closed by server\r\n");
            if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
                R_atcommand.MLinitflag = ML307A_Idle;
                mqtt_handler_connection_callback(false);
                app_task_send_event(APP_EVT_MQTT_DISCONNECTED, NULL, 0);
            }
            break;
        }
        
        case URC_MQTT_SUB_OK: {
            co_printf("URC: MQTT subscribe OK\r\n");
            // 可扩展：更新订阅状态
            break;
        }
        
        case URC_MQTT_UNSUB_OK: {
            co_printf("URC: MQTT unsubscribe OK\r\n");
            break;
        }
        
        case URC_MQTT_PUB_OK: {
            co_printf("URC: MQTT publish OK\r\n");
            // 可扩展：触发发布成功回调
            break;
        }
        
        case URC_SIM_READY: {
            co_printf("URC: SIM card ready\r\n");
            // 可扩展：触发SIM就绪事件
            break;
        }
        
        case URC_NET_REGISTERED: {
            co_printf("URC: Network registered\r\n");
            // 可扩展：触发网络就绪事件
            break;
        }
        
        case URC_OTHER:
        case URC_NONE:
        default: {
            co_printf("URC: Unknown or unhandled type=%d\r\n", entry->type);
            break;
        }
    }
}
