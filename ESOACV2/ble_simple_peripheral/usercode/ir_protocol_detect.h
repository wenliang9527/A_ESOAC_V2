/* 红外协议识别头文件 */

#ifndef _IR_PROTOCOL_DETECT_H
#define _IR_PROTOCOL_DETECT_H

#include <stdint.h>
#include <stdbool.h>
#include "frIRConversion.h"

/* 协议特征结构 */
typedef struct {
    ir_protocol_type_t type;
    const char *name;
    uint32_t carrier_freq;
    uint32_t carrier_tolerance;
    uint32_t lead_low_us;
    uint32_t lead_high_us;
    uint32_t lead_tolerance;
    uint8_t data_bits;
} proto_feature_t;

/* 协议识别函数 */
ir_protocol_type_t IR_DetectProtocol(TYPEDEFIRLEARNDATA *key);
const char* IR_GetProtocolName(ir_protocol_type_t type);

/* 按键功能识别 */
key_function_type_t IR_DetectKeyFunction(uint8_t key_index);
const char* IR_GetKeyFunctionName(key_function_type_t func);

/* 温度键分析 */
void IR_AnalyzeTempKey(uint8_t temp_up_idx, uint8_t temp_down_idx);
temp_step_type_t IR_DetectTempStep(TYPEDEFIRLEARNDATA *up, TYPEDEFIRLEARNDATA *down);
const char* IR_GetTempStepName(temp_step_type_t step);
const char* IR_GetTempCtrlName(temp_control_type_t ctrl);

/* 其他按键分析 */
void IR_AnalyzePowerKey(uint8_t power_key_idx);
void IR_AnalyzeModeKey(uint8_t mode_key_idx);
void IR_AnalyzeWindKey(uint8_t wind_key_idx);

/* 自动分析入口 */
void IR_AutoAnalyzeAfterLearn(uint8_t key_index);

/* 工具函数 */
uint32_t IR_CalcIRHash(TYPEDEFIRLEARNDATA *key);
uint8_t IR_CountBitDifference(TYPEDEFIRLEARNDATA *key1, TYPEDEFIRLEARNDATA *key2);

#endif
