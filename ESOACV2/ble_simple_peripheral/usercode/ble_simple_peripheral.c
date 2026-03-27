/**
 * Copyright (c) 2019, Freqchip
 * 
 * All rights reserved.
 * 
 * 
 */
 
 /*
 * INCLUDES (包含头文件)
 */
#include <stdbool.h>
#include <string.h>
#include "os_timer.h"
#include "gap_api.h"
#include "gatt_api.h"
#include "driver_gpio.h"
#include "simple_gatt_service.h"
#include "ble_simple_peripheral.h"

#include "sys_utils.h"
#include "flash_usage_config.h"

#include "aircondata.h"
#include "device_config.h"
#include "co_printf.h"

/*
 * MACROS (宏定义)
 */

/*
 * CONSTANTS (常量定义)
 */

// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertisting)
// GAP-广播包的内容,最长31个字节.短一点的内容可以节省广播时的系统功耗.
static uint8_t adv_data[] =
{
  // service UUID, to notify central devices what services are included
  // in this peripheral. 告诉central本机有什么服务, 但这里先只放一个主要的.
  0x03,   // length of this data
  GAP_ADVTYPE_16BIT_MORE,      // some of the UUID's, but not all
  0xFF,
  0xFE,
};

// GAP - Scan response data (max size = 31 bytes, though this is
// best kept short to conserve power while advertisting)
// GAP-Scan response内容,最长31个字节.短一点的内容可以节省广播时的系统功耗.
/**
 * @brief 扫描响应数据数组
 * 
 * 包含设备名称和发射功率信息的扫描响应数据。
 * 在BLE扫描过程中，当中央设备请求更多设备信息时发送此数据。
 * 
 * 数据结构遵循BLE广告数据格式：
 * - 第一个字节表示数据长度
 * - 第二个字节表示数据类型
 * - 后续字节为实际数据内容
 */
static uint8_t scan_rsp_data[] =
{
    // 设备完整名称
    0x12,                           // 此段数据长度：18字节
    GAP_ADVTYPE_LOCAL_NAME_COMPLETE, // 数据类型：完整本地名称
    'E','S','O','A','C', // 设备名称："Simple_Peripheral"
    // 发射功率等级
    0x02,                           // 此段数据长度：2字节
    GAP_ADVTYPE_POWER_LEVEL,        // 数据类型：功率等级
    0                               // 功率值：0dBm
};

/*
 * TYPEDEFS (类型定义)
 */

/*
 * GLOBAL VARIABLES (全局变量)
 */

/*
 * LOCAL VARIABLES (本地变量)
 */
os_timer_t update_param_timer;


 
/*
 * LOCAL FUNCTIONS (本地函数)
 */
static void sp_start_adv(void);
/*
 * EXTERN FUNCTIONS (外部函数)
 */

/*
 * PUBLIC FUNCTIONS (全局函数)
 */

/** @function group ble peripheral device APIs (ble外设相关的API)
 * @{
 */

/**
 * @brief 定时器回调函数，用于更新连接参数
 * 
 * 该函数作为定时器的回调函数被调用，主要功能是调用gap_conn_param_update
 * 函数来更新设备间的连接参数，以优化蓝牙连接的性能和功耗。
 * 
 * @param arg 指向传递给定时器回调函数的参数指针（本函数中未使用）
 * 
 * @return 无返回值
 */
void param_timer_func(void *arg)
{
    co_printf("param_timer_func\r\n");
    // 更新连接参数，设置最小间隔12*1.25ms=15ms，最大间隔12*1.25ms=15ms，
    // 延迟55*10ms=550ms，超时600*10ms=6000ms
    gap_conn_param_update(0, 12, 12, 55, 600);
}

/**
 * @brief GAP事件回调函数，处理蓝牙协议栈上报的各种GAP层事件。
 *
 * @param p_event 指向GAP事件结构体的指针，包含事件类型及对应的参数信息。
 */
void app_gap_evt_cb(gap_event_t *p_event)
{
    // 根据事件类型进行分发处理
    switch(p_event->type)
    {
        // 广播结束事件：打印广播状态
        case GAP_EVT_ADV_END:
        {
            co_printf("adv_end,status:0x%02x\r\n",p_event->param.adv_end.status);
        }
        break;

        // 所有服务已添加完成事件：启动广播
        case GAP_EVT_ALL_SVC_ADDED:
        {
            co_printf("All service added\r\n");
            sp_start_adv(); // 启动广播
        }
        break;

        // 从设备连接成功事件：打印连接索引和当前连接数，并启动参数更新定时器
        case GAP_EVT_SLAVE_CONNECT:
        {
            co_printf("slave[%d],connect. link_num:%d\r\n",p_event->param.slave_connect.conidx,gap_get_connect_num());
            os_timer_start(&update_param_timer,4000,0); // 延时4秒后尝试更新连接参数
            //gap_security_req(p_event->param.slave_connect.conidx); // 可选安全请求（被注释）
        }
        break;

        // 连接断开事件：打印断开原因并重新开始广播
        case GAP_EVT_DISCONNECT:
        {
            co_printf("Link[%d] disconnect,reason:0x%02X\r\n",p_event->param.disconnect.conidx
                      ,p_event->param.disconnect.reason);
            os_timer_stop(&update_param_timer); // 停止参数更新定时器
            gap_start_advertising(0); // 重启广播
        }
        break;

        // 连接参数更新被拒绝事件：打印连接索引与拒绝状态码
        case GAP_EVT_LINK_PARAM_REJECT:
            co_printf("Link[%d]param reject,status:0x%02x\r\n"
                      ,p_event->param.link_reject.conidx,p_event->param.link_reject.status);
            break;

        // 连接参数更新完成事件：打印新的连接间隔、延迟和超时时间
        case GAP_EVT_LINK_PARAM_UPDATE:
            co_printf("Link[%d]param update,interval:%d,latency:%d,timeout:%d\r\n",p_event->param.link_update.conidx
                      ,p_event->param.link_update.con_interval,p_event->param.link_update.con_latency,p_event->param.link_update.sup_to);
            break;

        // 对端特性指示事件：打印连接索引
        case GAP_EVT_PEER_FEATURE:
            co_printf("peer[%d] feats ind\r\n",p_event->param.peer_feature.conidx);
            //show_reg((uint8_t *)&(p_event->param.peer_feature.features),8,1); // 显示对端特性（被注释）
            break;

        // MTU更新事件：打印连接索引和新MTU值
        case GAP_EVT_MTU:
            co_printf("mtu update,conidx=%d,mtu=%d\r\n"
                      ,p_event->param.mtu.conidx,p_event->param.mtu.value);
            break;
        
        // 链路RSSI事件：打印当前链路的信号强度
        case GAP_EVT_LINK_RSSI:
            co_printf("link rssi %d\r\n",p_event->param.link_rssi);
            break;
                
        // 从机加密完成事件：打印连接索引并重启参数更新定时器
        case GAP_SEC_EVT_SLAVE_ENCRYPT:
            co_printf("slave[%d]_encrypted\r\n",p_event->param.slave_encrypt_conidx);
            os_timer_start(&update_param_timer,4000,0); // 加密完成后再次启动参数更新定时器
            break;

        // 其他未定义事件不做处理
        default:
            break;
    }
}


/**
 * @brief 启动BLE广播功能
 * 
 * 该函数配置并启动BLE设备的广播功能，包括设置广播参数、
 * 广播数据和扫描响应数据，然后开始广播。
 * 
 * @param 无
 * @return 无
 */
static void sp_start_adv(void)
{
    // 配置广播参数
    gap_adv_param_t adv_param;
    adv_param.adv_mode = GAP_ADV_MODE_UNDIRECT;           // 设置为可连接不可定向广播模式
    adv_param.adv_addr_type = GAP_ADDR_TYPE_PUBLIC;       // 使用公共地址类型
    adv_param.adv_chnl_map = GAP_ADV_CHAN_ALL;            // 在所有广播通道上发送
    adv_param.adv_filt_policy = GAP_ADV_ALLOW_SCAN_ANY_CON_ANY; // 允许任何设备扫描和连接
    adv_param.adv_intv_min = 600;                         // 最小广播间隔(600 * 0.625ms = 375ms)
    adv_param.adv_intv_max = 600;                         // 最大广播间隔(600 * 0.625ms = 375ms)
        
    gap_set_advertising_param(&adv_param);
    
    // 设置广播数据和扫描响应数据
	gap_set_advertising_data(adv_data, sizeof(adv_data));
	gap_set_advertising_rsp_data(scan_rsp_data, sizeof(scan_rsp_data));
    
    // 启动广播
	co_printf("Start advertising...\r\n");
	gap_start_advertising(0);
}

/**
 * @brief 动态更新BLE设备名称并重启广播
 *
 * 流程: 停止广播 -> 设置新名称 -> 更新扫描响应数据 -> 重启广播
 *
 * @param new_name  新设备名称字符串
 * @param name_len  名称长度(不含null终止符)
 * @return 0:成功 -1:参数错误 -2:广播停止失败
 */
int ble_update_device_name(const char *new_name, uint8_t name_len)
{
    if (new_name == NULL || name_len == 0 || name_len > BLE_NAME_MAX_LEN) {
        co_printf("ble_update_device_name: invalid param\r\n");
        return -1;
    }

    // Step 1: 停止当前广播
    gap_stop_advertising();
    co_printf("Advertising stopped for name update\r\n");

    // Step 2: 通过GAP API设置新的设备名称
    // gap_set_dev_name内部会保存名称，后续连接中remote设备读取时生效
    uint8_t dev_name[BLE_NAME_MAX_LEN + 1];
    memcpy(dev_name, new_name, name_len);
    dev_name[name_len] = '\0';
    gap_set_dev_name(dev_name, name_len + 1);

    // Step 3: 更新扫描响应数据中的设备名称
    // scan_rsp_data格式: [len, GAP_ADVTYPE_LOCAL_NAME_COMPLETE, name_bytes..., 0x02, GAP_ADVTYPE_POWER_LEVEL, 0]
    // 名称部分: 1字节长度 + 1字节类型 + name_len字节名称
    scan_rsp_data[0] = name_len + 1;  // 长度 = 类型(1) + 名称字节
    scan_rsp_data[1] = GAP_ADVTYPE_LOCAL_NAME_COMPLETE;

    // 填充名称字节，剩余位置填0
    memset(&scan_rsp_data[2], 0, 20);  // 先清零名称区域
    memcpy(&scan_rsp_data[2], new_name, name_len);

    // 功率等级字段紧跟名称之后
    uint8_t power_offset = 2 + name_len;
    scan_rsp_data[power_offset] = 0x02;                     // 长度
    scan_rsp_data[power_offset + 1] = GAP_ADVTYPE_POWER_LEVEL; // 类型
    scan_rsp_data[power_offset + 2] = 0;                    // 功率值

    // 更新扫描响应数据长度
    uint8_t scan_rsp_len = power_offset + 3;

    // Step 4: 设置新的扫描响应数据
    gap_set_advertising_rsp_data(scan_rsp_data, scan_rsp_len);

    // Step 5: 重启广播
    gap_start_advertising(0);

    co_printf("BLE name updated to \"%s\", adv restarted, scan_rsp_len=%d\r\n",
              new_name, scan_rsp_len);

    return 0;
}




/**
 * @brief 初始化简单外设（Simple Peripheral）的相关配置。
 *
 * 此函数用于初始化BLE外设的基本设置，包括设备名称、安全参数、绑定管理以及GATT服务等。
 * 它还打印本地蓝牙地址以供调试使用。
 *
 * @note 该函数无参数，也无返回值。
 */
void simple_peripheral_init(void)
{
    // 设置本地设备名称为 "Simple Peripheral"
	
    uint8_t local_name[18] = "ESOAC-" ;
	  strcat((char*)local_name, (char*)ESAirdata.MUConlyID);
    gap_set_dev_name(local_name, sizeof(local_name));

    // 初始化更新连接参数的定时器
    os_timer_init(&update_param_timer, param_timer_func, NULL);

#if 0		// 安全加密相关配置示例：启用MITM保护与安全连接（未启用）
    gap_security_param_t param =
    {
        .mitm = true,		
        .ble_secure_conn = true,		// 当前未启用安全连接
        .io_cap = GAP_IO_CAP_NO_INPUT_NO_OUTPUT,		// 设备无输入输出能力
        .pair_init_mode = GAP_PAIRING_MODE_WAIT_FOR_REQ,	// 等待配对请求
        .bond_auth = true,	// 需要进行绑定认证
    };
#endif 

#if 0   // 安全配置示例：使用键盘输入PIN码的方式进行配对（未启用）
    gap_security_param_t param =
    {
        .mitm = true,		// 使用PIN码进行MITM保护
        .ble_secure_conn = false,		// 不启用安全连接
        .io_cap = GAP_IO_CAP_KEYBOARD_ONLY,		// 设备具有键盘输入能力
        .pair_init_mode = GAP_PAIRING_MODE_WAIT_FOR_REQ,	// 等待配对请求
        .bond_auth = true,	// 需要绑定认证
    };
#endif

#if 0   // 安全配置示例：显示PIN码给用户确认（未启用）
    gap_security_param_t param =
    {
        .mitm = true,		// 使用PIN码进行MITM保护
        .ble_secure_conn = false,		// 不启用安全连接
        .io_cap = GAP_IO_CAP_DISPLAY_ONLY,	// 设备仅能显示PIN码
        .pair_init_mode = GAP_PAIRING_MODE_WAIT_FOR_REQ, // 等待配对请求
        .bond_auth = true,	// 需要绑定认证
        .password = 123456,	// 设置固定PIN码，范围应在100000~999999之间
    };
#endif

#if 1   // 当前使用的安全配置：无需MITM保护，无IO能力
    gap_security_param_t param =
    {
        .mitm = false,	// 不使用PIN码进行MITM保护
        .ble_secure_conn = false,	// 不启用安全连接
        .io_cap = GAP_IO_CAP_NO_INPUT_NO_OUTPUT, // 设备既无输入也无输出能力
        .pair_init_mode = GAP_PAIRING_MODE_WAIT_FOR_REQ,		// 等待配对请求
        .bond_auth = true,	// 需要绑定认证
        .password = 0,
    };
#endif

    // 初始化安全参数
    gap_security_param_init(&param);
    
    // 注册GAP事件回调函数
    gap_set_cb_func(app_gap_evt_cb);

    // 启用绑定管理模块，将绑定密钥和服务信息保存到Flash中，并在初始化时读取
    gap_bond_manager_init(BLE_BONDING_INFO_SAVE_ADDR, BLE_REMOTE_SERVICE_SAVE_ADDR, 8, true);
    // gap_bond_manager_delete_all(); // 可选：清除所有已存储的绑定信息

    // 获取并打印本地蓝牙地址
    mac_addr_t addr;
    gap_address_get(&addr);
    co_printf("Local BDADDR: 0x%2X%2X%2X%2X%2X%2X\r\n", 
              addr.addr[0], addr.addr[1], addr.addr[2],
              addr.addr[3], addr.addr[4], addr.addr[5]);

    // 添加GATT服务至数据库
    sp_gatt_add_service();  
}


