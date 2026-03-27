/**
 * @file ir_spi_flash_example.c
 * @brief 红外适配器SPI Flash保存示例
 * @details 展示如何使用红外适配器将学习数据保存到SPI Flash
 */

#include "ir_adapter.h"
#include "air_conditioner_ir_controller.h"
#include <stdio.h>

/**
 * @brief 示例：学习并保存红外数据到SPI Flash
 * @return 操作是否成功
 */
bool IR_SPI_Flash_LearnAndSaveExample(void)
{
    // 初始化红外适配器
    if (!IR_Adapter_Init()) {
        printf("Failed to initialize IR adapter\r\n");
        return false;
    }
    
    // 初始化空调控制器
    if (!AC_Controller_Init()) {
        printf("Failed to initialize AC controller\r\n");
        return false;
    }
    
    // 设置要学习的按键ID（例如：电源按键）
    uint8_t button_id = 0;  // 假设0是电源按键
    
    // 开始学习
    printf("Starting IR learning for button %d...\r\n", button_id);
    if (!IR_Adapter_StartLearning()) {
        printf("Failed to start IR learning\r\n");
        return false;
    }
    
    // 等待学习完成（实际应用中应该使用事件或回调）
    printf("Waiting for IR learning to complete...\r\n");
    uint32_t timeout = 30000;  // 30秒超时
    uint32_t start_time = 0;  // 实际应用中应该获取系统时间
    
    while (IR_Adapter_GetLearningStatus() == 1) {
        // 检查超时
        // if (get_system_time() - start_time > timeout) {
        //     printf("IR learning timeout\r\n");
        //     IR_Adapter_StopLearning();
        //     return false;
        // }
        
        // 延时
        // delay_ms(100);
        
        // 在实际应用中，这里应该让出CPU或进入低功耗模式
        break;  // 示例中直接退出
    }
    
    // 获取学习状态
    uint8_t status = IR_Adapter_GetLearningStatus();
    if (status == 2) {
        printf("IR learning completed successfully\r\n");
        
        // 获取学习到的数据
        TYPEDEFIRLEARNDATA learned_data = {0};
        if (IR_Adapter_GetLearnedData(&learned_data)) {
            printf("Got learned data, data_len = %d, carrier_freq = %d\r\n", 
                   learned_data.data_len, learned_data.ir_carrier_fre);
            
            // 手动保存到SPI Flash（可选，因为IR_Adapter_GetLearnedData已经自动保存）
            if (IR_Adapter_SaveToSPIFlash(&learned_data)) {
                printf("Data saved to SPI Flash manually\r\n");
            } else {
                printf("Failed to save data to SPI Flash manually\r\n");
            }
            
            return true;
        } else {
            printf("Failed to get learned data\r\n");
            return false;
        }
    } else {
        printf("IR learning failed or incomplete, status = %d\r\n", status);
        return false;
    }
}

/**
 * @brief 示例：从SPI Flash加载并发射红外数据
 * @return 操作是否成功
 */
bool IR_SPI_Flash_LoadAndTransmitExample(void)
{
    // 初始化红外适配器
    if (!IR_Adapter_Init()) {
        printf("Failed to initialize IR adapter\r\n");
        return false;
    }
    
    // 从SPI Flash加载之前保存的数据
    TYPEDEFIRLEARNDATA loaded_data = {0};
    if (!IR_Adapter_LoadFromSPIFlash(&loaded_data)) {
        printf("Failed to load data from SPI Flash\r\n");
        return false;
    }
    
    printf("Loaded data from SPI Flash, data_len = %d, carrier_freq = %d\r\n", 
           loaded_data.data_len, loaded_data.ir_carrier_fre);
    
    // 将加载的数据转换为发射格式
    TYPEDEFIRTRANSMITDATA transmit_data = {0};
    transmit_data.carrier_freq = loaded_data.ir_carrier_fre;
    transmit_data.data_len = loaded_data.data_len;
    
    // 复制数据
    for (uint16_t i = 0; i < loaded_data.data_len && i < IR_MAX_PULSE_COUNT; i++) {
        transmit_data.pulse_data[i] = loaded_data.raw_data[i];
    }
    
    // 发射红外信号
    printf("Transmitting IR signal...\r\n");
    if (IR_Adapter_TransmitIR(&transmit_data)) {
        printf("IR signal transmitted successfully\r\n");
        return true;
    } else {
        printf("Failed to transmit IR signal\r\n");
        return false;
    }
}

/**
 * @brief 示例：比较内部Flash和SPI Flash的性能
 * @return 操作是否成功
 */
bool IR_SPI_Flash_PerformanceComparison(void)
{
    printf("=== SPI Flash vs Internal Flash Performance Comparison ===\r\n");
    
    // 准备测试数据
    TYPEDEFIRLEARNDATA test_data = {0};
    test_data.data_len = 100;
    test_data.ir_carrier_fre = IR_CF_38K;
    
    for (uint16_t i = 0; i < test_data.data_len; i++) {
        test_data.raw_data[i] = i * 10;
    }
    
    // 测试SPI Flash写入性能
    uint32_t start_time = 0;  // 实际应用中应该获取系统时间
    if (IR_Adapter_SaveToSPIFlash(&test_data)) {
        uint32_t spi_write_time = 0;  // 实际应用中应该计算时间差
        printf("SPI Flash write time: %d ms\r\n", spi_write_time);
    } else {
        printf("Failed to write to SPI Flash\r\n");
        return false;
    }
    
    // 测试SPI Flash读取性能
    TYPEDEFIRLEARNDATA read_data = {0};
    start_time = 0;  // 实际应用中应该获取系统时间
    if (IR_Adapter_LoadFromSPIFlash(&read_data)) {
        uint32_t spi_read_time = 0;  // 实际应用中应该计算时间差
        printf("SPI Flash read time: %d ms\r\n", spi_read_time);
        
        // 验证数据
        if (memcmp(&test_data, &read_data, sizeof(TYPEDEFIRLEARNDATA)) == 0) {
            printf("Data verification: SUCCESS\r\n");
        } else {
            printf("Data verification: FAILED\r\n");
            return false;
        }
    } else {
        printf("Failed to read from SPI Flash\r\n");
        return false;
    }
    
    printf("=== Performance Comparison Complete ===\r\n");
    return true;
}

/**
 * @brief 主函数示例
 */
int main(void)
{
    printf("IR Adapter SPI Flash Example\r\n");
    
    // 示例1：学习并保存到SPI Flash
    if (IR_SPI_Flash_LearnAndSaveExample()) {
        printf("Learn and save example: SUCCESS\r\n");
    } else {
        printf("Learn and save example: FAILED\r\n");
    }
    
    // 示例2：从SPI Flash加载并发射
    if (IR_SPI_Flash_LoadAndTransmitExample()) {
        printf("Load and transmit example: SUCCESS\r\n");
    } else {
        printf("Load and transmit example: FAILED\r\n");
    }
    
    // 示例3：性能比较
    if (IR_SPI_Flash_PerformanceComparison()) {
        printf("Performance comparison: SUCCESS\r\n");
    } else {
        printf("Performance comparison: FAILED\r\n");
    }
    
    return 0;
}