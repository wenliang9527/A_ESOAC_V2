/**
 * @file app_task.c
 * @brief 应用层主任务 - 协调BLE和4G双通信
 */

#include "app_task.h"
#include <string.h>
#include "co_printf.h"
#include "os_timer.h"
#include "protocol.h"
#include "mqtt_handler.h"
#include "frADC.h"
#include "aircondata.h"
#include "device_config.h"
#include "frATcode.h"
#include "ble_simple_peripheral.h"

// 任务ID
uint16_t app_task_id;

// 定时器
os_timer_t heartbeat_timer;
os_timer_t status_report_timer;
os_timer_t sensor_read_timer;
os_timer_t mqtt_reconnect_timer;

// MQTT重连参数
#define MQTT_RECONNECT_INTERVAL_MS  30000   // 重连间隔30秒
#define MQTT_RECONNECT_MAX_RETRY    10      // 最大重连次数，超过后延长间隔
static uint8_t mqtt_reconnect_count = 0;
static bool mqtt_reconnect_running = false;
static bool mqtt_reconnect_timer_inited = false;

// 心跳定时器回调
static void heartbeat_timer_func(void *arg)
{
    // BLE心跳
    protocol_send_heartbeat(0);

    // MQTT心跳（如果已连接）
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        protocol_send_heartbeat(1);
    }
}

// 状态报告定时器回调
static void status_report_timer_func(void *arg)
{
    // 发送设备状态到BLE和MQTT
    protocol_send_status(0);

    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        protocol_send_status(1);
    }
}

// 传感器读取定时器回调
static void sensor_read_timer_func(void *arg)
{
    // 读取ADC功率值
    fr_ADC_send();

    // 计算功率值（需要根据实际电路调整）
    float power_value = Get_Power_Value(ESAirdata.AIRpowerADCvalue);
    ESAirdata.AIRpowervalue = power_value;

    // 计算温度值
    float temp_value = Get_Temperature_Value(ESAirdata.AIRpowerADCvalue);
    ESAirdata.temp_celsius = temp_value;

    co_printf("Power: %.2fW, Temp: %.2fC\r\n", power_value, temp_value);

    /* BLE：有链路时再组帧，通知仍受 ESAIR CCC 约束 */
    if (gap_get_connect_num() > 0) {
        protocol_send_power(0);
        protocol_send_temperature(0);
    }

    /* MQTT：已连接时上云（与 BLE 独立） */
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        mqtt_handler_send_power();
        mqtt_handler_send_temperature();
    }
}

// MQTT重连定时器回调
static void mqtt_reconnect_timer_func(void *arg)
{
    // 如果已经处于MQTT连接状态，无需重连
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        mqtt_reconnect_running = false;
        return;
    }

    co_printf("MQTT reconnect attempt %d/%d\r\n", mqtt_reconnect_count + 1, MQTT_RECONNECT_MAX_RETRY);

    // 推进状态机（非阻塞，每次推进一步）
    ML307A_MQTTinit();

    // 如果连接成功
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        co_printf("MQTT reconnect success\r\n");
        mqtt_reconnect_count = 0;
        mqtt_reconnect_running = false;
        app_task_send_event(APP_EVT_MQTT_CONNECTED, NULL, 0);
        app_task_stop_reconnect_timer();
    } else {
        mqtt_reconnect_count++;

        // 超过最大重试次数后，延长重连间隔并重置计数
        if (mqtt_reconnect_count >= MQTT_RECONNECT_MAX_RETRY) {
            co_printf("MQTT reconnect max retry reached, extending interval\r\n");
            mqtt_reconnect_count = 0;
            // 延长至120秒
            os_timer_stop(&mqtt_reconnect_timer);
            os_timer_start(&mqtt_reconnect_timer, 120000, false);
        } else {
            // 常规重试：继续按固定间隔推进状态机
            os_timer_start(&mqtt_reconnect_timer, MQTT_RECONNECT_INTERVAL_MS, false);
        }
    }
}

/**
 * @brief 应用层主任务
 * @param param 事件参数
 * @return 事件处理状态
 */
static int app_task_func(os_event_t *param)
{
    switch (param->event_id) {
        case APP_EVT_MQTT_DATA_RECEIVED:
            {
                // 处理MQTT接收到的数据
                // 在mqtt_handler中已经处理，这里可以添加额外的业务逻辑
                co_printf("APP: MQTT data processed\r\n");
            }
            break;

        case APP_EVT_BLE_DATA_RECEIVED:
            {
                uint8_t *data = (uint8_t *)param->param;
                uint16_t len = param->param_len;

                protocol_frame_t frame;
                if (protocol_parse_frame(data, len, &frame)) {
                    protocol_process_frame(&frame, 0);  // 0 = BLE source
                }
            }
            break;

        case APP_EVT_MQTT_CONNECTED:
            {
                co_printf("APP: MQTT connected\r\n");
                // 发送设备信息到MQTT
                protocol_send_device_info(1);
                protocol_send_status(1);
            }
            break;

        case APP_EVT_MQTT_DISCONNECTED:
            {
                co_printf("APP: MQTT disconnected\r\n");
                // 启动重连定时器
                app_task_start_reconnect_timer();
            }
            break;

        case APP_EVT_MQTT_CONFIG_UPDATED:
            {
                co_printf("APP: MQTT config updated, reconnecting...\r\n");
                // 停止正在运行的重连定时器
                app_task_stop_reconnect_timer();

                // 如果当前已连接，先断开
                if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
                    AT_SendCommandFormat("AT+MQTTDISC=0");
                    AT_WaitResponse("OK", 3000);
                    R_atcommand.MLinitflag = ML307A_Idle;
                    mqtt_handler_connection_callback(false);
                }

                // 重置状态机并触发重连（使用新配置）
                memset(&R_atcommand.mqtt_recv, 0, sizeof(R_atcommand.mqtt_recv));
                R_atcommand.ML307Con = DIS_CONNECTING;  // 跳过网络注册（网络仍可用）
                R_atcommand.MLinitflag = ML307A_Idle;
                R_atcommand.RECompleted = false;

                // 启动重连定时器（短延迟后重连）
                app_task_start_reconnect_timer();
            }
            break;

        default:
            break;
    }

    return EVT_CONSUMED;
}

/**
 * @brief 应用任务、协议与设备 Flash 配置（须在 BLE 外设初始化前调用；不含 MQTT）
 */
void app_task_init_early(void)
{
    app_task_id = os_task_create(app_task_func);
    co_printf("APP Task created, id:%d\r\n", app_task_id);

    protocol_init();

    // 含 BLE 名称等；simple_peripheral_init() -> ble_init_device_name() 依赖此处
    device_config_load();
}

/**
 * @brief MQTT 配置加载、处理器与周期定时器（须在 BLE 初始化完成后、ML307 前调用）
 */
void app_task_init_mqtt_timers(void)
{
    mqtt_config_load();
    mqtt_handler_init();

    os_timer_init(&heartbeat_timer, heartbeat_timer_func, NULL);
    os_timer_init(&status_report_timer, status_report_timer_func, NULL);
    os_timer_init(&sensor_read_timer, sensor_read_timer_func, NULL);
    if (!mqtt_reconnect_timer_inited) {
        os_timer_init(&mqtt_reconnect_timer, mqtt_reconnect_timer_func, NULL);
        mqtt_reconnect_timer_inited = true;
    }

    os_timer_start(&heartbeat_timer, 60000, 1);
    os_timer_start(&status_report_timer, 30000, 1);
    os_timer_start(&sensor_read_timer, 10000, 1);
}

/**
 * @brief 应用层任务完整初始化（等价于 early + mqtt_timers，用于无需插入 BLE 的场景）
 */
void app_task_init(void)
{
    app_task_init_early();
    app_task_init_mqtt_timers();
    co_printf("APP Task initialized\r\n");
}

/**
 * @brief 发送事件到应用任务
 * @param event_id 事件ID
 * @param param 参数
 * @param param_size 参数大小
 */
void app_task_send_event(uint16_t event_id, void *param, uint16_t param_size)
{
    os_event_t evt;
    evt.event_id = event_id;
    evt.param = param;
    evt.param_len = param_size;
    os_msg_post(app_task_id, &evt);
}

void app_task_start_reconnect_timer(void)
{
    if (mqtt_reconnect_running) {
        return;  // 已在重连中，避免重复启动
    }
    mqtt_reconnect_running = true;
    mqtt_reconnect_count = 0;

    // 仅在“开始一次重连尝试”时初始化状态机；定时器 tick 内不重置，
    // 否则会导致多步 AT 状态机无法推进到订阅完成态。
    R_atcommand.ML307Con = NET_INITIALIZING;
    R_atcommand.MLinitflag = ML307A_Idle;
    R_atcommand.RECompleted = false;

    if (!mqtt_reconnect_timer_inited) {
        os_timer_init(&mqtt_reconnect_timer, mqtt_reconnect_timer_func, NULL);
        mqtt_reconnect_timer_inited = true;
    }

    // 首次快速触发，后续由回调按策略续期
    os_timer_start(&mqtt_reconnect_timer, 1000, false);
    co_printf("MQTT reconnect timer started\r\n");
}

void app_task_stop_reconnect_timer(void)
{
    mqtt_reconnect_running = false;
    mqtt_reconnect_count = 0;
    os_timer_stop(&mqtt_reconnect_timer);
    co_printf("MQTT reconnect timer stopped\r\n");
}
