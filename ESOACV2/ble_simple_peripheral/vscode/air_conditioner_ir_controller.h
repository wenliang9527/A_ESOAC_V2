/**
 * @file air_conditioner_ir_controller.h
 * @brief 空调红外控制系统头文件
 * @details 支持主流空调品牌的学习、存储和发射功能
 */

#ifndef __AIR_CONDITIONER_IR_CONTROLLER_H__
#define __AIR_CONDITIONER_IR_CONTROLLER_H__

#include "frIRConversion_optimized.h"
#include <stdint.h>
#include <stdbool.h>

// 空调品牌定义
#define AC_BRAND_GENERIC     0x00    // 通用/未知品牌
#define AC_BRAND_GREY        0x01    // 格力
#define AC_BRAND_MIDEA       0x02    // 美的
#define AC_BRAND_HAIER       0x03    // 海尔
#define AC_BRAND_GREE        0x04    // 海信
#define AC_BRAND_AUX         0x05    // 奥克斯
#define AC_BRAND_CHANGHONG   0x06    // 长虹
#define AC_BRAND_TCL         0x07    // TCL
#define AC_BRAND_HISENSE     0x08    // 海信
#define AC_BRAND_KELON       0x09    // 科龙
#define AC_BRAND_CHIGO       0x0A    // 志高
#define AC_BRAND_MAX         0x0B    // 品牌数量

// 空调模式定义
#define AC_MODE_AUTO         0x00    // 自动模式
#define AC_MODE_COOL         0x01    // 制冷模式
#define AC_MODE_HEAT         0x02    // 制热模式
#define AC_MODE_DRY          0x03    // 除湿模式
#define AC_MODE_FAN          0x04    // 送风模式

// 空调风速定义
#define AC_FAN_AUTO          0x00    // 自动风速
#define AC_FAN_LOW           0x01    // 低速
#define AC_FAN_MEDIUM        0x02    // 中速
#define AC_FAN_HIGH          0x03    // 高速

// 空调温度范围
#define AC_TEMP_MIN          16      // 最低温度 (°C)
#define AC_TEMP_MAX          32      // 最高温度 (°C)
#define AC_TEMP_DEFAULT      25      // 默认温度 (°C)

// 最大存储按键数量
#define AC_MAX_BUTTONS       50      // 每个品牌最多存储按键数量

// 空调按键定义
#define AC_BUTTON_POWER      0x00    // 电源键
#define AC_BUTTON_MODE       0x01    // 模式键
#define AC_BUTTON_TEMP_UP    0x02    // 温度+
#define AC_BUTTON_TEMP_DOWN  0x03    // 温度-
#define AC_BUTTON_FAN        0x04    // 风速键
#define AC_BUTTON_SWING      0x05    // 摆风键
#define AC_BUTTON_SLEEP      0x06    // 睡眠键
#define AC_BUTTON_TIMER      0x07    // 定时键
#define AC_BUTTON_TURBO      0x08    // 强劲键
#define AC_BUTTON_LIGHT      0x09    // 显示灯键
#define AC_BUTTON_ECO        0x0A    // 节能键
#define AC_BUTTON_HEALTH     0x0B    // 健康键
#define AC_BUTTON_DRY        0x0C    // 干燥键
#define AC_BUTTON_CUSTOM1    0x10    // 自定义键1
#define AC_BUTTON_CUSTOM2    0x11    // 自定义键2
#define AC_BUTTON_CUSTOM3    0x12    // 自定义键3
#define AC_BUTTON_CUSTOM4    0x13    // 自定义键4

// 空调状态结构
typedef struct {
    uint8_t power;          // 电源状态 (0=关, 1=开)
    uint8_t mode;           // 模式 (AC_MODE_*)
    uint8_t temp;           // 温度 (AC_TEMP_MIN ~ AC_TEMP_MAX)
    uint8_t fan_speed;      // 风速 (AC_FAN_*)
    uint8_t swing;          // 摆风状态 (0=关, 1=开)
    uint8_t sleep;          // 睡眠状态 (0=关, 1=开)
    uint8_t eco;            // 节能状态 (0=关, 1=开)
    uint8_t turbo;          // 强劲状态 (0=关, 1=开)
    uint8_t health;         // 健康状态 (0=关, 1=开)
    uint8_t light;          // 显示灯状态 (0=关, 1=开)
    uint8_t timer;          // 定时状态 (0=关, 1=开)
    uint8_t timer_hours;    // 定时小时数
    uint8_t brand;          // 品牌代码 (AC_BRAND_*)
} AC_Status_t;

// 空调按键数据结构
typedef struct {
    uint8_t button_id;                      // 按键ID (AC_BUTTON_*)
    char button_name[16];                   // 按键名称
    TYPEDEFIRLEARNDATA ir_data;             // 红外数据
    bool is_valid;                          // 数据是否有效
    uint32_t learn_timestamp;               // 学习时间戳
} AC_Button_t;

// 空调品牌数据结构
typedef struct {
    uint8_t brand_id;                        // 品牌ID (AC_BRAND_*)
    char brand_name[16];                     // 品牌名称
    AC_Button_t buttons[AC_MAX_BUTTONS];    // 按键数据
    uint8_t button_count;                   // 已学习按键数量
    bool is_active;                         // 是否为当前激活品牌
} AC_Brand_t;

// 空调控制器结构
typedef struct {
    AC_Brand_t brands[AC_BRAND_MAX];        // 品牌数据
    uint8_t active_brand;                   // 当前激活品牌
    AC_Status_t current_status;             // 当前空调状态
    bool is_initialized;                    // 是否已初始化
    uint32_t last_transmit_time;            // 上次发射时间
} AC_Controller_t;

// 函数声明

/**
 * @brief 初始化空调控制器
 * @return 初始化是否成功
 */
bool AC_Controller_Init(void);

/**
 * @brief 设置当前空调品牌
 * @param brand_id 品牌ID (AC_BRAND_*)
 * @return 设置是否成功
 */
bool AC_Controller_SetBrand(uint8_t brand_id);

/**
 * @brief 获取当前空调品牌
 * @return 当前品牌ID
 */
uint8_t AC_Controller_GetBrand(void);

/**
 * @brief 学习空调按键
 * @param button_id 按键ID (AC_BUTTON_*)
 * @param button_name 按键名称 (可选)
 * @return 学习是否成功
 */
bool AC_Controller_LearnButton(uint8_t button_id, const char *button_name);

/**
 * @brief 发射空调按键
 * @param button_id 按键ID (AC_BUTTON_*)
 * @return 发射是否成功
 */
bool AC_Controller_TransmitButton(uint8_t button_id);

/**
 * @brief 设置空调电源状态
 * @param power 电源状态 (0=关, 1=开)
 * @return 设置是否成功
 */
bool AC_Controller_SetPower(uint8_t power);

/**
 * @brief 设置空调模式
 * @param mode 模式 (AC_MODE_*)
 * @return 设置是否成功
 */
bool AC_Controller_SetMode(uint8_t mode);

/**
 * @brief 设置空调温度
 * @param temp 温度 (AC_TEMP_MIN ~ AC_TEMP_MAX)
 * @return 设置是否成功
 */
bool AC_Controller_SetTemperature(uint8_t temp);

/**
 * @brief 设置空调风速
 * @param fan_speed 风速 (AC_FAN_*)
 * @return 设置是否成功
 */
bool AC_Controller_SetFanSpeed(uint8_t fan_speed);

/**
 * @brief 设置空调摆风状态
 * @param swing 摆风状态 (0=关, 1=开)
 * @return 设置是否成功
 */
bool AC_Controller_SetSwing(uint8_t swing);

/**
 * @brief 设置空调睡眠模式
 * @param sleep 睡眠状态 (0=关, 1=开)
 * @return 设置是否成功
 */
bool AC_Controller_SetSleep(uint8_t sleep);

/**
 * @brief 设置空调节能模式
 * @param eco 节能状态 (0=关, 1=开)
 * @return 设置是否成功
 */
bool AC_Controller_SetEco(uint8_t eco);

/**
 * @brief 设置空调强劲模式
 * @param turbo 强劲状态 (0=关, 1=开)
 * @return 设置是否成功
 */
bool AC_Controller_SetTurbo(uint8_t turbo);

/**
 * @brief 获取当前空调状态
 * @return 当前空调状态结构指针
 */
AC_Status_t* AC_Controller_GetStatus(void);

/**
 * @brief 获取品牌名称
 * @param brand_id 品牌ID
 * @return 品牌名称字符串
 */
const char* AC_Controller_GetBrandName(uint8_t brand_id);

/**
 * @brief 获取按键名称
 * @param button_id 按键ID
 * @return 按键名称字符串
 */
const char* AC_Controller_GetButtonName(uint8_t button_id);

/**
 * @brief 保存空调数据到Flash
 * @return 保存是否成功
 */
bool AC_Controller_SaveToFlash(void);

/**
 * @brief 从Flash加载空调数据
 * @return 加载是否成功
 */
bool AC_Controller_LoadFromFlash(void);

/**
 * @brief 清除所有学习数据
 * @return 清除是否成功
 */
bool AC_Controller_ClearAllData(void);

/**
 * @brief 清除指定品牌的学习数据
 * @param brand_id 品牌ID
 * @return 清除是否成功
 */
bool AC_Controller_ClearBrandData(uint8_t brand_id);

/**
 * @brief 获取品牌已学习按键数量
 * @param brand_id 品牌ID
 * @return 按键数量
 */
uint8_t AC_Controller_GetButtonCount(uint8_t brand_id);

/**
 * @brief 获取指定品牌按键的学习状态
 * @param brand_id 品牌ID
 * @param button_id 按键ID
 * @return 是否已学习 (true=已学习, false=未学习)
 */
bool AC_Controller_IsButtonLearned(uint8_t brand_id, uint8_t button_id);

/**
 * @brief 复制按键数据
 * @param src_brand_id 源品牌ID
 * @param src_button_id 源按键ID
 * @param dst_brand_id 目标品牌ID
 * @param dst_button_id 目标按键ID
 * @return 复制是否成功
 */
bool AC_Controller_CopyButton(uint8_t src_brand_id, uint8_t src_button_id, 
                              uint8_t dst_brand_id, uint8_t dst_button_id);

/**
 * @brief 自动识别空调品牌
 * @param ir_data 红外数据
 * @return 识别的品牌ID，失败返回AC_BRAND_GENERIC
 */
uint8_t AC_Controller_IdentifyBrand(TYPEDEFIRLEARNDATA *ir_data);

/**
 * @brief 处理接收到的红外信号
 * @param ir_data 红外数据
 * @return 处理是否成功
 */
bool AC_Controller_ProcessReceivedIR(TYPEDEFIRLEARNDATA *ir_data);

/**
 * @brief 定时处理函数
 * @return 处理是否成功
 */
bool AC_Controller_PeriodicProcess(void);

#endif // __AIR_CONDITIONER_IR_CONTROLLER_H__