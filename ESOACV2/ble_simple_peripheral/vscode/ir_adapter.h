/**
 * @file ir_adapter.h
 * @brief 红外转换适配器头文件
 * @details 集成原始frIRConversion.c和优化版本frIRConversion_optimized.c
 */

#ifndef __IR_ADAPTER_H__
#define __IR_ADAPTER_H__

#include "frIRConversion_optimized.h"
#include <stdint.h>
#include <stdbool.h>

// 原始代码中的数据结构定义（从frIRConversion.c提取）
#define IRLEADLOW               9000    // NEC引导码低电平时间 (us)
#define IRLEADHIGHT             4500    // NEC引导码高电平时间 (us)
#define IRLOGIC0LOW             560     // NEC逻辑0低电平时间 (us)
#define IRLOGIC0HIGHT           560     // NEC逻辑0高电平时间 (us)
#define IRLOGIC1LOW             560     // NEC逻辑1低电平时间 (us)
#define IRLOGIC1HIGHT           1690    // NEC逻辑1高电平时间 (us)
#define IRSTOPLOW               560     // NEC停止码低电平时间 (us)
#define IRSTOPHIGHT             560     // NEC停止码高电平时间 (us)

#define LERANDATACNTMAX         256     // 学习数据最大数量
#define LERANDATABUFMAX         1024    // 学习数据缓冲区大小

// 红外载波频率定义
#define IR_CF_36K               36000   // 36kHz载波频率
#define IR_CF_38K               38000   // 38kHz载波频率
#define IR_CF_56K               56000   // 56kHz载波频率

// 红外模式定义
#define IR_SEND                 0       // 发射模式
#define IR_LEARN                1       // 学习模式

// 原始代码中的数据结构
typedef struct {
    uint16_t IR_Pwm_State_Date[LERANDATACNTMAX];  // PWM状态数据
    uint16_t IR_pwm_Num;                          // PWM数据数量
    uint16_t ir_carrier_fre;                      // 载波频率
    uint8_t IR_pwm_state;                         // PWM状态
    uint16_t IR_pwm_SendNum;                      // 当前发送位置
    bool IR_Busy;                                 // 忙碌状态
    bool loop;                                    // 循环发送标志
    uint32_t total_count;                         // 总计数
    uint32_t high_count_half;                     // 高电平计数一半
} TYPEDEFIRPWMTIM;

typedef struct {
    uint32_t ir_learn_Date[LERANDATACNTMAX];      // 学习数据
    uint16_t ir_learn_data_cnt;                   // 学习数据数量
    uint16_t ir_carrier_fre;                      // 载波频率
    uint8_t IR_learn_state;                       // 学习状态
} TYPEDEFIRLEARNDATA;

typedef struct {
    uint32_t ir_learn_data_buf[LERANDATABUFMAX];  // 学习数据缓冲区
    uint16_t ir_learn_data_buf_cnt;               // 学习数据缓冲区计数
    uint16_t ir_carrier_times;                    // 载波次数
    uint8_t ir_learn_step;                        // 学习步骤
    uint8_t ir_timer_cnt;                         // 定时器计数
    uint8_t ir_learn_start;                       // 学习开始标志
} TYPEDEFIRLEARN;

// 空调红外数据结构（从原始代码提取）
typedef struct {
    uint8_t power;          // 电源状态
    uint8_t mode;           // 模式
    uint8_t temp;           // 温度
    uint8_t fan_speed;      // 风速
    uint8_t swing;          // 摆风
    uint8_t sleep;          // 睡眠
    uint8_t timer;           // 定时
    uint8_t eco;            // 节能
    uint8_t turbo;          // 强劲
    uint8_t health;         // 健康
    uint8_t light;          // 显示灯
    uint8_t dry;            // 干燥
    uint8_t brand;          // 品牌
    uint8_t reserved;       // 保留
} ESOACIRDATA;

typedef struct {
    uint8_t AIPstudyKey;    // 学习按键
    TYPEDEFIRLEARNDATA ir_learn_data;  // 红外学习数据
    uint8_t reserved[16];    // 保留
} airconditioner;

// 全局变量声明
extern TYPEDEFIRPWMTIM IR_PWM_TIM;
extern TYPEDEFIRLEARNDATA ir_learn_data;
extern ESOACIRDATA ESIDdata;
extern airconditioner ESairkey;

// 函数声明

/**
 * @brief 初始化红外适配器
 * @return 初始化是否成功
 */
bool IR_Adapter_Init(void);

/**
 * @brief 开始红外学习
 * @return 学习是否开始成功
 */
bool IR_Adapter_StartLearning(void);

/**
 * @brief 停止红外学习
 * @return 停止是否成功
 */
bool IR_Adapter_StopLearning(void);

/**
 * @brief 获取学习状态
 * @return 学习状态 (0=空闲, 1=学习中, 2=完成)
 */
uint8_t IR_Adapter_GetLearningStatus(void);

/**
 * @brief 获取学习到的红外数据
 * @param learn_data 学习数据输出
 * @return 获取是否成功
 */
bool IR_Adapter_GetLearnedData(TYPEDEFIRLEARNDATA *learn_data);

/**
 * @brief 发射红外信号
 * @param transmit_data 发射数据
 * @return 发射是否成功
 */
bool IR_Adapter_TransmitIR(TYPEDEFIRTRANSMITDATA *transmit_data);

/**
 * @brief 发射空调按键
 * @param button_id 按键ID
 * @return 发射是否成功
 */
bool IR_Adapter_TransmitACButton(uint8_t button_id);

/**
 * @brief 处理定时器中断（原始代码中的timer0_isr_ram）
 */
void IR_Adapter_TimerISR(void);

/**
 * @brief 处理外部中断（原始代码中的exti_isr_ram）
 */
void IR_Adapter_ExtISR(void);

/**
 * @brief 红外数据解码（原始代码中的ir_data_to_int）
 * @param ESIR_learn 红外学习数据
 * @return 解码结果，失败返回-1
 */
int IR_Adapter_DecodeData(TYPEDEFIRLEARNDATA *ESIR_learn);

/**
 * @brief 红外数据编码（原始代码中的IR_decode）
 * @param ir_data 红外数据
 * @param data_size 数据大小
 * @param IR_Send_struct 发送数据结构
 */
void IR_Adapter_EncodeData(uint8_t *ir_data, uint8_t data_size, TYPEDEFIRPWMTIM *IR_Send_struct);

/**
 * @brief 开始红外发射（原始代码中的IR_start_send）
 * @param IR_Send_struct 发送数据结构
 */
void IR_Adapter_StartSend(TYPEDEFIRPWMTIM *IR_Send_struct);

/**
 * @brief 停止红外发射（原始代码中的IR_stop_send）
 */
void IR_Adapter_StopSend(void);

/**
 * @brief 处理红外学习数据（原始代码中的IR_ProcessLearnData）
 * @param learn_data 学习数据
 */
void IR_Adapter_ProcessLearnData(TYPEDEFIRLEARNDATA *learn_data);

/**
 * @brief 将原始红外数据转换为优化格式
 * @param raw_data 原始数据
 * @param raw_len 原始数据长度
 * @param optimized_data 优化格式数据
 * @return 转换是否成功
 */
bool IR_Adapter_ConvertToOptimized(uint32_t *raw_data, uint16_t raw_len, 
                                   TYPEDEFIRLEARNDATA *optimized_data);

/**
 * @brief 将优化格式数据转换为原始格式
 * @param optimized_data 优化格式数据
 * @param raw_data 原始数据
 * @param raw_len 原始数据长度
 * @return 转换是否成功
 */
bool IR_Adapter_ConvertFromOptimized(TYPEDEFIRLEARNDATA *optimized_data, 
                                     uint32_t *raw_data, uint16_t *raw_len);

/**
 * @brief 定期处理函数
 * @return 处理是否成功
 */
bool IR_Adapter_PeriodicProcess(void);

/**
 * @brief 将学习数据保存到SPI Flash
 * @param data 学习数据
 * @return 保存是否成功
 */
bool IR_Adapter_SaveToSPIFlash(TYPEDEFIRLEARNDATA *data);

/**
 * @brief 从SPI Flash加载学习数据
 * @param data 学习数据
 * @return 加载是否成功
 */
bool IR_Adapter_LoadFromSPIFlash(TYPEDEFIRLEARNDATA *data);

#endif // __IR_ADAPTER_H__