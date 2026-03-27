/**
 * @file ir_example_app.c
 * @brief 红外转换优化版本示例应用
 * @details 演示如何使用优化后的红外转换功能
 */

#include "frIRConversion_optimized.h"
#include "ML307A_IR_Bridge.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 全局变量
static TYPEDEFIRLEARNDATA g_learn_data = {0};
static TYPEDEFIRTRANSMITDATA g_transmit_data = {0};
static volatile bool g_learning_active = false;
static volatile bool g_transmit_active = false;

/**
 * @brief 红外学习完成回调函数
 */
void IR_LearnCompleteCallback(TYPEDEFIRLEARNDATA *learned_data)
{
    if (!learned_data) {
        return;
    }
    
    printf("[IR] Learning completed! Data length: %d\n", learned_data->data_len);
    
    // 处理学习到的数据
    IR_ProcessLearnData(learned_data);
    
    // 通过4G模块发送到云端
    #ifdef ML307A_MODULE_ENABLE
    if (ML307A_IR_Bridge_IsConnected()) {
        if (ML307A_IR_Bridge_PublishIRData(learned_data)) {
            printf("[IR] Data published to cloud successfully\n");
        } else {
            printf("[IR] Failed to publish data to cloud\n");
        }
    }
    #endif
    
    g_learning_active = false;
}

/**
 * @brief 红外发射完成回调函数
 */
void IR_TransmitCompleteCallback(TYPEDEFIRTRANSMITDATA *transmitted_data)
{
    if (!transmitted_data) {
        return;
    }
    
    printf("[IR] Transmission completed! Pulse count: %d\n", transmitted_data->pulse_count);
    g_transmit_active = false;
}

/**
 * @brief 红外信号处理任务
 */
void IR_SignalProcessingTask(void)
{
    static uint32_t last_process_time = 0;
    uint32_t current_time = GetSystemTick();
    
    // 每100ms处理一次
    if (current_time - last_process_time >= 100) {
        last_process_time = current_time;
        
        // 处理4G桥接任务
        #ifdef ML307A_MODULE_ENABLE
        ML307A_IR_Bridge_Process();
        #endif
        
        // 检查是否有新的红外数据需要处理
        if (g_learning_active) {
            // 这里可以添加红外信号检测逻辑
            // 当检测到完整的红外信号时，调用 IR_LearnCompleteCallback
        }
        
        // 检查是否需要发射红外信号
        if (g_transmit_active) {
            // 这里可以添加红外发射逻辑
        }
    }
}

/**
 * @brief 示例：学习空调红外信号
 */
void Example_IRLearnAirConditioner(void)
{
    printf("\n=== IR Learning Example: Air Conditioner ===\n");
    
    // 模拟红外学习数据（实际应从硬件获取）
    uint16_t mock_data[] = {
        9000, 4500,  // NEC起始脉冲
        560, 560,    // 逻辑0
        560, 1690,   // 逻辑1
        560, 560,    // 逻辑0
        560, 1690,   // 逻辑1
        // ... 更多数据
    };
    
    // 填充学习数据结构
    g_learn_data.data_len = sizeof(mock_data) / sizeof(mock_data[0]);
    g_learn_data.timestamp = GetSystemTick();
    g_learn_data.protocol = IR_PROTOCOL_NEC;
    g_learn_data.status = 1;
    
    // 复制数据
    for (uint16_t i = 0; i < g_learn_data.data_len && i < IR_MAX_PULSE_COUNT; i++) {
        g_learn_data.raw_data[i] = mock_data[i];
    }
    
    // 处理学习数据
    IR_LearnCompleteCallback(&g_learn_data);
    
    printf("[IR] Air conditioner signal learned successfully\n");
}

/**
 * @brief 示例：发射红外信号控制空调
 */
void Example_IRTransmitAirConditioner(void)
{
    printf("\n=== IR Transmission Example: Air Conditioner ===\n");
    
    // 设置发射参数
    g_transmit_data.carrier_freq = 38;      // 38kHz载波频率
    g_transmit_data.duty_cycle = 33;        // 33%占空比
    g_transmit_data.repeat_count = 1;       // 重复1次
    
    // 编码空调控制信号（地址0x10，命令0x20）
    if (ir_encode_signal(0x10, 0x20, IR_PROTOCOL_NEC, &g_transmit_data)) {
        printf("[IR] Air conditioner signal encoded successfully\n");
        printf("[IR] Pulse count: %d\n", g_transmit_data.pulse_count);
        
        // 开始发射
        g_transmit_active = true;
        
        // 模拟发射完成
        IR_TransmitCompleteCallback(&g_transmit_data);
    } else {
        printf("[IR] Failed to encode air conditioner signal\n");
    }
}

/**
 * @brief 示例：红外协议识别
 */
void Example_IRProtocolDetection(void)
{
    printf("\n=== IR Protocol Detection Example ===\n");
    
    // NEC协议示例数据
    uint16_t nec_data[] = {9000, 4500, 560, 560, 560, 1690};
    uint8_t detected_protocol = ir_detect_protocol(nec_data, sizeof(nec_data)/sizeof(nec_data[0]));
    
    printf("[IR] Detected protocol: %s (0x%02X)\n", 
           (detected_protocol == IR_PROTOCOL_NEC) ? "NEC" : "Unknown", 
           detected_protocol);
    
    // Sony协议示例数据
    uint16_t sony_data[] = {2400, 600, 600, 600, 1200, 600};
    detected_protocol = ir_detect_protocol(sony_data, sizeof(sony_data)/sizeof(sony_data[0]));
    
    printf("[IR] Detected protocol: %s (0x%02X)\n", 
           (detected_protocol == IR_PROTOCOL_SONY) ? "Sony" : "Unknown", 
           detected_protocol);
}

/**
 * @brief 示例：红外数据压缩
 */
void Example_IRDataCompression(void)
{
    printf("\n=== IR Data Compression Example ===\n");
    
    // 原始红外数据
    uint16_t raw_data[100];
    for (int i = 0; i < 100; i++) {
        raw_data[i] = (i % 2 == 0) ? 560 : 1690;  // 交替模式
    }
    
    uint8_t compressed_data[IR_MAX_DATA_SIZE];
    uint16_t compressed_len = 0;
    
    // 压缩数据
    if (ir_compress_data(raw_data, 100, compressed_data, &compressed_len)) {
        printf("[IR] Data compressed successfully\n");
        printf("[IR] Original size: %d bytes\n", 100 * sizeof(uint16_t));
        printf("[IR] Compressed size: %d bytes\n", compressed_len);
        printf("[IR] Compression ratio: %.1f%%\n", 
               (1.0 - (float)compressed_len / (100 * sizeof(uint16_t))) * 100);
        
        // 解压缩验证
        uint16_t decompressed_data[100];
        uint16_t decompressed_len = 0;
        
        if (ir_decompress_data(compressed_data, compressed_len, decompressed_data, &decompressed_len)) {
            printf("[IR] Data decompressed successfully\n");
            printf("[IR] Decompressed length: %d\n", decompressed_len);
        }
    } else {
        printf("[IR] Data compression failed\n");
    }
}

/**
 * @brief 示例：4G模块集成
 */
void Example_4GIntegration(void)
{
    printf("\n=== 4G Module Integration Example ===\n");
    
    #ifdef ML307A_MODULE_ENABLE
    // 初始化4G模块
    if (ML307A_IR_Bridge_Init()) {
        printf("[4G] IR bridge initialized successfully\n");
        
        // 检查连接状态
        if (ML307A_IR_Bridge_IsConnected()) {
            printf("[4G] IR bridge connected to cloud\n");
            
            // 获取统计信息
            IR_BridgeStats_t stats;
            if (ML307A_IR_Bridge_GetStats(&stats)) {
                printf("[4G] Total published: %lu\n", stats.total_published);
                printf("[4G] Total received: %lu\n", stats.total_received);
                printf("[4G] Failed publishes: %lu\n", stats.failed_publishes);
            }
            
            // 发送测试数据
            TYPEDEFIRLEARNDATA test_data = {0};
            test_data.data_len = 10;
            test_data.timestamp = GetSystemTick();
            test_data.protocol = IR_PROTOCOL_NEC;
            
            for (int i = 0; i < 10; i++) {
                test_data.raw_data[i] = (i + 1) * 100;
            }
            
            if (ML307A_IR_Bridge_PublishIRData(&test_data)) {
                printf("[4G] Test data published successfully\n");
            } else {
                printf("[4G] Failed to publish test data\n");
            }
        } else {
            printf("[4G] IR bridge not connected\n");
        }
    } else {
        printf("[4G] Failed to initialize IR bridge\n");
    }
    #else
    printf("[4G] 4G module support not enabled\n");
    #endif
}

/**
 * @brief 性能测试
 */
void Example_PerformanceTest(void)
{
    printf("\n=== Performance Test ===\n");
    
    uint32_t start_time, end_time;
    int decode_result;
    
    // 测试红外解码性能
    TYPEDEFIRLEARNDATA test_data = {0};
    test_data.data_len = 68;  // NEC协议完整数据长度
    test_data.protocol = IR_PROTOCOL_NEC;
    test_data.timestamp = GetSystemTick();
    
    // 填充NEC协议测试数据
    test_data.raw_data[0] = 9000;   // 起始脉冲
    test_data.raw_data[1] = 4500;   // 起始间隔
    
    // 填充地址和命令数据
    for (int i = 2; i < 34; i += 2) {
        test_data.raw_data[i] = 560;    // 脉冲
        test_data.raw_data[i + 1] = (i < 18) ? 1690 : 560;  // 逻辑1或逻辑0
    }
    
    // 性能测试循环
    start_time = GetSystemTick();
    
    for (int i = 0; i < 1000; i++) {
        decode_result = ir_data_to_int(&test_data);
    }
    
    end_time = GetSystemTick();
    
    printf("[Performance] 1000次解码耗时: %lu ms\n", end_time - start_time);
    printf("[Performance] 平均每次解码: %.2f us\n", 
           (float)(end_time - start_time) * 1000.0 / 1000.0);
    printf("[Performance] 解码结果: %d\n", decode_result);
    
    // 内存使用统计
    printf("[Performance] 学习数据缓冲区: %d bytes\n", sizeof(TYPEDEFIRLEARNDATA));
    printf("[Performance] 发射数据缓冲区: %d bytes\n", sizeof(TYPEDEFIRTRANSMITDATA));
    printf("[Performance] 解码结果缓冲区: %d bytes\n", sizeof(TYPEDEFIRDECODERESULT));
}

/**
 * @brief 主函数 - 运行所有示例
 */
int main(void)
{
    printf("=== IR Conversion Optimized Version Demo ===\n\n");
    
    // 系统初始化
    SystemInit();
    
    // 初始化红外模块
    printf("[System] Initializing IR module...\n");
    IR_ClearLearnedData();
    
    // 运行示例
    Example_IRLearnAirConditioner();
    Example_IRTransmitAirConditioner();
    Example_IRProtocolDetection();
    Example_IRDataCompression();
    Example_4GIntegration();
    Example_PerformanceTest();
    
    printf("\n=== All Examples Completed ===\n");
    
    // 主循环
    while (1) {
        IR_SignalProcessingTask();
        
        // 其他系统任务...
        
        DelayMs(10);
    }
    
    return 0;
}

/**
 * @brief 系统初始化函数（模拟）
 */
void SystemInit(void)
{
    printf("[System] System initialization completed\n");
}

/**
 * @brief 获取系统时间戳（模拟）
 */
uint32_t GetSystemTick(void)
{
    static uint32_t tick = 0;
    return tick++;
}

/**
 * @brief 延时函数（模拟）
 */
void DelayMs(uint32_t ms)
{
    // 模拟延时
    for (volatile uint32_t i = 0; i < ms * 1000; i++);
}