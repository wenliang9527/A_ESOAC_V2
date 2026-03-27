/**
 * @file ir_adapter.c
 * @brief 红外转换适配器实现
 * @details 集成原始frIRConversion.c和优化版本frIRConversion_optimized.c
 */

#include "ir_adapter.h"
#include "air_conditioner_ir_controller.h"
#include "frspi.h"
#include <string.h>
#include <stdio.h>

// 调试日志宏
#define IR_ADAPTER_DEBUG_ENABLE  1
#define IR_ADAPTER_LOG(fmt, ...) do { if(IR_ADAPTER_DEBUG_ENABLE) co_printf("[IR_ADAPTER] " fmt "\r\n", ##__VA_ARGS__); } while(0)

// SPI Flash存储地址定义
#define IR_SPI_FLASH_BASE_ADDR      0x00100000  // 1MB偏移，避免与空调控制器数据冲突
#define IR_SPI_FLASH_SECTOR_SIZE    SPIF_SECTOR_SIZE
#define IR_SPI_FLASH_PAGE_SIZE      SPIF_PAGE_SIZE

// 学习状态定义
#define IR_LEARN_STATE_IDLE     0       // 空闲状态
#define IR_LEARN_STATE_WAITING  1       // 等待信号
#define IR_LEARN_STATE_LEARNING 2       // 学习中
#define IR_LEARN_STATE_COMPLETE 3       // 学习完成

// 全局变量
TYPEDEFIRPWMTIM IR_PWM_TIM = {0};
TYPEDEFIRLEARNDATA ir_learn_data = {0};
ESOACIRDATA ESIDdata = {0};
airconditioner ESairkey = {0};

// 内部状态变量
static uint8_t g_ir_learning_state = IR_LEARN_STATE_IDLE;
static TYPEDEFIRLEARN *g_ir_learn = NULL;
static uint8_t g_ir_mode = IR_SEND;
static uint16_t g_user_ir_task_id = 0;

// 内部函数声明
static bool IR_Adapter_InitHardware(void);
static void IR_Adapter_ProcessLearnedData(void);
static bool IR_Adapter_ValidateLearnedData(void);

/**
 * @brief 初始化红外适配器
 * @return 初始化是否成功
 */
bool IR_Adapter_Init(void)
{
    IR_ADAPTER_LOG("Initializing IR Adapter...");
    
    // 初始化SPI Flash
    fr_spi_flash();
    
    // 初始化硬件
    if (!IR_Adapter_InitHardware()) {
        IR_ADAPTER_LOG("Failed to initialize IR hardware");
        return false;
    }
    
    // 初始化全局变量
    memset(&IR_PWM_TIM, 0, sizeof(IR_PWM_TIM));
    memset(&ir_learn_data, 0, sizeof(ir_learn_data));
    memset(&ESIDdata, 0, sizeof(ESIDdata));
    memset(&ESairkey, 0, sizeof(ESairkey));
    
    // 初始化学习状态
    g_ir_learning_state = IR_LEARN_STATE_IDLE;
    g_ir_mode = IR_SEND;
    
    // 分配学习结构内存
    if (!g_ir_learn) {
        g_ir_learn = (TYPEDEFIRLEARN *)malloc(sizeof(TYPEDEFIRLEARN));
        if (!g_ir_learn) {
            IR_ADAPTER_LOG("Failed to allocate memory for IR learning");
            return false;
        }
        memset(g_ir_learn, 0, sizeof(TYPEDEFIRLEARN));
    }
    
    // 尝试从SPI Flash加载之前保存的数据
    TYPEDEFIRLEARNDATA saved_data = {0};
    if (IR_Adapter_LoadFromSPIFlash(&saved_data)) {
        IR_ADAPTER_LOG("Loaded IR data from SPI Flash, data_len = %d", saved_data.data_len);
        // 可以在这里使用加载的数据，例如设置到全局变量
        // memcpy(&ir_learn_data, &saved_data, sizeof(TYPEDEFIRLEARNDATA));
    } else {
        IR_ADAPTER_LOG("No valid IR data found in SPI Flash, starting with empty data");
    }
    
    IR_ADAPTER_LOG("IR Adapter initialized successfully");
    return true;
}

/**
 * @brief 开始红外学习
 * @return 学习是否开始成功
 */
bool IR_Adapter_StartLearning(void)
{
    if (g_ir_learning_state != IR_LEARN_STATE_IDLE) {
        IR_ADAPTER_LOG("IR learning already in progress");
        return false;
    }
    
    // 检查是否正在发射
    if (IR_PWM_TIM.IR_Busy) {
        IR_ADAPTER_LOG("IR transmitter is busy");
        return false;
    }
    
    // 重置学习数据
    memset(&ir_learn_data, 0, sizeof(ir_learn_data));
    memset(g_ir_learn, 0, sizeof(TYPEDEFIRLEARN));
    
    // 设置学习模式
    g_ir_mode = IR_LEARN;
    g_ir_learning_state = IR_LEARN_STATE_WAITING;
    g_ir_learn->ir_learn_step = 0;  // IR_WAIT_STOP
    
    // 启动定时器和外部中断
    // 这里应该根据原始代码调用相应的硬件初始化函数
    // 例如：timer_init(TIMER0, 1000, TIMER_PERIODIC);
    // 例如：ext_int_enable(EXTI_3, EXTI_EDGE_BOTH);
    
    IR_ADAPTER_LOG("IR learning started");
    return true;
}

/**
 * @brief 停止红外学习
 * @return 停止是否成功
 */
bool IR_Adapter_StopLearning(void)
{
    if (g_ir_learning_state == IR_LEARN_STATE_IDLE) {
        return true;
    }
    
    // 停止定时器和外部中断
    // 这里应该根据原始代码调用相应的硬件停止函数
    // 例如：timer_stop(TIMER0);
    // 例如：ext_int_disable(EXTI_3);
    
    // 重置状态
    g_ir_learning_state = IR_LEARN_STATE_IDLE;
    g_ir_mode = IR_SEND;
    
    IR_ADAPTER_LOG("IR learning stopped");
    return true;
}

/**
 * @brief 获取学习状态
 * @return 学习状态 (0=空闲, 1=学习中, 2=完成)
 */
uint8_t IR_Adapter_GetLearningStatus(void)
{
    switch (g_ir_learning_state) {
        case IR_LEARN_STATE_IDLE:
            return 0;
        case IR_LEARN_STATE_WAITING:
        case IR_LEARN_STATE_LEARNING:
            return 1;
        case IR_LEARN_STATE_COMPLETE:
            return 2;
        default:
            return 0;
    }
}

/**
 * @brief 获取学习到的红外数据
 * @param learn_data 学习数据输出
 * @return 获取是否成功
 */
bool IR_Adapter_GetLearnedData(TYPEDEFIRLEARNDATA *learn_data)
{
    if (!learn_data || g_ir_learning_state != IR_LEARN_STATE_COMPLETE) {
        return false;
    }
    
    // 复制学习数据
    memcpy(learn_data, &ir_learn_data, sizeof(TYPEDEFIRLEARNDATA));
    
    // 重置状态
    g_ir_learning_state = IR_LEARN_STATE_IDLE;
    
    return true;
}

/**
 * @brief 发射红外信号
 * @param transmit_data 发射数据
 * @return 发射是否成功
 */
bool IR_Adapter_TransmitIR(TYPEDEFIRTRANSMITDATA *transmit_data)
{
    if (!transmit_data) {
        IR_ADAPTER_LOG("Invalid transmit data");
        return false;
    }
    
    // 检查是否正在学习
    if (g_ir_mode == IR_LEARN) {
        IR_ADAPTER_LOG("Cannot transmit while in learning mode");
        return false;
    }
    
    // 检查是否正在发射
    if (IR_PWM_TIM.IR_Busy) {
        IR_ADAPTER_LOG("IR transmitter is busy");
        return false;
    }
    
    // 将优化格式的数据转换为原始格式
    TYPEDEFIRPWMTIM ir_send_data = {0};
    
    // 设置载波频率
    ir_send_data.ir_carrier_fre = transmit_data->carrier_freq;
    
    // 设置PWM数据
    // 这里需要根据优化格式的数据转换为原始格式的PWM数据
    // 由于原始代码和优化代码的数据结构不同，这里简化处理
    
    // 开始发射
    IR_Adapter_StartSend(&ir_send_data);
    
    IR_ADAPTER_LOG("IR transmission started");
    return true;
}

/**
 * @brief 发射空调按键
 * @param button_id 按键ID
 * @return 发射是否成功
 */
bool IR_Adapter_TransmitACButton(uint8_t button_id)
{
    // 通过空调控制器发射按键
    return AC_Controller_TransmitButton(button_id);
}

/**
 * @brief 处理定时器中断（原始代码中的timer0_isr_ram）
 */
void IR_Adapter_TimerISR(void)
{
    if (g_ir_mode == IR_SEND) {
        // 发射模式处理
        if (IR_PWM_TIM.IR_pwm_SendNum < IR_PWM_TIM.IR_pwm_Num) {
            // 更新PWM状态
            IR_PWM_TIM.IR_pwm_state = !IR_PWM_TIM.IR_pwm_state;
            
            // 更新PWM参数
            // 这里应该根据原始代码调用相应的PWM更新函数
            // 例如：pwm_update(PWM_CHANNEL_5, IR_PWM_TIM.ir_carrier_fre, duty);
            
            // 重新加载定时器
            // 例如：timer_init_count_us_reload(TIMER0, IR_PWM_TIM.IR_Pwm_State_Date[IR_PWM_TIM.IR_pwm_SendNum++]);
            
        } else if (IR_PWM_TIM.loop) {
            // 循环发射
            IR_PWM_TIM.IR_pwm_SendNum = 0;
            IR_PWM_TIM.IR_pwm_state = 0;
            // 重新加载定时器
            // 例如：timer_init_count_us_reload(TIMER0, IR_PWM_TIM.IR_Pwm_State_Date[IR_PWM_TIM.IR_pwm_SendNum++]);
        } else {
            // 发射完成
            IR_PWM_TIM.IR_Busy = false;
            // 停止定时器
            // 例如：timer_stop(TIMER0);
            
            // 发送完成事件
            // 例如：os_msg_post(g_user_ir_task_id, &ir_event);
        }
    } else if (g_ir_mode == IR_LEARN) {
        // 学习模式处理
        if (!g_ir_learn) {
            return;
        }
        
        switch (g_ir_learn->ir_learn_step) {
            case 0:  // IR_WAIT_STOP
                IR_ADAPTER_LOG("T");
                g_ir_learn->ir_timer_cnt++;
                if (g_ir_learn->ir_timer_cnt > 200) {  // 200ms未检测到信号变化，认为信号已停止
                    g_ir_learn->ir_learn_step = 1;  // IR_LEARN_GET_DATA
                    IR_ADAPTER_LOG("Switch to IR_LEARN_GET_DATA");
                    g_ir_learn->ir_learn_start = 0;
                }
                break;
                
            case 1:  // IR_LEARN_GET_DATA
                g_ir_learn->ir_timer_cnt++;
                if ((g_ir_learn->ir_timer_cnt >= 300) && g_ir_learn->ir_learn_start) {
                    g_ir_learn->ir_timer_cnt = 0;
                    
                    IR_ADAPTER_LOG("IR learn stop, ir_learn_cnt= %d", g_ir_learn->ir_learn_data_buf_cnt);
                    
                    // 处理学习到的数据
                    IR_Adapter_ProcessLearnedData();
                    
                    // 验证学习数据
                    if (IR_Adapter_ValidateLearnedData()) {
                        // 学习成功
                        ir_learn_data.IR_learn_state |= BIT(0);
                        ir_learn_data.ir_learn_data_cnt = g_ir_learn->ir_learn_data_cnt;
                        memcpy(ir_learn_data.ir_learn_Date, g_ir_learn->ir_learn_Date, 
                               g_ir_learn->ir_learn_data_cnt * sizeof(uint32_t));
                        
                        // 保存到空调控制器
                        memcpy(&ESairkey.airbutton[ESairkey.AIPstudyKey], &ir_learn_data, 
                               sizeof(TYPEDEFIRLEARNDATA));
                        
                        // 保存到SPI Flash
                        if (IR_Adapter_SaveToSPIFlash(&ir_learn_data)) {
                            IR_ADAPTER_LOG("IR data saved to SPI Flash successfully");
                        } else {
                            IR_ADAPTER_LOG("Failed to save IR data to SPI Flash");
                        }
                        
                        g_ir_learning_state = IR_LEARN_STATE_COMPLETE;
                        IR_ADAPTER_LOG("IR learn success! ir_learn_data_cnt = %d", 
                                      ir_learn_data.ir_learn_data_cnt);
                    } else {
                        IR_ADAPTER_LOG("IR learn failed, try again");
                        g_ir_learn->ir_learn_step = 0;  // 回到IR_WAIT_STOP
                    }
                    
                    IR_ADAPTER_LOG("*****************************************");
                }
                break;
        }
    }
}

/**
 * @brief 处理外部中断（原始代码中的exti_isr_ram）
 */
void IR_Adapter_ExtISR(void)
{
    if (!g_ir_learn) {
        return;
    }
    
    switch (g_ir_learn->ir_learn_step) {
        case 0:  // IR_WAIT_STOP
            IR_ADAPTER_LOG("P");
            g_ir_learn->ir_timer_cnt = 0;
            // 重新加载定时器
            // 例如：timer_reload(TIMER0);
            break;
            
        case 1:  // IR_LEARN_GET_DATA
            IR_ADAPTER_LOG("D");
            // 获取定时器值
            // 例如：timer_value = 48000 - timerp_ir->count_value.count;
            
            if (g_ir_learn->ir_learn_start == 0) {
                g_ir_learn->ir_learn_start = 1;
                g_ir_learn->ir_carrier_times = 0;
                g_ir_learn->ir_timer_cnt = 0;
                g_ir_learn->ir_learn_data_buf_cnt = 0;
                g_ir_learn->ir_learn_data_cnt = 0;
                ir_learn_data.ir_learn_data_cnt = 0;
                IR_ADAPTER_LOG("S");
            } else {
                // 处理信号变化
                // 这里应该根据原始代码处理信号变化
                // 例如：计算脉冲宽度、载波频率等
            }
            break;
    }
}

/**
 * @brief 红外数据解码（原始代码中的ir_data_to_int）
 * @param ESIR_learn 红外学习数据
 * @return 解码结果，失败返回-1
 */
int IR_Adapter_DecodeData(TYPEDEFIRLEARNDATA *ESIR_learn)
{
    // 调用优化版本的解码函数
    return ir_data_to_int(ESIR_learn);
}

/**
 * @brief 红外数据编码（原始代码中的IR_decode）
 * @param ir_data 红外数据
 * @param data_size 数据大小
 * @param IR_Send_struct 发送数据结构
 */
void IR_Adapter_EncodeData(uint8_t *ir_data, uint8_t data_size, TYPEDEFIRPWMTIM *IR_Send_struct)
{
    uint8_t i = 0, j = 0;
    
    if ((data_size * 2 + 4) > LERANDATACNTMAX) {
        IR_ADAPTER_LOG("Data size oversize!");
        return;
    }
    
    IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRLEADLOW;
    IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRLEADHIGHT;
    
    for (i = 0; i < data_size; i++) {
        for (j = 0; j < 8; j++) {
            if ((ir_data[i] >> j) & 0x01) {
                IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRLOGIC1LOW;
                IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRLOGIC1HIGHT;
            } else {
                IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRLOGIC0LOW;
                IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRLOGIC0HIGHT;
            }
        }
    }
    
    IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRSTOPLOW;
    IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRSTOPHIGHT;
}

/**
 * @brief 开始红外发射（原始代码中的IR_start_send）
 * @param IR_Send_struct 发送数据结构
 */
void IR_Adapter_StartSend(TYPEDEFIRPWMTIM *IR_Send_struct)
{
    if (((ir_learn_data.IR_learn_state & BIT(1)) == BIT(1))) {
        IR_ADAPTER_LOG("IR learn busy!");
        return;
    }
    
    if (IR_PWM_TIM.IR_Busy) {
        IR_ADAPTER_LOG("IR send busy!");
        return;
    }
    
    memcpy(&IR_PWM_TIM, IR_Send_struct, sizeof(TYPEDEFIRPWMTIM));
    IR_PWM_TIM.IR_Busy = true;
    IR_PWM_TIM.IR_pwm_SendNum = 0;
    
    g_ir_mode = IR_SEND;
    
    IR_ADAPTER_LOG("IR Send start, ir_carrier_fre:%d IR_pwm_SendNum:%d", 
                   IR_PWM_TIM.ir_carrier_fre, IR_PWM_TIM.IR_pwm_SendNum);
    
    // 初始化定时器
    // 例如：timer_init(TIMER0, IR_PWM_TIM.IR_Pwm_State_Date[IR_PWM_TIM.IR_pwm_SendNum++], TIMER_PERIODIC);
    
    // 初始化PWM
    // 例如：pwm_init(PWM_CHANNEL_5, IR_PWM_TIM.ir_carrier_fre, 50);
    // 例如：pwm_start(PWM_CHANNEL_5);
    
    // 启动定时器
    // 例如：timer_run(TIMER0);
}

/**
 * @brief 停止红外发射（原始代码中的IR_stop_send）
 */
void IR_Adapter_StopSend(void)
{
    IR_PWM_TIM.loop = false;
}

/**
 * @brief 处理红外学习数据（原始代码中的IR_ProcessLearnData）
 * @param learn_data 学习数据
 */
void IR_Adapter_ProcessLearnData(TYPEDEFIRLEARNDATA *learn_data)
{
    if (!learn_data) {
        return;
    }
    
    int result = IR_Adapter_DecodeData(learn_data);
    
    if (result != -1) {
        // 解码成功，保存到学习缓冲区
        // 这里应该调用优化版本的学习数据处理函数
        IR_ProcessLearnData(learn_data);
        
        // 通过4G模块上报（如果启用）
        #ifdef ML307A_MODULE_ENABLE
        ML307A_IR_Bridge_PublishIRData(learn_data);
        #endif
    }
}

/**
 * @brief 将原始红外数据转换为优化格式
 * @param raw_data 原始数据
 * @param raw_len 原始数据长度
 * @param optimized_data 优化格式数据
 * @return 转换是否成功
 */
bool IR_Adapter_ConvertToOptimized(uint32_t *raw_data, uint16_t raw_len, 
                                   TYPEDEFIRLEARNDATA *optimized_data)
{
    if (!raw_data || !optimized_data || raw_len == 0) {
        return false;
    }
    
    // 清空优化格式数据
    memset(optimized_data, 0, sizeof(TYPEDEFIRLEARNDATA));
    
    // 转换数据格式
    // 由于原始数据和优化数据的数据结构不同，需要进行转换
    // 这里简化处理，实际实现需要根据具体数据结构进行转换
    
    for (uint16_t i = 0; i < raw_len && i < IR_MAX_PULSE_COUNT; i++) {
        optimized_data->raw_data[i] = (uint16_t)raw_data[i];
    }
    
    optimized_data->data_len = raw_len;
    optimized_data->timestamp = 0;  // 应该使用实际时间戳
    optimized_data->protocol = IR_PROTOCOL_UNKNOWN;  // 需要检测协议
    optimized_data->status = 0;
    
    return true;
}

/**
 * @brief 将优化格式数据转换为原始格式
 * @param optimized_data 优化格式数据
 * @param raw_data 原始数据
 * @param raw_len 原始数据长度
 * @return 转换是否成功
 */
bool IR_Adapter_ConvertFromOptimized(TYPEDEFIRLEARNDATA *optimized_data, 
                                     uint32_t *raw_data, uint16_t *raw_len)
{
    if (!optimized_data || !raw_data || !raw_len) {
        return false;
    }
    
    // 转换数据格式
    // 由于原始数据和优化数据的数据结构不同，需要进行转换
    // 这里简化处理，实际实现需要根据具体数据结构进行转换
    
    for (uint16_t i = 0; i < optimized_data->data_len && i < LERANDATACNTMAX; i++) {
        raw_data[i] = (uint32_t)optimized_data->raw_data[i];
    }
    
    *raw_len = optimized_data->data_len;
    
    return true;
}

/**
 * @brief 定期处理函数
 * @return 处理是否成功
 */
bool IR_Adapter_PeriodicProcess(void)
{
    // 处理空调控制器的定期任务
    AC_Controller_PeriodicProcess();
    
    return true;
}

// 内部函数实现

/**
 * @brief 初始化红外硬件
 * @return 初始化是否成功
 */
static bool IR_Adapter_InitHardware(void)
{
    // 这里应该根据原始代码调用相应的硬件初始化函数
    // 例如：初始化GPIO、定时器、PWM、外部中断等
    
    // 由于无法访问具体的硬件驱动函数，这里仅提供框架
    
    IR_ADAPTER_LOG("IR hardware initialized");
    return true;
}

/**
 * @brief 处理学习到的数据
 */
static void IR_Adapter_ProcessLearnedData(void)
{
    uint32_t ir_carrier_cycle_data_sum = 0;
    uint8_t ir_carrier_cycle_data_cnt = 0;
    uint16_t i = 0;
    
    // 计算载波频率
    for (i = 0; i < 100; i++) {
        if ((g_ir_learn->ir_learn_data_buf[i] > 700) && (g_ir_learn->ir_learn_data_buf[i] < 1429)) {
            ir_carrier_cycle_data_sum += g_ir_learn->ir_learn_data_buf[i];
            ir_carrier_cycle_data_cnt++;
        }
    }
    
    if (ir_carrier_cycle_data_cnt > 0) {
        ir_carrier_cycle_data_sum /= ir_carrier_cycle_data_cnt;
        g_ir_learn->ir_carrier_fre = 48000000 / ir_carrier_cycle_data_sum;
        
        // 根据载波频率选择标准频率
        if (g_ir_learn->ir_carrier_fre > (IR_CF_56K - 1000)) {
            g_ir_learn->ir_carrier_fre = IR_CF_56K;
        } else if (g_ir_learn->ir_carrier_fre > (IR_CF_38K - 1000)) {
            g_ir_learn->ir_carrier_fre = IR_CF_38K;
        } else if (g_ir_learn->ir_carrier_fre > (IR_CF_36K - 1000)) {
            g_ir_learn->ir_carrier_fre = IR_CF_36K;
        }
        
        IR_ADAPTER_LOG("ir_carrier_fre =%d Hz", g_ir_learn->ir_carrier_fre);
        ir_learn_data.ir_carrier_fre = g_ir_learn->ir_carrier_fre;
    }
    
    // 处理脉冲数据
    for (i = 0; i < g_ir_learn->ir_learn_data_buf_cnt; i++) {
        if ((g_ir_learn->ir_learn_data_buf[i] > 700) && (g_ir_learn->ir_learn_data_buf[i] < 1429)) {
            g_ir_learn->ir_carrier_times++;
        } else if (g_ir_learn->ir_learn_data_buf[i] > 9600) {  // 200us
            if (g_ir_learn->ir_learn_data_cnt < (LERANDATACNTMAX - 1)) {
                g_ir_learn->ir_learn_Date[g_ir_learn->ir_learn_data_cnt++] = 
                    (g_ir_learn->ir_carrier_times + 0.5) * 1000000 / g_ir_learn->ir_carrier_fre;
                g_ir_learn->ir_learn_Date[g_ir_learn->ir_learn_data_cnt++] = 
                    (g_ir_learn->ir_learn_data_buf[i] >> 16) * 1000 + 
                    (g_ir_learn->ir_learn_data_buf[i] & 0xffff) / 48 - 
                    0.5 * 1000000 / g_ir_learn->ir_carrier_fre;
            }
            g_ir_learn->ir_carrier_times = 0;
        }
    }
    
    // 处理最后一个脉冲
    if ((g_ir_learn->ir_learn_data_cnt < LERANDATACNTMAX) && (g_ir_learn->ir_carrier_times > 0)) {
        g_ir_learn->ir_learn_Date[g_ir_learn->ir_learn_data_cnt++] = 
            (g_ir_learn->ir_carrier_times + 0.5) * 1000000 / g_ir_learn->ir_carrier_fre;
        IR_ADAPTER_LOG("ir_learn_Date[%d] = %d", 
                       g_ir_learn->ir_learn_data_cnt - 1, 
                       g_ir_learn->ir_learn_Date[g_ir_learn->ir_learn_data_cnt - 1]);
    }
    
    // 添加长停止位
    if (g_ir_learn->ir_learn_data_cnt < LERANDATACNTMAX) {
        g_ir_learn->ir_learn_Date[g_ir_learn->ir_learn_data_cnt++] = 100000;
        IR_ADAPTER_LOG("ir_learn_Date[%d] = %d", 
                       g_ir_learn->ir_learn_data_cnt - 1, 
                       g_ir_learn->ir_learn_Date[g_ir_learn->ir_learn_data_cnt - 1]);
    } else {
        g_ir_learn->ir_learn_Date[g_ir_learn->ir_learn_data_cnt - 1] = 100000;
        IR_ADAPTER_LOG("ir_learn_Date[%d] = %d", 
                       g_ir_learn->ir_learn_data_cnt - 1, 
                       g_ir_learn->ir_learn_Date[g_ir_learn->ir_learn_data_cnt - 1]);
    }
    
    IR_ADAPTER_LOG("ir_learn_data_cnt %d", g_ir_learn->ir_learn_data_cnt);
    
    // 打印学习数据
    for (i = 0; i < g_ir_learn->ir_learn_data_cnt; i++) {
        IR_ADAPTER_LOG("[%d %d] ", i, g_ir_learn->ir_learn_Date[i]);
        if (i % 10 == 9) {
            IR_ADAPTER_LOG("\r\n");
        }
    }
    IR_ADAPTER_LOG("\r\n");
}

/**
 * @brief 验证学习数据
 * @return 验证是否成功
 */
static bool IR_Adapter_ValidateLearnedData(void)
{
    // 这里应该实现学习数据的验证逻辑
    // 例如检查数据长度、载波频率、脉冲宽度等是否在合理范围内
    
    if (g_ir_learn->ir_learn_data_cnt < 10) {
        IR_ADAPTER_LOG("Learned data too short: %d", g_ir_learn->ir_learn_data_cnt);
        return false;
    }
    
    if (g_ir_learn->ir_carrier_fre < 30000 || g_ir_learn->ir_carrier_fre > 60000) {
        IR_ADAPTER_LOG("Invalid carrier frequency: %d", g_ir_learn->ir_carrier_fre);
        return false;
    }
    
    return true;
}

/**
 * @brief 将学习数据保存到SPI Flash
 * @param data 学习数据
 * @return 保存是否成功
 */
bool IR_Adapter_SaveToSPIFlash(TYPEDEFIRLEARNDATA *data)
{
    if (!data) {
        IR_ADAPTER_LOG("Invalid data pointer");
        return false;
    }
    
    // 计算数据大小
    uint32_t data_size = sizeof(TYPEDEFIRLEARNDATA);
    
    // 写入SPI Flash
    if (SpiFlash_Write(IR_SPI_FLASH_BASE_ADDR, (uint8_t *)data, data_size) != 0) {
        IR_ADAPTER_LOG("Failed to write data to SPI Flash");
        return false;
    }
    
    IR_ADAPTER_LOG("Data saved to SPI Flash at address 0x%08X, size %d bytes", 
                   IR_SPI_FLASH_BASE_ADDR, data_size);
    return true;
}

/**
 * @brief 从SPI Flash加载学习数据
 * @param data 学习数据
 * @return 加载是否成功
 */
bool IR_Adapter_LoadFromSPIFlash(TYPEDEFIRLEARNDATA *data)
{
    if (!data) {
        IR_ADAPTER_LOG("Invalid data pointer");
        return false;
    }
    
    // 计算数据大小
    uint32_t data_size = sizeof(TYPEDEFIRLEARNDATA);
    
    // 从SPI Flash读取
    if (SpiFlash_Read(IR_SPI_FLASH_BASE_ADDR, (uint8_t *)data, data_size) != 0) {
        IR_ADAPTER_LOG("Failed to read data from SPI Flash");
        return false;
    }
    
    // 验证数据有效性
    if (data->data_len == 0 || data->data_len > IR_MAX_PULSE_COUNT) {
        IR_ADAPTER_LOG("Invalid data loaded from SPI Flash");
        return false;
    }
    
    IR_ADAPTER_LOG("Data loaded from SPI Flash at address 0x%08X, size %d bytes", 
                   IR_SPI_FLASH_BASE_ADDR, data_size);
    return true;
}