/**
 * @file frIRConversion.h
 * @brief 红外信号转换模块头文件
 * @details 支持多种红外协议编解码
 */

#ifndef __FR_IR_CONVERSION_H__
#define __FR_IR_CONVERSION_H__

#include <stdint.h>
#include <stdbool.h>

// 宏定义
#define IR_MAX_DATA_SIZE    256     // 红外数据最大长度
#define IR_MAX_PULSE_COUNT  512     // 最大脉冲数量
#define IR_PULSE_TOLERANCE  200     // 脉冲宽度容差 (us)

// 协议定义
#define IR_PROTOCOL_NEC     0x01    // NEC协议
#define IR_PROTOCOL_SONY    0x02    // Sony协议
#define IR_PROTOCOL_RC5     0x03    // RC5协议
#define IR_PROTOCOL_RC6     0x04    // RC6协议

// 数据结构定义
/**
 * @brief 红外学习数据结构
 */
typedef struct {
    uint16_t raw_data[IR_MAX_PULSE_COUNT];  // 原始脉宽数据
    uint16_t data_len;                      // 数据长度
    uint32_t timestamp;                     // 时间戳
    uint8_t protocol;                       // 协议类型
    uint8_t status;                         // 状态
} TYPEDEFIRLEARNDATA;

/**
 * @brief 红外发射数据结构
 */
typedef struct {
    uint16_t carrier_freq;                  // 载波频率 (kHz)
    uint16_t duty_cycle;                    // 占空比 (%)
    uint16_t pulse_data[IR_MAX_PULSE_COUNT]; // 脉冲数据
    uint16_t pulse_count;                   // 脉冲数量
    uint32_t repeat_count;                  // 重复次数
} TYPEDEFIRTRANSMITDATA;

/**
 * @brief 红外解码结果
 */
typedef struct {
    uint8_t address;                        // 地址码
    uint8_t command;                        // 命令码
    uint8_t protocol;                       // 协议类型
    bool is_repeat;                         // 是否为重复码
    uint32_t timestamp;                     // 解码时间戳
} TYPEDEFIRDECODERESULT;

// 函数声明
/**
 * @brief 红外数据转换为整数
 * @param ESIR_learn 红外学习数据结构
 * @return 转换后的整数值，失败返回-1
 */
int ir_data_to_int(TYPEDEFIRLEARNDATA *ESIR_learn);

/**
 * @brief 整数转换为红外数据
 * @param value 整数值
 * @param protocol 协议类型
 * @param output_data 输出数据结构
 * @return 成功返回true，失败返回false
 */
bool int_to_ir_data(int value, uint8_t protocol, TYPEDEFIRTRANSMITDATA *output_data);

/**
 * @brief 红外信号解码
 * @param raw_data 原始数据
 * @param data_len 数据长度
 * @param result 解码结果
 * @return 解码是否成功
 */
bool ir_decode_signal(uint16_t *raw_data, uint16_t data_len, TYPEDEFIRDECODERESULT *result);

/**
 * @brief 红外信号编码
 * @param address 地址码
 * @param command 命令码
 * @param protocol 协议类型
 * @param output_data 输出数据
 * @return 编码是否成功
 */
bool ir_encode_signal(uint8_t address, uint8_t command, uint8_t protocol, TYPEDEFIRTRANSMITDATA *output_data);

/**
 * @brief 红外数据学习
 * @param learn_data 学习数据结构
 * @return 学习是否成功
 */
bool ir_learn_signal(TYPEDEFIRLEARNDATA *learn_data);

/**
 * @brief 红外信号发射
 * @param transmit_data 发射数据结构
 * @return 发射是否成功
 */
bool ir_transmit_signal(TYPEDEFIRTRANSMITDATA *transmit_data);

/**
 * @brief 红外信号重复检测
 * @param current_data 当前数据
 * @param previous_data 之前数据
 * @param time_diff 时间差 (ms)
 * @return 是否为重复信号
 */
bool ir_is_repeat_signal(TYPEDEFIRLEARNDATA *current_data, TYPEDEFIRLEARNDATA *previous_data, uint32_t time_diff);

/**
 * @brief 红外协议识别
 * @param raw_data 原始数据
 * @param data_len 数据长度
 * @return 识别的协议类型
 */
uint8_t ir_detect_protocol(uint16_t *raw_data, uint16_t data_len);

/**
 * @brief 红外数据验证
 * @param data 数据
 * @param len 长度
 * @return 数据是否有效
 */
bool ir_validate_data(uint16_t *data, uint16_t len);

/**
 * @brief 红外数据压缩
 * @param raw_data 原始数据
 * @param data_len 数据长度
 * @param compressed_data 压缩后数据
 * @param compressed_len 压缩后长度
 * @return 压缩是否成功
 */
bool ir_compress_data(uint16_t *raw_data, uint16_t data_len, uint8_t *compressed_data, uint16_t *compressed_len);

/**
 * @brief 红外数据解压缩
 * @param compressed_data 压缩数据
 * @param compressed_len 压缩长度
 * @param raw_data 解压缩后数据
 * @param data_len 解压缩后长度
 * @return 解压缩是否成功
 */
bool ir_decompress_data(uint8_t *compressed_data, uint16_t compressed_len, uint16_t *raw_data, uint16_t *data_len);

/**
 * @brief 红外学习数据处理（新增）
 * @param learn_data 学习数据结构
 */
void IR_ProcessLearnData(TYPEDEFIRLEARNDATA *learn_data);

/**
 * @brief 获取学习到的红外数据数量
 * @return 数据数量
 */
uint16_t IR_GetLearnedDataCount(void);

/**
 * @brief 清除学习数据
 */
void IR_ClearLearnedData(void);

// 4G模块集成接口（可选）
#ifdef ML307A_MODULE_ENABLE
/**
 * @brief 通过4G模块发布红外数据
 * @param ir_data 红外数据
 * @return 发布是否成功
 */
bool ML307A_IR_Bridge_PublishIRData(TYPEDEFIRLEARNDATA *ir_data);

/**
 * @brief 通过4G模块订阅红外控制命令
 * @param topic MQTT主题
 * @param callback 回调函数
 * @return 订阅是否成功
 */
bool ML307A_IR_Bridge_SubscribeCommand(const char *topic, void (*callback)(const char *data));
#endif

#endif // __FR_IR_CONVERSION_H__