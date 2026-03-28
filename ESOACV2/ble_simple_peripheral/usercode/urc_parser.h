/* URC解析器头文件 - 统一处理ML307A模块的非请求结果码
 * 设计目标：
 * 1. 统一URC处理入口，消除代码重复
 * 2. 支持异步处理，避免阻塞UART接收
 * 3. 易于扩展，新增URC类型只需添加前缀和处理器
 */

#ifndef _URC_PARSER_H
#define _URC_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "co_printf.h"

/* ============================================================================
 * 常量定义 - 优化内存占用
 * ============================================================================ */

#define URC_QUEUE_SIZE          4       // URC队列深度（减少内存占用）
#define URC_DATA_MAX_LEN        128     // 单条URC数据最大长度（减少内存）
#define URC_TOPIC_MAX_LEN       64      // 主题字符串最大长度（减少内存）
#define URC_PREFIX_TABLE_SIZE   8       // URC前缀表大小

/* ============================================================================
 * URC类型枚举
 * ============================================================================ */

typedef enum {
    URC_NONE = 0,
    URC_MQTT_RECV,         // +MQTTrecv:...  收到MQTT数据
    URC_MQTT_CLOSED,       // +MQTTclosed:   MQTT连接断开
    URC_MQTT_SUB_OK,       // +MQTTsub:      订阅成功
    URC_MQTT_UNSUB_OK,     // +MQTTunsub:    退订成功
    URC_MQTT_PUB_OK,       // +MQTTpub:      发布成功
    URC_SIM_READY,         // +CPIN: READY   SIM卡就绪
    URC_NET_REGISTERED,    // +CREG: 0,1     网络注册成功
    URC_OTHER              // 其他未分类URC
} urc_type_t;

/* ============================================================================
 * URC条目结构 - 队列中的单个URC（优化内存布局）
 * ============================================================================ */

typedef struct {
    uint16_t data_len;                  // 数据长度
    urc_type_t type;                    // URC类型
    char topic[URC_TOPIC_MAX_LEN];      // 解析出的主题 (URC_MQTT_RECV用)
    uint8_t data[URC_DATA_MAX_LEN];     // URC数据内容
} urc_entry_t;

/* ============================================================================
 * URC环形队列
 * ============================================================================ */

typedef struct {
    uint8_t head;                        // 写入位置 (下一个写入位置)
    uint8_t tail;                        // 读取位置 (下一个读取位置)
    uint8_t count;                       // 当前条目数
    urc_entry_t entries[URC_QUEUE_SIZE]; // 队列条目数组
} urc_queue_t;

/* ============================================================================
 * URC前缀表条目 - 用于识别URC类型
 * ============================================================================ */

typedef struct {
    const char *prefix;     // URC前缀字符串
    urc_type_t type;        // 对应的URC类型
    bool has_data;          // 是否包含二进制数据 (如+MQTTrecv)
    bool has_topic;         // 是否包含主题字段
} urc_prefix_entry_t;

/* ============================================================================
 * 函数声明
 * ============================================================================ */

// 队列操作
void urc_queue_init(urc_queue_t *queue);
bool urc_queue_push(urc_queue_t *queue, const urc_entry_t *entry);
bool urc_queue_pop(urc_queue_t *queue, urc_entry_t *entry);
bool urc_queue_is_empty(const urc_queue_t *queue);
bool urc_queue_is_full(const urc_queue_t *queue);
uint8_t urc_queue_count(const urc_queue_t *queue);
void urc_queue_clear(urc_queue_t *queue);

// URC解析
bool urc_parse(uint8_t *buffer, uint16_t len, urc_entry_t *entry);
urc_type_t urc_identify(const uint8_t *buffer, uint16_t len);
bool urc_parse_mqtt_recv(uint8_t *buffer, uint16_t len, urc_entry_t *entry);

// URC处理分发
void urc_process_entry(const urc_entry_t *entry);
const char* urc_type_to_string(urc_type_t type);

#endif /* _URC_PARSER_H */
