/**
 * @file frIRConversion_optimized.c
 * @brief 红外信号转换优化版本
 * @details 支持多种红外协议解码，包括 NEC、Sony、RC5
 */

#include "frIRConversion.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* ============================================================================
 *                              调试配置
 * ============================================================================ */
#define IR_DEBUG_ENABLE         1
#define IR_LOG(fmt, ...)        do { if(IR_DEBUG_ENABLE) co_printf("[IR] " fmt "\r\n", ##__VA_ARGS__); } while(0)

/* ============================================================================
 *                              常量定义
 * ============================================================================ */
#ifndef IR_MAX_DATA_SIZE
#define IR_MAX_DATA_SIZE        64
#endif

#ifndef ML307A_MODULE_ENABLE
/* #define ML307A_MODULE_ENABLE */
#endif

/* ============================================================================
 *                              协议时序参数 (单位：us)
 * ============================================================================ */
/* NEC 协议参数 */
#define NEC_START_PULSE_MIN     8500
#define NEC_START_PULSE_MAX     9500
#define NEC_BIT_0_MIN           400
#define NEC_BIT_0_MAX           700
#define NEC_BIT_1_MIN           1400
#define NEC_BIT_1_MAX           1700

/* Sony 协议参数 */
#define SONY_START_PULSE_MIN    2000
#define SONY_START_PULSE_MAX    2800
#define SONY_BIT_LENGTH         600

/* RC5 协议参数 */
#define RC5_START_PULSE_MIN     800
#define RC5_START_PULSE_MAX     900

/* ============================================================================
 *                              信号状态枚举
 * ============================================================================ */
typedef enum {
    IR_STATE_IDLE = 0,
    IR_STATE_START,
    IR_STATE_DATA,
    IR_STATE_STOP,
    IR_STATE_COMPLETE
} IR_State_t;

/* ============================================================================
 *                              协议类型枚举
 * ============================================================================ */
typedef enum {
    IR_PROTOCOL_NONE = 0,
    IR_PROTOCOL_NEC,
    IR_PROTOCOL_SONY,
    IR_PROTOCOL_RC5,
    IR_PROTOCOL_RC6,
    IR_PROTOCOL_UNKNOWN
} IR_Protocol_t;

/* ============================================================================
 *                              内部状态结构体
 * ============================================================================ */
typedef struct {
    IR_State_t state;
    IR_Protocol_t protocol;
    uint32_t start_time;
    uint32_t last_edge_time;
    uint16_t pulse_widths[512];
    uint16_t pulse_count;
    uint8_t decoded_address;
    uint8_t decoded_command;
    uint8_t bit_index;
    bool is_repeating;
    uint32_t repeat_timeout;
} IR_Internal_t;

/* ============================================================================
 *                              全局变量
 * ============================================================================ */
static IR_Internal_t g_ir_state = {0};
static uint8_t g_ir_learn_buffer[IR_MAX_DATA_SIZE] = {0};
static uint16_t g_ir_learn_count = 0;

/* ============================================================================
 *                              内部函数声明
 * ============================================================================ */
static IR_Protocol_t IR_DetectProtocol(const uint16_t *pulse_widths, uint16_t count);
static bool IR_DecodeNEC(const uint16_t *pulse_widths, uint16_t count, uint8_t *address, uint8_t *command);
static bool IR_DecodeSony(const uint16_t *pulse_widths, uint16_t count, uint8_t *address, uint8_t *command);
static bool IR_DecodeRC5(const uint16_t *pulse_widths, uint16_t count, uint8_t *address, uint8_t *command);
static uint16_t IR_GenerateNEC(uint8_t address, uint8_t command, uint16_t *output_buffer);
static uint16_t IR_GenerateSony(uint8_t address, uint8_t command, uint16_t *output_buffer);
static uint16_t IR_GenerateRC5(uint8_t address, uint8_t command, uint16_t *output_buffer);
static bool IR_ValidatePulseWidth(uint16_t measured, uint16_t expected, uint16_t tolerance);
static void IR_ResetState(void);

/* ============================================================================
 *                              外部接口函数
 * ============================================================================ */

/**
 * @brief 将红外数据转换为整数 (优化版)
 * @param ESIR_learn 红外学习数据结构
 * @return 转换后的整数值，失败返回 -1
 */
int ir_data_to_int(TYPEDEFIRLEARNDATA *ESIR_learn)
{
    if (!ESIR_learn || ESIR_learn->data_len == 0) {
        IR_LOG("Invalid input parameters");
        return -1;
    }

    // 添加数据长度上限检查
    if (ESIR_learn->data_len > 512) {
        IR_LOG("Data length exceeds maximum (512)");
        return -1;
    }

    // 重置内部状态
    IR_ResetState();

    // 检测协议类型
    g_ir_state.protocol = IR_DetectProtocol(ESIR_learn->raw_data, ESIR_learn->data_len);

    if (g_ir_state.protocol == IR_PROTOCOL_UNKNOWN) {
        IR_LOG("Unknown IR protocol");
        return -1;
    }

    bool decode_success = false;

    // 根据协议类型进行解码
    switch (g_ir_state.protocol) {
        case IR_PROTOCOL_NEC:
            decode_success = IR_DecodeNEC(ESIR_learn->raw_data, ESIR_learn->data_len,
                                          &g_ir_state.decoded_address, &g_ir_state.decoded_command);
            break;

        case IR_PROTOCOL_SONY:
            decode_success = IR_DecodeSony(ESIR_learn->raw_data, ESIR_learn->data_len,
                                           &g_ir_state.decoded_address, &g_ir_state.decoded_command);
            break;

        case IR_PROTOCOL_RC5:
            decode_success = IR_DecodeRC5(ESIR_learn->raw_data, ESIR_learn->data_len,
                                          &g_ir_state.decoded_address, &g_ir_state.decoded_command);
            break;

        default:
            IR_LOG("Unsupported protocol: %d", g_ir_state.protocol);
            return -1;
    }

    if (decode_success) {
        // 计算返回值：地址 (8 位) + 命令 (8 位) + 协议类型 (4 位)
        int result = ((int)g_ir_state.decoded_address << 12) |
                     ((int)g_ir_state.decoded_command << 4) |
                     (int)g_ir_state.protocol;

        IR_LOG("IR decoded successfully: addr=0x%02X, cmd=0x%02X, protocol=%d",
               g_ir_state.decoded_address, g_ir_state.decoded_command, g_ir_state.protocol);

        return result;
    }

    IR_LOG("IR decode failed");
    return -1;
}

/**
 * @brief 检测红外协议类型
 * @param pulse_widths 脉冲宽度数组
 * @param count 脉冲数量
 * @return 检测到的协议类型
 */
static IR_Protocol_t IR_DetectProtocol(const uint16_t *pulse_widths, uint16_t count)
{
    if (!pulse_widths || count < 4) {
        return IR_PROTOCOL_NONE;
    }

    // 检测 NEC 协议特征
    if (IR_ValidatePulseWidth(pulse_widths[0], 9000, 500) &&
        IR_ValidatePulseWidth(pulse_widths[1], 4500, 500)) {
        return IR_PROTOCOL_NEC;
    }

    // 检测 Sony 协议特征
    if (IR_ValidatePulseWidth(pulse_widths[0], 2400, 400)) {
        return IR_PROTOCOL_SONY;
    }

    // 检测 RC5 协议特征
    if (IR_ValidatePulseWidth(pulse_widths[0], 889, 100) && count >= 26) {
        return IR_PROTOCOL_RC5;
    }

    return IR_PROTOCOL_UNKNOWN;
}

/**
 * @brief NEC 协议解码
 * @param pulse_widths 脉冲宽度数组
 * @param count 脉冲数量
 * @param address 返回地址
 * @param command 返回命令
 * @return 解码是否成功
 */
static bool IR_DecodeNEC(const uint16_t *pulse_widths, uint16_t count, uint8_t *address, uint8_t *command)
{
    if (!pulse_widths || !address || !command || count < 68) {
        return false;
    }

    uint8_t addr_data = 0;
    uint8_t addr_inv = 0;
    uint8_t cmd_data = 0;
    uint8_t cmd_inv = 0;

    // 跳过起始脉冲 (9000us + 4500us)
    uint16_t bit_index = 2;

    // 解码地址 (8 位)
    for (uint8_t i = 0; i < 8; i++) {
        if (bit_index >= count - 1) {
            return false;
        }

        // NEC 协议：逻辑 0 = 560us + 560us, 逻辑 1 = 560us + 1690us
        if (IR_ValidatePulseWidth(pulse_widths[bit_index + 1], 1690, 300)) {
            addr_data |= (1 << (7 - i));
        } else if (!IR_ValidatePulseWidth(pulse_widths[bit_index + 1], 560, 200)) {
            return false;
        }
        bit_index += 2;
    }

    // 解码地址反码
    for (uint8_t i = 0; i < 8; i++) {
        if (bit_index >= count - 1) {
            return false;
        }

        if (IR_ValidatePulseWidth(pulse_widths[bit_index + 1], 1690, 300)) {
            addr_inv |= (1 << (7 - i));
        }
        bit_index += 2;
    }

    // 解码命令
    for (uint8_t i = 0; i < 8; i++) {
        if (bit_index >= count - 1) {
            return false;
        }

        if (IR_ValidatePulseWidth(pulse_widths[bit_index + 1], 1690, 300)) {
            cmd_data |= (1 << (7 - i));
        }
        bit_index += 2;
    }

    // 解码命令反码
    for (uint8_t i = 0; i < 8; i++) {
        if (bit_index >= count - 1) {
            return false;
        }

        if (IR_ValidatePulseWidth(pulse_widths[bit_index + 1], 1690, 300)) {
            cmd_inv |= (1 << (7 - i));
        }
        bit_index += 2;
    }

    // 验证数据完整性 (地址和命令应与其反码互补)
    if ((addr_data != (uint8_t)~addr_inv) || (cmd_data != (uint8_t)~cmd_inv)) {
        IR_LOG("NEC data validation failed: addr=0x%02X(~0x%02X), cmd=0x%02X(~0x%02X)",
               addr_data, addr_inv, cmd_data, cmd_inv);
        return false;
    }

    *address = addr_data;
    *command = cmd_data;

    IR_LOG("NEC decoded: addr=0x%02X, cmd=0x%02X", addr_data, cmd_data);
    return true;
}

/**
 * @brief Sony 协议解码
 * @param pulse_widths 脉冲宽度数组
 * @param count 脉冲数量
 * @param address 返回地址
 * @param command 返回命令
 * @return 解码是否成功
 * @note Sony 协议有 12/15/20 位三种格式，地址可能超过 8 位
 */
static bool IR_DecodeSony(const uint16_t *pulse_widths, uint16_t count, uint8_t *address, uint8_t *command)
{
    if (!pulse_widths || !address || !command || count < 24) {
        return false;
    }

    uint16_t bit_index = 1;
    uint32_t data = 0;
    uint8_t bits_received = 0;

    // Sony 协议：逻辑 0 = 600us + 600us, 逻辑 1 = 1200us + 600us
    while (bit_index < count - 1 && bits_received < 20) {
        uint16_t pulse_width = pulse_widths[bit_index];
        uint16_t space_width = pulse_widths[bit_index + 1];

        if (IR_ValidatePulseWidth(pulse_width, SONY_BIT_LENGTH, 200)) {
            if (IR_ValidatePulseWidth(space_width, SONY_BIT_LENGTH, 200)) {
                data <<= 1;
                bits_received++;
            } else if (IR_ValidatePulseWidth(space_width, SONY_BIT_LENGTH * 2, 200)) {
                data <<= 1;
                data |= 1;
                bits_received++;
            } else {
                break;
            }
        } else {
            break;
        }

        bit_index += 2;
    }

    // 根据接收的位数确定地址和命令
    if (bits_received == 12) {
        // 12 位格式：7 位命令 + 5 位地址
        *command = (data >> 5) & 0x7F;
        *address = data & 0x1F;
    } else if (bits_received == 15) {
        // 15 位格式：7 位命令 + 8 位地址
        *command = (data >> 8) & 0x7F;
        *address = data & 0xFF;
    } else if (bits_received == 20) {
        // 20 位格式：7 位命令 + 13 位地址 (注意：address 是 8 位，会截断高 5 位)
        *command = (data >> 13) & 0x7F;
        *address = data & 0xFF;  // 修复：只取低 8 位，避免溢出
        IR_LOG("Sony 20-bit: high 5 bits of address truncated");
    } else {
        IR_LOG("Sony decode failed: invalid bit count: %d", bits_received);
        return false;
    }

    IR_LOG("Sony decoded: addr=0x%02X, cmd=0x%02X, bits=%d", *address, *command, bits_received);
    return true;
}

/**
 * @brief RC5 协议解码
 * @param pulse_widths 脉冲宽度数组
 * @param count 脉冲数量
 * @param address 返回地址
 * @param command 返回命令
 * @return 解码是否成功
 */
static bool IR_DecodeRC5(const uint16_t *pulse_widths, uint16_t count, uint8_t *address, uint8_t *command)
{
    if (!pulse_widths || !address || !command || count < 26) {
        return false;
    }

    uint32_t data = 0;
    uint8_t bit_count = 0;

    // RC5 协议使用曼彻斯特编码，每位由两个脉冲组成
    for (uint16_t i = 0; i < count - 1 && bit_count < 14; i++) {
        uint16_t duration = pulse_widths[i];

        if (IR_ValidatePulseWidth(duration, 889, 200)) {
            if (i + 1 < count) {
                uint16_t next_duration = pulse_widths[i + 1];

                if (IR_ValidatePulseWidth(next_duration, 889, 200)) {
                    // 两个相同长度的脉冲表示逻辑 0
                    data <<= 1;
                    bit_count++;
                    i++;
                } else if (IR_ValidatePulseWidth(next_duration, 1778, 300)) {
                    // 一个长脉冲表示逻辑 1
                    data <<= 1;
                    data |= 1;
                    bit_count++;
                    i++;
                }
            }
        }
    }

    if (bit_count >= 14) {
        // RC5 格式：2 位起始 + 1 位翻转 + 5 位地址 + 6 位命令
        *address = (data >> 6) & 0x1F;
        *command = data & 0x3F;

        IR_LOG("RC5 decoded: addr=0x%02X, cmd=0x%02X", *address, *command);
        return true;
    }

    IR_LOG("RC5 decode failed: insufficient bits: %d", bit_count);
    return false;
}

/**
 * @brief 验证脉冲宽度是否在预期范围内
 * @param measured 测量值
 * @param expected 期望值
 * @param tolerance 容差
 * @return 是否在有效范围内
 */
static bool IR_ValidatePulseWidth(uint16_t measured, uint16_t expected, uint16_t tolerance)
{
    int32_t diff = (int32_t)measured - (int32_t)expected;
    if (diff < 0) {
        diff = -diff;
    }

    return (diff <= tolerance);
}

/**
 * @brief 重置内部状态
 */
static void IR_ResetState(void)
{
    memset(&g_ir_state, 0, sizeof(g_ir_state));
    g_ir_state.state = IR_STATE_IDLE;
    g_ir_state.protocol = IR_PROTOCOL_NONE;
}

/**
 * @brief 处理红外学习数据 (优化版本)
 * @param learn_data 学习数据结构
 */
void IR_ProcessLearnData(TYPEDEFIRLEARNDATA *learn_data)
{
    if (!learn_data) {
        return;
    }

    int result = ir_data_to_int(learn_data);

    if (result != -1) {
        // 解码成功，保存到学习缓冲区
        if (g_ir_learn_count < IR_MAX_DATA_SIZE) {
            g_ir_learn_buffer[g_ir_learn_count++] = (uint8_t)(result & 0xFF);
            IR_LOG("IR data saved to buffer[%d]", g_ir_learn_count - 1);
        } else {
            IR_LOG("Learn buffer full");
        }

        // 通过 4G 模块上报 (条件编译)
        #ifdef ML307A_MODULE_ENABLE
        ML307A_IR_Bridge_PublishIRData(learn_data);
        #endif
    }
}

/**
 * @brief 获取学习到的红外数据数量
 * @return 数据数量
 */
uint16_t IR_GetLearnedDataCount(void)
{
    return g_ir_learn_count;
}

/**
 * @brief 清除学习数据
 */
void IR_ClearLearnedData(void)
{
    memset(g_ir_learn_buffer, 0, sizeof(g_ir_learn_buffer));
    g_ir_learn_count = 0;
    IR_LOG("Learned data cleared");
}

/* ============================================================================
 *                              未实现函数占位 (保持接口完整)
 * ============================================================================ */
static uint16_t IR_GenerateNEC(uint8_t address, uint8_t command, uint16_t *output_buffer)
{
    (void)address;
    (void)command;
    (void)output_buffer;
    return 0;
}

static uint16_t IR_GenerateSony(uint8_t address, uint8_t command, uint16_t *output_buffer)
{
    (void)address;
    (void)command;
    (void)output_buffer;
    return 0;
}

static uint16_t IR_GenerateRC5(uint8_t address, uint8_t command, uint16_t *output_buffer)
{
    (void)address;
    (void)command;
    (void)output_buffer;
    return 0;
}