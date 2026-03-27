/**
 * @file ir_control_example.c
 * @brief 红外空调控制示例应用
 * @details 展示如何使用红外适配器控制空调
 */

#include "ir_adapter.h"
#include "air_conditioner_ir_controller.h"
#include "ML307A_IR_Bridge.h"
#include <stdio.h>
#include <string.h>

// 示例应用状态
typedef enum {
    APP_STATE_INIT,
    APP_STATE_IDLE,
    APP_STATE_LEARNING,
    APP_STATE_LEARN_COMPLETE,
    APP_STATE_TRANSMITTING,
    APP_STATE_ERROR
} app_state_t;

// 示例应用结构
typedef struct {
    app_state_t state;
    uint8_t current_brand;
    uint8_t learning_button;
    uint8_t transmit_button;
    uint8_t learn_retry_count;
    uint32_t last_action_time;
} app_context_t;

// 全局应用上下文
static app_context_t g_app_ctx = {0};

// 内部函数声明
static void APP_Init(void);
static void APP_Process(void);
static void APP_HandleUserInput(void);
static void APP_DisplayStatus(void);
static void APP_HandleLearnComplete(void);
static void APP_HandleTransmitComplete(void);

// 示例应用主函数
int main(void)
{
    // 初始化应用
    APP_Init();
    
    // 主循环
    while (1) {
        APP_Process();
        APP_HandleUserInput();
        APP_DisplayStatus();
        
        // 延时
        // delay_ms(100);
    }
    
    return 0;
}

/**
 * @brief 初始化应用
 */
static void APP_Init(void)
{
    printf("=== 红外空调控制示例应用 ===\n");
    
    // 初始化红外适配器
    if (!IR_Adapter_Init()) {
        printf("红外适配器初始化失败\n");
        g_app_ctx.state = APP_STATE_ERROR;
        return;
    }
    
    // 初始化空调控制器
    if (!AC_Controller_Init()) {
        printf("空调控制器初始化失败\n");
        g_app_ctx.state = APP_STATE_ERROR;
        return;
    }
    
    // 初始化4G模块（如果启用）
    #ifdef ML307A_MODULE_ENABLE
    if (!ML307A_IR_Bridge_Init()) {
        printf("4G模块初始化失败\n");
        // 4G模块失败不影响基本功能
    }
    #endif
    
    // 设置初始品牌
    g_app_ctx.current_brand = AC_BRAND_GRE;
    AC_Controller_SwitchBrand(g_app_ctx.current_brand);
    
    // 初始化状态
    g_app_ctx.state = APP_STATE_IDLE;
    g_app_ctx.learning_button = AC_BUTTON_POWER;
    g_app_ctx.transmit_button = AC_BUTTON_POWER;
    g_app_ctx.learn_retry_count = 0;
    g_app_ctx.last_action_time = 0;
    
    printf("应用初始化完成，当前品牌: %s\n", 
           AC_Controller_GetBrandName(g_app_ctx.current_brand));
}

/**
 * @brief 处理应用状态
 */
static void APP_Process(void)
{
    // 定期处理
    IR_Adapter_PeriodicProcess();
    
    // 根据状态处理
    switch (g_app_ctx.state) {
        case APP_STATE_INIT:
            // 初始化状态，通常不会停留在这里
            break;
            
        case APP_STATE_IDLE:
            // 空闲状态，等待用户输入
            break;
            
        case APP_STATE_LEARNING:
            // 学习状态，检查学习是否完成
            if (IR_Adapter_GetLearningStatus() == 2) {  // 学习完成
                APP_HandleLearnComplete();
            }
            break;
            
        case APP_STATE_LEARN_COMPLETE:
            // 学习完成状态，等待用户下一步操作
            break;
            
        case APP_STATE_TRANSMITTING:
            // 发射状态，检查发射是否完成
            if (!IR_Adapter_IsTransmitting()) {
                APP_HandleTransmitComplete();
            }
            break;
            
        case APP_STATE_ERROR:
            // 错误状态
            printf("应用处于错误状态，请重启\n");
            break;
    }
}

/**
 * @brief 处理用户输入
 */
static void APP_HandleUserInput(void)
{
    // 这里应该根据实际的用户输入方式处理
    // 例如：按键输入、串口命令、网络命令等
    
    // 示例：模拟用户输入
    // 实际应用中应该替换为真实的输入处理
    
    static uint32_t input_counter = 0;
    input_counter++;
    
    // 每10秒执行一次操作（仅用于演示）
    if (input_counter % 100 == 0) {
        switch (g_app_ctx.state) {
            case APP_STATE_IDLE:
                printf("模拟用户输入：开始学习电源按键\n");
                g_app_ctx.learning_button = AC_BUTTON_POWER;
                g_app_ctx.state = APP_STATE_LEARNING;
                IR_Adapter_StartLearning();
                break;
                
            case APP_STATE_LEARN_COMPLETE:
                printf("模拟用户输入：发射电源按键\n");
                g_app_ctx.transmit_button = AC_BUTTON_POWER;
                g_app_ctx.state = APP_STATE_TRANSMITTING;
                IR_Adapter_TransmitACButton(g_app_ctx.transmit_button);
                break;
                
            default:
                break;
        }
    }
}

/**
 * @brief 显示状态信息
 */
static void APP_DisplayStatus(void)
{
    static uint32_t display_counter = 0;
    display_counter++;
    
    // 每5秒显示一次状态
    if (display_counter % 50 == 0) {
        printf("当前状态: ");
        switch (g_app_ctx.state) {
            case APP_STATE_INIT:
                printf("初始化\n");
                break;
            case APP_STATE_IDLE:
                printf("空闲\n");
                break;
            case APP_STATE_LEARNING:
                printf("学习中...\n");
                break;
            case APP_STATE_LEARN_COMPLETE:
                printf("学习完成\n");
                break;
            case APP_STATE_TRANSMITTING:
                printf("发射中...\n");
                break;
            case APP_STATE_ERROR:
                printf("错误\n");
                break;
        }
        
        // 显示学习状态
        uint8_t learn_status = IR_Adapter_GetLearningStatus();
        if (learn_status == 1) {
            printf("红外学习状态: 学习中\n");
        } else if (learn_status == 2) {
            printf("红外学习状态: 学习完成\n");
        }
        
        // 显示发射状态
        if (IR_Adapter_IsTransmitting()) {
            printf("红外发射状态: 发射中\n");
        }
        
        // 显示空调控制器状态
        printf("空调品牌: %s\n", 
               AC_Controller_GetBrandName(g_app_ctx.current_brand));
        printf("已学习按键数: %d\n", 
               AC_Controller_GetLearnedButtonCount());
    }
}

/**
 * @brief 处理学习完成
 */
static void APP_HandleLearnComplete(void)
{
    TYPEDEFIRLEARNDATA learn_data = {0};
    
    // 获取学习数据
    if (IR_Adapter_GetLearnedData(&learn_data)) {
        printf("学习成功! 数据长度: %d, 载波频率: %d\n", 
               learn_data.ir_learn_data_cnt, learn_data.ir_carrier_fre);
        
        // 保存到空调控制器
        if (AC_Controller_SaveLearnedButton(g_app_ctx.learning_button, &learn_data)) {
            printf("按键 %s 已保存\n", 
                   AC_Controller_GetButtonName(g_app_ctx.learning_button));
        } else {
            printf("保存按键失败\n");
        }
        
        // 更新状态
        g_app_ctx.state = APP_STATE_LEARN_COMPLETE;
        g_app_ctx.learn_retry_count = 0;
    } else {
        printf("获取学习数据失败\n");
        
        // 重试
        g_app_ctx.learn_retry_count++;
        if (g_app_ctx.learn_retry_count < 3) {
            printf("重试学习...\n");
            IR_Adapter_StartLearning();
        } else {
            printf("学习失败，已达到最大重试次数\n");
            g_app_ctx.state = APP_STATE_ERROR;
            g_app_ctx.learn_retry_count = 0;
        }
    }
}

/**
 * @brief 处理发射完成
 */
static void APP_HandleTransmitComplete(void)
{
    printf("发射完成\n");
    g_app_ctx.state = APP_STATE_IDLE;
}

/**
 * @brief 切换空调品牌
 * @param brand_id 品牌ID
 */
void APP_SwitchBrand(uint8_t brand_id)
{
    if (brand_id >= AC_BRAND_COUNT) {
        printf("无效的品牌ID: %d\n", brand_id);
        return;
    }
    
    if (AC_Controller_SwitchBrand(brand_id)) {
        g_app_ctx.current_brand = brand_id;
        printf("已切换到品牌: %s\n", AC_Controller_GetBrandName(brand_id));
    } else {
        printf("切换品牌失败\n");
    }
}

/**
 * @brief 学习按键
 * @param button_id 按键ID
 */
void APP_LearnButton(uint8_t button_id)
{
    if (button_id >= AC_BUTTON_COUNT) {
        printf("无效的按键ID: %d\n", button_id);
        return;
    }
    
    if (g_app_ctx.state != APP_STATE_IDLE) {
        printf("当前状态不允许学习\n");
        return;
    }
    
    g_app_ctx.learning_button = button_id;
    g_app_ctx.state = APP_STATE_LEARNING;
    
    printf("开始学习按键: %s\n", AC_Controller_GetButtonName(button_id));
    
    if (!IR_Adapter_StartLearning()) {
        printf("启动学习失败\n");
        g_app_ctx.state = APP_STATE_IDLE;
    }
}

/**
 * @brief 发射按键
 * @param button_id 按键ID
 */
void APP_TransmitButton(uint8_t button_id)
{
    if (button_id >= AC_BUTTON_COUNT) {
        printf("无效的按键ID: %d\n", button_id);
        return;
    }
    
    if (g_app_ctx.state != APP_STATE_IDLE && g_app_ctx.state != APP_STATE_LEARN_COMPLETE) {
        printf("当前状态不允许发射\n");
        return;
    }
    
    g_app_ctx.transmit_button = button_id;
    g_app_ctx.state = APP_STATE_TRANSMITTING;
    
    printf("发射按键: %s\n", AC_Controller_GetButtonName(button_id));
    
    if (!IR_Adapter_TransmitACButton(button_id)) {
        printf("启动发射失败\n");
        g_app_ctx.state = APP_STATE_IDLE;
    }
}

/**
 * @brief 显示所有已学习的按键
 */
void APP_ShowLearnedButtons(void)
{
    printf("已学习的按键:\n");
    
    for (uint8_t i = 0; i < AC_BUTTON_COUNT; i++) {
        if (AC_Controller_IsButtonLearned(i)) {
            printf("  %s: 已学习\n", AC_Controller_GetButtonName(i));
        } else {
            printf("  %s: 未学习\n", AC_Controller_GetButtonName(i));
        }
    }
}

/**
 * @brief 保存所有学习数据到Flash
 */
void APP_SaveToFlash(void)
{
    if (AC_Controller_SaveToFlash()) {
        printf("学习数据已保存到Flash\n");
    } else {
        printf("保存到Flash失败\n");
    }
}

/**
 * @brief 从Flash加载所有学习数据
 */
void APP_LoadFromFlash(void)
{
    if (AC_Controller_LoadFromFlash()) {
        printf("已从Flash加载学习数据\n");
    } else {
        printf("从Flash加载失败\n");
    }
}

/**
 * @brief 清除所有学习数据
 */
void APP_ClearAllLearnedData(void)
{
    AC_Controller_ClearAllLearnedData();
    printf("已清除所有学习数据\n");
}

/**
 * @brief 处理4G模块命令（如果启用）
 * @param command 命令字符串
 */
void APP_Handle4GCommand(const char *command)
{
    #ifdef ML307A_MODULE_ENABLE
    // 这里应该解析4G模块接收到的命令并执行相应操作
    // 例如：学习、发射、切换品牌等
    
    if (strstr(command, "learn")) {
        // 解析按键ID
        uint8_t button_id = 0;
        if (sscanf(command, "learn %hhu", &button_id) == 1) {
            APP_LearnButton(button_id);
        } else {
            printf("无效的学习命令格式\n");
        }
    } else if (strstr(command, "transmit")) {
        // 解析按键ID
        uint8_t button_id = 0;
        if (sscanf(command, "transmit %hhu", &button_id) == 1) {
            APP_TransmitButton(button_id);
        } else {
            printf("无效的发射命令格式\n");
        }
    } else if (strstr(command, "brand")) {
        // 解析品牌ID
        uint8_t brand_id = 0;
        if (sscanf(command, "brand %hhu", &brand_id) == 1) {
            APP_SwitchBrand(brand_id);
        } else {
            printf("无效的品牌命令格式\n");
        }
    } else if (strstr(command, "status")) {
        APP_ShowLearnedButtons();
    } else {
        printf("未知命令: %s\n", command);
    }
    #endif
}