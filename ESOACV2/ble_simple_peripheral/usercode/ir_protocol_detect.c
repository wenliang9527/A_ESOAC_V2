/* 红外协议识别实现 */

#include "ir_protocol_detect.h"
#include "co_printf.h"
#include <string.h>

/* 协议特征数据库 */
static const proto_feature_t proto_db[] = {
    {IR_PROTO_NEC,   "NEC",   38000, 2000, 9000, 4500, 500, 32},
    {IR_PROTO_GREE,  "GREE",  38000, 2000, 9000, 4500, 800, 32},
    {IR_PROTO_MIDEA, "MIDEA", 38000, 2000, 4400, 4400, 500, 35},
    {IR_PROTO_HAIER, "HAIER", 38000, 2000, 8000, 4000, 500, 24},
    {IR_PROTO_CHIGO, "CHIGO", 38000, 2000, 9000, 4500, 500, 32},
    {IR_PROTO_AUX,   "AUX",   38000, 2000, 9000, 4500, 500, 32},
    {IR_PROTO_SONY,  "SONY",  40000, 2000, 2400, 600,  300, 12},
    {IR_PROTO_RC5,   "RC5",   36000, 2000, 0,    0,    0,   14},
    {IR_PROTO_RC6,   "RC6",   36000, 2000, 2666, 889,  200, 20},
};

/* 获取协议名称 */
const char* IR_GetProtocolName(ir_protocol_type_t type)
{
    switch (type) {
        case IR_PROTO_NEC:   return "NEC";
        case IR_PROTO_GREE:  return "GREE";
        case IR_PROTO_MIDEA: return "MIDEA";
        case IR_PROTO_HAIER: return "HAIER";
        case IR_PROTO_CHIGO: return "CHIGO";
        case IR_PROTO_AUX:   return "AUX";
        case IR_PROTO_SONY:  return "SONY";
        case IR_PROTO_RC5:   return "RC5";
        case IR_PROTO_RC6:   return "RC6";
        case IR_PROTO_CUSTOM: return "CUSTOM";
        default:             return "UNKNOWN";
    }
}

/* 获取按键功能名称 */
const char* IR_GetKeyFunctionName(key_function_type_t func)
{
    switch (func) {
        case KEY_FUNC_POWER:     return "POWER";
        case KEY_FUNC_MODE:      return "MODE";
        case KEY_FUNC_TEMP_UP:   return "TEMP_UP";
        case KEY_FUNC_TEMP_DOWN: return "TEMP_DOWN";
        case KEY_FUNC_WIND:      return "WIND";
        case KEY_FUNC_TIMER:     return "TIMER";
        case KEY_FUNC_SLEEP:     return "SLEEP";
        case KEY_FUNC_SWING:     return "SWING";
        case KEY_FUNC_ECO:       return "ECO";
        default:                 return "UNKNOWN";
    }
}

/* 获取温度步进名称 */
const char* IR_GetTempStepName(temp_step_type_t step)
{
    switch (step) {
        case TEMP_STEP_0_5: return "0.5";
        case TEMP_STEP_1_0: return "1.0";
        case TEMP_STEP_2_0: return "2.0";
        default:            return "UNKNOWN";
    }
}

/* 获取温度控制类型名称 */
const char* IR_GetTempCtrlName(temp_control_type_t ctrl)
{
    switch (ctrl) {
        case TEMP_CTRL_INCREMENTAL: return "INCREMENTAL";
        case TEMP_CTRL_DIRECT:      return "DIRECT";
        case TEMP_CTRL_CYCLE:       return "CYCLE";
        default:                    return "UNKNOWN";
    }
}

/* 识别协议类型 */
ir_protocol_type_t IR_DetectProtocol(TYPEDEFIRLEARNDATA *key)
{
    if (key->ir_learn_data_cnt < 4) {
        return IR_PROTO_UNKNOWN;
    }
    
    uint32_t carrier = key->ir_carrier_fre;
    uint32_t lead_low = key->ir_learn_Date[0];
    uint32_t lead_high = key->ir_learn_Date[1];
    uint8_t data_bits = (key->ir_learn_data_cnt - 2) / 2;
    
    co_printf("IR_DetectProtocol: carrier=%dHz, lead=%d/%d, bits=%d\r\n",
              carrier, lead_low, lead_high, data_bits);
    
    for (uint8_t i = 0; i < sizeof(proto_db)/sizeof(proto_db[0]); i++) {
        const proto_feature_t *p = &proto_db[i];
        
        /* 检查载波频率 */
        if (carrier < p->carrier_freq - p->carrier_tolerance ||
            carrier > p->carrier_freq + p->carrier_tolerance) {
            continue;
        }
        
        /* RC5没有引导码，特殊处理 */
        if (p->type == IR_PROTO_RC5) {
            if (data_bits >= 12 && data_bits <= 16) {
                co_printf("IR_DetectProtocol: Detected %s protocol\r\n", p->name);
                return p->type;
            }
            continue;
        }
        
        /* 检查引导码 */
        if (lead_low < p->lead_low_us - p->lead_tolerance ||
            lead_low > p->lead_low_us + p->lead_tolerance) {
            continue;
        }
        
        if (lead_high < p->lead_high_us - p->lead_tolerance ||
            lead_high > p->lead_high_us + p->lead_tolerance) {
            continue;
        }
        
        /* 检查数据位数 */
        if (data_bits == p->data_bits || data_bits == p->data_bits + 1) {
            co_printf("IR_DetectProtocol: Detected %s protocol\r\n", p->name);
            return p->type;
        }
    }
    
    co_printf("IR_DetectProtocol: Unknown protocol, using CUSTOM\r\n");
    return IR_PROTO_CUSTOM;
}

/* 根据按键索引推断功能类型 */
key_function_type_t IR_DetectKeyFunction(uint8_t key_index)
{
    static const key_function_type_t key_map[] = {
        KEY_FUNC_POWER,      /* 0 */
        KEY_FUNC_MODE,       /* 1 */
        KEY_FUNC_TEMP_UP,    /* 2 */
        KEY_FUNC_TEMP_DOWN,  /* 3 */
        KEY_FUNC_WIND,       /* 4 */
        KEY_FUNC_UNKNOWN,    /* 5 */
        KEY_FUNC_UNKNOWN,    /* 6 */
        KEY_FUNC_UNKNOWN,    /* 7 */
        KEY_FUNC_UNKNOWN,    /* 8 */
        KEY_FUNC_UNKNOWN,    /* 9 */
        KEY_FUNC_UNKNOWN,    /* 10 */
        KEY_FUNC_UNKNOWN     /* 11 */
    };
    
    if (key_index < sizeof(key_map)/sizeof(key_map[0])) {
        return key_map[key_index];
    }
    
    return KEY_FUNC_UNKNOWN;
}

/* 计算红外码特征哈希 */
uint32_t IR_CalcIRHash(TYPEDEFIRLEARNDATA *key)
{
    uint32_t hash = 0;
    
    /* 使用数据部分计算哈希 (跳过引导码，最多使用8个数据) */
    for (uint8_t i = 2; i < key->ir_learn_data_cnt && i < 10; i++) {
        hash = hash * 31 + (key->ir_learn_Date[i] & 0xFFFF);
    }
    
    return hash;
}

/* 统计两个红外码的差异程度 */
uint8_t IR_CountBitDifference(TYPEDEFIRLEARNDATA *key1, TYPEDEFIRLEARNDATA *key2)
{
    uint8_t diff_count = 0;
    uint8_t min_cnt = (key1->ir_learn_data_cnt < key2->ir_learn_data_cnt) ? 
                      key1->ir_learn_data_cnt : key2->ir_learn_data_cnt;
    
    for (uint8_t i = 2; i < min_cnt; i++) {
        /* 如果时间差异超过25%，认为是不同的 */
        int32_t diff = (int32_t)key1->ir_learn_Date[i] - (int32_t)key2->ir_learn_Date[i];
        int32_t threshold = (int32_t)key1->ir_learn_Date[i] / 4;
        if (diff < 0) diff = -diff;
        if (threshold < 0) threshold = -threshold;
        
        if (diff > threshold) {
            diff_count++;
        }
    }
    
    return diff_count;
}

/* 识别温度步进 */
temp_step_type_t IR_DetectTempStep(TYPEDEFIRLEARNDATA *up, TYPEDEFIRLEARNDATA *down)
{
    uint8_t diff = IR_CountBitDifference(up, down);
    
    if (diff <= 1) {
        return TEMP_STEP_1_0;
    } else if (diff <= 2) {
        /* 可能是0.5度或2度，默认1度 */
        return TEMP_STEP_1_0;
    } else {
        return TEMP_STEP_1_0;
    }
}

/* 分析温度键 */
void IR_AnalyzeTempKey(uint8_t temp_up_idx, uint8_t temp_down_idx)
{
    TYPEDEFIRLEARNDATA *up = &ESairkey.airbutton[temp_up_idx];
    TYPEDEFIRLEARNDATA *down = &ESairkey.airbutton[temp_down_idx];
    
    /* 设置功能类型 */
    up->key_function = KEY_FUNC_TEMP_UP;
    down->key_function = KEY_FUNC_TEMP_DOWN;
    
    /* 关联两个按键 */
    up->paired_key_index = temp_down_idx;
    down->paired_key_index = temp_up_idx;
    
    /* 计算哈希值对比 */
    uint32_t up_hash = IR_CalcIRHash(up);
    uint32_t down_hash = IR_CalcIRHash(down);
    
    co_printf("IR_AnalyzeTempKey: up_hash=0x%08X, down_hash=0x%08X\r\n", up_hash, down_hash);
    
    if (up_hash == down_hash) {
        /* 相同，可能是循环键 */
        up->temp_ctrl_type = TEMP_CTRL_CYCLE;
        down->temp_ctrl_type = TEMP_CTRL_CYCLE;
        up->temp_step = TEMP_STEP_1_0;
        down->temp_step = TEMP_STEP_1_0;
        co_printf("IR_AnalyzeTempKey: Detected CYCLE type\r\n");
    } else {
        /* 不同，是+/-键 */
        up->temp_ctrl_type = TEMP_CTRL_INCREMENTAL;
        down->temp_ctrl_type = TEMP_CTRL_INCREMENTAL;
        
        /* 识别步进值 */
        up->temp_step = IR_DetectTempStep(up, down);
        down->temp_step = up->temp_step;
        
        co_printf("IR_AnalyzeTempKey: Detected INCREMENTAL type, step=%s\r\n",
                  IR_GetTempStepName(up->temp_step));
    }
}

/* 分析电源键 */
void IR_AnalyzePowerKey(uint8_t power_key_idx)
{
    TYPEDEFIRLEARNDATA *key = &ESairkey.airbutton[power_key_idx];
    key->key_function = KEY_FUNC_POWER;
    key->cycle_count = 2;  /* 开关两档 */
}

/* 分析模式键 */
void IR_AnalyzeModeKey(uint8_t mode_key_idx)
{
    TYPEDEFIRLEARNDATA *key = &ESairkey.airbutton[mode_key_idx];
    key->key_function = KEY_FUNC_MODE;
    key->cycle_count = 6;  /* 默认6种模式 */
}

/* 分析风速键 */
void IR_AnalyzeWindKey(uint8_t wind_key_idx)
{
    TYPEDEFIRLEARNDATA *key = &ESairkey.airbutton[wind_key_idx];
    key->key_function = KEY_FUNC_WIND;
    key->cycle_count = 3;  /* 默认3档风速 */
}

/* 学习完成后自动分析 */
void IR_AutoAnalyzeAfterLearn(uint8_t key_index)
{
    TYPEDEFIRLEARNDATA *key = &ESairkey.airbutton[key_index];
    
    co_printf("\r\n========== Key %d Analysis ==========\r\n", key_index);
    
    /* 识别协议 */
    key->protocol_type = IR_DetectProtocol(key);
    
    /* 识别功能类型 */
    key->key_function = IR_DetectKeyFunction(key_index);
    
    /* 根据按键类型进行专项分析 */
    switch (key->key_function) {
        case KEY_FUNC_POWER:
            IR_AnalyzePowerKey(key_index);
            break;
            
        case KEY_FUNC_MODE:
            IR_AnalyzeModeKey(key_index);
            break;
            
        case KEY_FUNC_WIND:
            IR_AnalyzeWindKey(key_index);
            break;
            
        case KEY_FUNC_TEMP_UP:
            /* 温度键需要配对分析 */
            if (ESairkey.keyExistence[3]) {
                IR_AnalyzeTempKey(2, 3);
            } else {
                key->key_function = KEY_FUNC_TEMP_UP;
                key->temp_ctrl_type = TEMP_CTRL_UNKNOWN;
                key->temp_step = TEMP_STEP_UNKNOWN;
            }
            break;
            
        case KEY_FUNC_TEMP_DOWN:
            /* 温度键需要配对分析 */
            if (ESairkey.keyExistence[2]) {
                IR_AnalyzeTempKey(2, 3);
            } else {
                key->key_function = KEY_FUNC_TEMP_DOWN;
                key->temp_ctrl_type = TEMP_CTRL_UNKNOWN;
                key->temp_step = TEMP_STEP_UNKNOWN;
            }
            break;
            
        default:
            break;
    }
    
    /* 打印识别结果 */
    co_printf("Protocol: %s\r\n", IR_GetProtocolName(key->protocol_type));
    co_printf("Function: %s\r\n", IR_GetKeyFunctionName(key->key_function));
    
    if (key->key_function == KEY_FUNC_TEMP_UP || key->key_function == KEY_FUNC_TEMP_DOWN) {
        co_printf("Temp Control: %s\r\n", IR_GetTempCtrlName(key->temp_ctrl_type));
        co_printf("Temp Step: %s degree\r\n", IR_GetTempStepName(key->temp_step));
        co_printf("Paired Key: %d\r\n", key->paired_key_index);
    }
    
    if (key->cycle_count > 0) {
        co_printf("Cycle Count: %d\r\n", key->cycle_count);
    }
    
    co_printf("====================================\r\n");
}
