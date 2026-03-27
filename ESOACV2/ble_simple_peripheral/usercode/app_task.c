/* 应用任务：BLE/4G 事件与周期定时器 */

#include "app_task.h"
#include <string.h>
#include "co_printf.h"
#include "os_timer.h"
#include "protocol.h"
#include "frIRConversion.h"
#include "mqtt_handler.h"
#include "frADC.h"
#include "aircondata.h"
#include "device_config.h"
#include "frATcode.h"
#include "ble_simple_peripheral.h"

uint16_t app_task_id;

/* 周期定时器 */
os_timer_t heartbeat_timer;
os_timer_t status_report_timer;
os_timer_t sensor_read_timer;
os_timer_t mqtt_reconnect_timer;

/* MQTT 重连：30s 步进，超限后拉长间隔 */
#define MQTT_RECONNECT_INTERVAL_MS  30000
#define MQTT_RECONNECT_MAX_RETRY    10
static uint8_t mqtt_reconnect_count = 0;
static bool mqtt_reconnect_running = false;
static bool mqtt_reconnect_timer_inited = false;

static void heartbeat_timer_func(void *arg)
{
    protocol_send_heartbeat(0);  /* BLE */
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        protocol_send_heartbeat(1);  /* MQTT */
    }
}

static void status_report_timer_func(void *arg)
{
    protocol_send_status(0);

    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        protocol_send_status(1);
    }
}

static void sensor_read_timer_func(void *arg)
{
    fr_ADC_send();
    float power_value = Get_Power_Value(ESAirdata.AIRpowerADCvalue);
    ESAirdata.AIRpowervalue = power_value;

    float temp_value = Get_Temperature_Value(ESAirdata.AIRntcADCvalue);
    ESAirdata.temp_celsius = temp_value;

    co_printf("Power: %.2fW, Temp: %.2fC\r\n", power_value, temp_value);

    /* BLE：已连接才发；Notify 受 CCC */
    if (gap_get_connect_num() > 0) {
        protocol_send_power(0);
        protocol_send_temperature(0);
    }

    /* MQTT：已连接则上报 */
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        mqtt_handler_send_power();
        mqtt_handler_send_temperature();
    }

#if FR_ADC_PERIODIC_UART_REPORT
    protocol_send_power(PROTOCOL_SRC_UART);
    protocol_send_temperature(PROTOCOL_SRC_UART);
#endif
}

static void mqtt_reconnect_timer_func(void *arg)
{
    /* 已连则停 */
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        mqtt_reconnect_running = false;
        return;
    }

    co_printf("MQTT reconnect attempt %d/%d\r\n", mqtt_reconnect_count + 1, MQTT_RECONNECT_MAX_RETRY);

    ML307A_MQTTinit();

    /* 成功 */
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        co_printf("MQTT reconnect success\r\n");
        mqtt_reconnect_count = 0;
        mqtt_reconnect_running = false;
        app_task_send_event(APP_EVT_MQTT_CONNECTED, NULL, 0);
        app_task_stop_reconnect_timer();
    } else {
        mqtt_reconnect_count++;

        if (mqtt_reconnect_count >= MQTT_RECONNECT_MAX_RETRY) {
            co_printf("MQTT reconnect max retry reached, extending interval\r\n");
            mqtt_reconnect_count = 0;
            os_timer_stop(&mqtt_reconnect_timer);
            os_timer_start(&mqtt_reconnect_timer, 120000, false);
        } else {
            os_timer_start(&mqtt_reconnect_timer, MQTT_RECONNECT_INTERVAL_MS, false);
        }
    }
}

/* 应用任务消息处理 */
static int app_task_func(os_event_t *param)
{
    switch (param->event_id) {
        case APP_EVT_MQTT_DATA_RECEIVED:
            {
                co_printf("APP: MQTT data processed\r\n");
            }
            break;

        case APP_EVT_BLE_DATA_RECEIVED:
            {
                uint8_t *data = (uint8_t *)param->param;
                uint16_t len = param->param_len;

                protocol_frame_t frame;
                if (protocol_parse_frame(data, len, &frame)) {
                    protocol_process_frame(&frame, PROTOCOL_SRC_BLE);
                }
            }
            break;

        case APP_EVT_MQTT_CONNECTED:
            {
                co_printf("APP: MQTT connected\r\n");
                protocol_send_device_info(PROTOCOL_SRC_MQTT);
                protocol_send_status(PROTOCOL_SRC_MQTT);
            }
            break;

        case APP_EVT_MQTT_DISCONNECTED:
            {
                co_printf("APP: MQTT disconnected\r\n");
                app_task_start_reconnect_timer();
            }
            break;

        case APP_EVT_MQTT_CONFIG_UPDATED:
            {
                co_printf("APP: MQTT config updated, reconnecting...\r\n");
                app_task_stop_reconnect_timer();

                /* 已连则先发断开 */
                if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
                    AT_SendCommandFormat("AT+MQTTDISC=0");
                    AT_WaitResponse("OK", 3000);
                    R_atcommand.MLinitflag = ML307A_Idle;
                    mqtt_handler_connection_callback(false);
                }

                memset(&R_atcommand.mqtt_recv, 0, sizeof(R_atcommand.mqtt_recv));
                    R_atcommand.ML307Con = DIS_CONNECTING;
                R_atcommand.MLinitflag = ML307A_Idle;
                R_atcommand.RECompleted = false;

                app_task_start_reconnect_timer();
            }
            break;

        default:
            break;
    }

    return EVT_CONSUMED;
}

/* 协议、Flash、IR；须早于 BLE 外设初始化 */
void app_task_init_early(void)
{
    app_task_id = os_task_create(app_task_func);
    co_printf("APP Task created, id:%d\r\n", app_task_id);

    protocol_init();

    device_config_load();  /* BLE 名等，供 ble_init_device_name */

    /* IR 任务：发完回收 PC5/Timer0 */
    IR_init();
}

/* MQTT 配置与周期定时器；BLE 初始化之后 */
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
    os_timer_start(&sensor_read_timer, ADC_SAMPLE_PERIOD_MS, 1);
}

/* early + mqtt_timers */
void app_task_init(void)
{
    app_task_init_early();
    app_task_init_mqtt_timers();
    co_printf("APP Task initialized\r\n");
}

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
        return;
    }
    mqtt_reconnect_running = true;
    mqtt_reconnect_count = 0;

    /* 每次重连会话初始化一次状态机；tick 内勿清 */
    R_atcommand.ML307Con = NET_INITIALIZING;
    R_atcommand.MLinitflag = ML307A_Idle;
    R_atcommand.RECompleted = false;

    if (!mqtt_reconnect_timer_inited) {
        os_timer_init(&mqtt_reconnect_timer, mqtt_reconnect_timer_func, NULL);
        mqtt_reconnect_timer_inited = true;
    }

    /* 先 1s，后续由回调续期 */
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
