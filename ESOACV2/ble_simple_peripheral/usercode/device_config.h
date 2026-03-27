/**
 * @file device_config.h
 * @brief 设备配置持久化存储模块
 * @description 管理设备名称等配置参数的SPI Flash读写
 */

#ifndef _DEVICE_CONFIG_H
#define _DEVICE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 *                              存储地址
 * ============================================================================ */
#define DEVICE_CONFIG_FLASH_ADDR    (4096 * 1)   // 使用Flash数据区1

/* ============================================================================
 *                              参数限制
 * ============================================================================ */
#define BLE_NAME_MAX_LEN            20            // BLE设备名称最大长度
#define MQTT_ADDR_MAX_LEN           128           // MQTT服务器地址最大长度
#define MQTT_PORT_MAX_LEN           6             // MQTT端口号最大长度
#define MQTT_CLIENT_ID_MAX_LEN      128           // MQTT客户端ID最大长度
#define MQTT_USER_MAX_LEN           128           // MQTT用户名最大长度
#define MQTT_PASS_MAX_LEN           128           // MQTT密码最大长度
#define MQTT_TOPIC_MAX_LEN          128           // MQTT主题最大长度
#define DEVICE_CONFIG_MAGIC         0x45534F43   // 魔数 "ESOC"，用于校验Flash数据有效性
#define MQTT_CONFIG_MAGIC           0x4D515454   // 魔数 "MQTT"，用于MQTT配置区有效性校验

/* ============================================================================
 *                              名称校验结果
 * ============================================================================ */
typedef enum {
    NAME_OK = 0,                   // 名称有效
    NAME_ERR_EMPTY,               // 名称为空
    NAME_ERR_TOO_LONG,            // 名称过长
    NAME_ERR_INVALID_CHAR,        // 包含非法字符
    NAME_ERR_FLASH_FAIL,          // Flash操作失败
    NAME_ERR_MAGIC_MISMATCH,      // Flash数据校验失败
} ble_name_status_t;

/* ============================================================================
 *                              设备配置结构体
 * ============================================================================ */
typedef struct {
    uint32_t magic;                             // 魔数标识
    uint8_t  ble_name[BLE_NAME_MAX_LEN + 1];   // BLE设备名称 (null-terminated)
    uint8_t  ble_name_len;                      // 名称实际长度
    uint8_t  reserved[23];                      // 预留，对齐到32字节
} device_config_t;

/* ============================================================================
 *                        MQTT配置持久化结构体
 * ============================================================================ */
#define MQTT_CONFIG_FLASH_ADDR    (4096 * 2)   // 使用Flash数据区2

typedef struct {
    uint32_t magic;                                     // 魔数标识 MQTT_CONFIG_MAGIC
    char server_addr[MQTT_ADDR_MAX_LEN];                // 服务器地址
    char server_port[MQTT_PORT_MAX_LEN];                // 服务器端口
    char client_id[MQTT_CLIENT_ID_MAX_LEN];             // 客户端ID
    char username[MQTT_USER_MAX_LEN];                   // 用户名
    char password[MQTT_PASS_MAX_LEN];                   // 密码
    char subscribe_topic[MQTT_TOPIC_MAX_LEN];           // 订阅主题
    char publish_topic[MQTT_TOPIC_MAX_LEN];             // 发布主题
    uint8_t reserved[32];                               // 预留扩展
} mqtt_config_storage_t;

/* ============================================================================
 *                              公共函数
 * ============================================================================ */

/**
 * @brief 从SPI Flash加载设备配置
 * @note 如果Flash中没有有效数据，则使用默认配置
 */
void device_config_load(void);

/**
 * @brief 保存设备配置到SPI Flash
 * @return true:保存成功 false:保存失败
 */
bool device_config_save(void);

/**
 * @brief 校验设备名称合法性
 * @param name 名称字符串
 * @param len  名称长度
 * @return 校验结果 (ble_name_status_t)
 */
ble_name_status_t device_config_validate_name(const char *name, uint8_t len);

/**
 * @brief 设置BLE设备名称
 * @param name     名称字符串
 * @param len      名称长度 (不包含null终止符)
 * @param save     true:同时保存到Flash false:仅内存更新
 * @return 校验结果 (ble_name_status_t)
 */
ble_name_status_t device_config_set_ble_name(const char *name, uint8_t len, bool save);

/**
 * @brief 获取当前BLE设备名称
 * @return 设备名称字符串指针 (静态缓冲区，无需释放)
 */
const char *device_config_get_ble_name(void);

/**
 * @brief 获取当前BLE设备名称长度
 * @return 名称长度
 */
uint8_t device_config_get_ble_name_len(void);

/**
 * @brief 恢复默认设备名称
 * @param save true:同时保存到Flash
 */
void device_config_reset_default(bool save);

/* ============================================================================
 *                      MQTT配置持久化函数
 * ============================================================================ */

/**
 * @brief 从Flash加载MQTT配置到g_mqtt_config
 * @note 如果Flash中无有效配置，保持g_mqtt_config的默认值不变
 * @return true:加载成功（Flash中有有效配置） false:无有效配置（使用默认值）
 */
bool mqtt_config_load(void);

/**
 * @brief 将当前g_mqtt_config保存到Flash
 * @return true:保存成功 false:保存失败
 */
bool mqtt_config_save(void);

/**
 * @brief 从Flash擦除MQTT配置（恢复为编译时默认值）
 * @return true:擦除成功 false:擦除失败
 */
bool mqtt_config_erase(void);

#endif // _DEVICE_CONFIG_H
