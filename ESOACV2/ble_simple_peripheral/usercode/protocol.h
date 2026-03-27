/**
 * @file protocol.h
 * @brief ESOAC空调节能终端协议解析模块头文件
 * @description 实现ESOAC协议帧的解析和构建
 */

#ifndef _PROTOCOL_H
#define _PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "aircondata.h"

// 协议帧格式定义
#define FRAME_HEADER_1      0x5A
#define FRAME_HEADER_2      0x7A
#define MAX_DATA_LENGTH     254
// Total frame bytes = 2(header) + 1(length) + (1(data_mark)+2(cmd)+N(data)) + 1(checksum) = N + 7
#define FRAME_MAX_SIZE      (MAX_DATA_LENGTH + 7)

// 协议命令定义
typedef enum {
    CMD_HEARTBEAT = 0x0001,          // 心跳
    CMD_DEVICE_INFO = 0x0002,       // 设备信息
    CMD_SET_POWER = 0x0101,          // 设置电源开关
    CMD_SET_MODE = 0x0102,          // 设置模式
    CMD_SET_TEMP = 0x0103,           // 设置温度
    CMD_SET_WIND = 0x0104,           // 设置风速
    CMD_GET_STATUS = 0x0201,         // 获取状态
    CMD_GET_POWER = 0x0202,          // 获取功率
    CMD_GET_TEMP = 0x0203,           // 获取温度
    CMD_IR_LEARN_START = 0x0301,     // 红外学习开始
    CMD_IR_LEARN_STOP = 0x0302,      // 红外学习停止
    CMD_IR_SEND = 0x0303,            // 红外发送
    CMD_IR_LEARN_RESULT = 0x0304,    // 红外学习结果
    CMD_SET_TIMER = 0x0401,          // 设置定时器
    CMD_GET_TIMER = 0x0402,          // 获取定时器
    /* 设置 BLE 名称；载荷 [name_len(1)][name…]。入口：UART1 协议帧 / MQTT 订阅主题 payload / BLE GATT 写(ESAIR) */
    CMD_SET_BLE_NAME = 0x0501,       // 设置BLE设备名称
    CMD_GET_BLE_NAME = 0x0502,       // 获取BLE设备名称
    CMD_SET_MQTT_CONFIG = 0x0503,    // 设置MQTT配置
    CMD_GET_MQTT_CONFIG = 0x0504,    // 获取MQTT配置
    CMD_SYNC_TIME = 0x0204,          // 时间同步(设置RTC)
    CMD_GET_ADC_DATA = 0x0205,       // 获取ADC原始数据
    CMD_IR_READ_DATA = 0x0305,       // 读取红外学习数据
    CMD_IR_SAVE_KEYS = 0x0306,       // 保存红外按键到Flash
    CMD_OTA_START = 0x0601,          // OTA开始
    CMD_OTA_DATA = 0x0602,           // OTA数据
    CMD_OTA_END = 0x0603,            // OTA结束
    CMD_RESPONSE = 0x8000,           // 通用响应
} protocol_cmd_t;

// 数据标记定义
typedef enum {
    DATA_MARK_REQUEST = 0x00,
    DATA_MARK_RESPONSE = 0x01,
    DATA_MARK_NOTIFY = 0x02,
    DATA_MARK_ERROR = 0xFF
} data_mark_t;

// 协议帧结构
typedef struct {
    uint8_t frame_header[2];     // 0x5A 0x7A
    uint8_t data_length;        // 数据长度
    uint8_t data_mark;          // 数据标记
    uint16_t command;           // 命令字
    uint8_t data[MAX_DATA_LENGTH];  // 数据
    uint8_t checksum;           // 校验和
} protocol_frame_t;

// 响应状态定义
typedef enum {
    STATUS_SUCCESS = 0x00,
    STATUS_ERROR_PARAM = 0x01,
    STATUS_ERROR_CMD = 0x02,
    STATUS_ERROR_CRC = 0x03,
    STATUS_ERROR_BUSY = 0x04,
    STATUS_ERROR_STORAGE = 0x05,     // 外部 Flash 不可用或写入失败（如未挂 W25Q）
    STATUS_ERROR_FAIL = 0xFF
} response_status_t;

/**
 * @brief 协议初始化
 */
void protocol_init(void);

/**
 * @brief 计算校验和
 * @param frame 协议帧指针
 * @return 校验和
 */
uint8_t protocol_calc_checksum(protocol_frame_t *frame);

/**
 * @brief 验证校验和
 * @param frame 协议帧指针
 * @return true:有效 false:无效
 */
bool protocol_verify_checksum(protocol_frame_t *frame);

/**
 * @brief 构建协议帧
 * @param frame 协议帧指针
 * @param command 命令字
 * @param data_mark 数据标记
 * @param data 数据指针
 * @param data_len 数据长度
 * @return true:成功 false:失败
 */
bool protocol_build_frame(protocol_frame_t *frame, uint16_t command,
                          uint8_t data_mark, uint8_t *data, uint8_t data_len);

/**
 * @brief 解析协议帧
 * @param buffer 接收缓冲区
 * @param len 缓冲区长度
 * @param frame 输出协议帧
 * @return true:成功 false:失败
 */
bool protocol_parse_frame(uint8_t *buffer, uint16_t len, protocol_frame_t *frame);

/**
 * @brief 处理接收到的协议帧
 * @param frame 协议帧指针
 * @param source 来源(0:BLE, 1:MQTT)
 * @return true:处理成功 false:处理失败
 */
bool protocol_process_frame(protocol_frame_t *frame, uint8_t source);

/**
 * @brief 发送心跳包
 * @param source 来源(0:BLE, 1:MQTT)
 */
void protocol_send_heartbeat(uint8_t source);

/**
 * @brief 发送设备信息
 * @param source 来源(0:BLE, 1:MQTT)
 */
void protocol_send_device_info(uint8_t source);

/**
 * @brief 发送设备状态
 * @param source 来源(0:BLE, 1:MQTT)
 */
void protocol_send_status(uint8_t source);

/**
 * @brief 发送功率数据
 * @param source 来源(0:BLE, 1:MQTT)
 */
void protocol_send_power(uint8_t source);

/**
 * @brief 发送温度数据
 * @param source 来源(0:BLE, 1:MQTT)
 */
void protocol_send_temperature(uint8_t source);

/**
 * @brief 发送响应
 * @param command 命令字
 * @param status 状态
 * @param source 来源(0:BLE, 1:MQTT)
 */
void protocol_send_response(uint16_t command, response_status_t status, uint8_t source);

/**
 * @brief 发送当前BLE设备名称
 * @param source 来源(0:BLE, 1:MQTT)
 */
void protocol_send_ble_name(uint8_t source);

#endif // _PROTOCOL_H
