/**
 * @file ML307A_IR_Bridge.h
 * @brief ML307A模块红外数据桥接头文件
 * @details 定义红外数据通过4G网络传输的接口
 */

#ifndef __ML307A_IR_BRIDGE_H__
#define __ML307A_IR_BRIDGE_H__

#include <stdint.h>
#include <stdbool.h>
#include "frIRConversion_optimized.h"

// 配置参数
#define IR_BRIDGE_HEARTBEAT_INTERVAL    30000    // 心跳间隔 (ms)
#define IR_BRIDGE_MAX_RETRY_COUNT       3        // 最大重试次数
#define IR_BRIDGE_RETRY_DELAY           5000     // 重试延迟 (ms)

/**
 * @brief 红外桥接状态枚举
 */
typedef enum {
    IR_BRIDGE_OK = 0,
    IR_BRIDGE_ERROR_INIT,
    IR_BRIDGE_ERROR_NETWORK,
    IR_BRIDGE_ERROR_MQTT,
    IR_BRIDGE_ERROR_DATA,
    IR_BRIDGE_ERROR_TIMEOUT
} IR_BridgeError_t;

/**
 * @brief 红外桥接配置结构体
 */
typedef struct {
    char mqtt_server[64];           // MQTT服务器地址
    uint16_t mqtt_port;             // MQTT服务器端口
    char mqtt_client_id[32];        // MQTT客户端ID
    char mqtt_username[32];         // MQTT用户名
    char mqtt_password[32];         // MQTT密码
    char device_id[32];             // 设备ID
    uint32_t publish_interval;      // 数据发布间隔 (ms)
    bool enable_compression;        // 是否启用数据压缩
    bool enable_encryption;         // 是否启用数据加密
} IR_BridgeConfig_t;

/**
 * @brief 红外桥接统计信息
 */
typedef struct {
    uint32_t total_published;       // 总发布数量
    uint32_t total_received;        // 总接收数量
    uint32_t failed_publishes;      // 失败发布数量
    uint32_t failed_receives;       // 失败接收数量
    uint32_t last_publish_time;     // 最后发布时间
    uint32_t last_receive_time;     // 最后接收时间
    uint32_t connection_uptime;     // 连接运行时间
} IR_BridgeStats_t;

// 函数声明

/**
 * @brief 初始化红外数据桥接
 * @return 初始化是否成功
 */
bool ML307A_IR_Bridge_Init(void);

/**
 * @brief 发布红外数据到云端
 * @param ir_data 红外数据
 * @return 发布是否成功
 */
bool ML307A_IR_Bridge_PublishIRData(TYPEDEFIRLEARNDATA *ir_data);

/**
 * @brief 订阅红外控制命令
 * @param topic MQTT主题
 * @param callback 回调函数
 * @return 订阅是否成功
 */
bool ML307A_IR_Bridge_SubscribeCommand(const char *topic, void (*callback)(const char *data));

/**
 * @brief 定期处理红外桥接任务
 */
void ML307A_IR_Bridge_Process(void);

/**
 * @brief 去初始化红外桥接
 */
void ML307A_IR_Bridge_Deinit(void);

/**
 * @brief 获取红外桥接状态
 * @return 桥接状态
 */
uint8_t ML307A_IR_Bridge_GetStatus(void);

/**
 * @brief 获取红外桥接统计信息
 * @param stats 统计信息结构体指针
 * @return 获取是否成功
 */
bool ML307A_IR_Bridge_GetStats(IR_BridgeStats_t *stats);

/**
 * @brief 重置红外桥接统计信息
 */
void ML307A_IR_Bridge_ResetStats(void);

/**
 * @brief 配置红外桥接参数
 * @param config 配置结构体
 * @return 配置是否成功
 */
bool ML307A_IR_Bridge_Configure(IR_BridgeConfig_t *config);

/**
 * @brief 批量发布红外数据
 * @param ir_data_array 红外数据数组
 * @param data_count 数据数量
 * @return 发布是否成功
 */
bool ML307A_IR_Bridge_PublishBatch(TYPEDEFIRLEARNDATA *ir_data_array, uint16_t data_count);

/**
 * @brief 红外数据压缩发布
 * @param ir_data 红外数据
 * @return 发布是否成功
 */
bool ML307A_IR_Bridge_PublishCompressed(TYPEDEFIRLEARNDATA *ir_data);

/**
 * @brief 红外数据加密发布
 * @param ir_data 红外数据
 * @return 发布是否成功
 */
bool ML307A_IR_Bridge_PublishEncrypted(TYPEDEFIRLEARNDATA *ir_data);

/**
 * @brief 红外桥接错误处理
 * @param error_code 错误代码
 * @return 错误描述字符串
 */
const char* ML307A_IR_Bridge_GetErrorString(IR_BridgeError_t error_code);

/**
 * @brief 红外桥接自检
 * @return 自检是否通过
 */
bool ML307A_IR_Bridge_SelfTest(void);

/**
 * @brief 红外桥接心跳包发送
 * @return 发送是否成功
 */
bool ML307A_IR_Bridge_SendHeartbeat(void);

/**
 * @brief 红外桥接连接状态检查
 * @return 连接是否正常
 */
bool ML307A_IR_Bridge_IsConnected(void);

/**
 * @brief 红外桥接重连
 * @return 重连是否成功
 */
bool ML307A_IR_Bridge_Reconnect(void);

/**
 * @brief 红外桥接故障恢复
 * @return 恢复是否成功
 */
bool ML307A_IR_Bridge_Recover(void);

/**
 * @brief 红外桥接日志记录
 * @param level 日志级别
 * @param format 格式化字符串
 * @param ... 可变参数
 */
void ML307A_IR_Bridge_Log(int level, const char *format, ...);

// 调试接口
#ifdef IR_BRIDGE_DEBUG_ENABLE
/**
 * @brief 红外桥接调试信息获取
 * @param debug_info 调试信息缓冲区
 * @param buffer_size 缓冲区大小
 * @return 获取是否成功
 */
bool ML307A_IR_Bridge_GetDebugInfo(char *debug_info, uint16_t buffer_size);

/**
 * @brief 红外桥接性能测试
 * @param test_duration 测试持续时间 (ms)
 * @param test_results 测试结果
 * @return 测试是否成功
 */
bool ML307A_IR_Bridge_PerformanceTest(uint32_t test_duration, uint32_t *test_results);
#endif

#endif // __ML307A_IR_BRIDGE_H__