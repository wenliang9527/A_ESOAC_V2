#include "frATcode.h"
#include "frusart.h"
#include "app_task.h"
#include "mqtt_handler.h"

// ML307A 模组初始化参数
#define ML307_INIT_RETRY_COUNT      5
#define ML307_AT_TIMEOUT            3000
#define ML307_CPIN_TIMEOUT          5000
#define ML307_CREG_TIMEOUT          10000
#define ML307_REG_WAIT_COUNT        10
#define ML307_REG_POLL_INTERVAL     20000

// 全局变量定义
at_command_t R_atcommand;
uint16_t MLAT_task_id;
os_timer_t MQTTinitout_timer;

// 全局 MQTT 配置参数（初始化默认值）
mqtt_config_t g_mqtt_config = {
    .server_addr = "your-server-address.com",
    .server_port = "1883",
    .client_id = "your-client-id",
    .username = "your-username",
    .password = "your-password",
    .subscribe_topic = "your-subscribe-topic",
    .publish_topic = "your-publish-topic"
};


/**
 * @brief 初始化 ML307 模组 GPIO 引脚
 */
void ML307gpioinit(void)
{
    system_set_port_pull(GPIO_PA6 | GPIO_PA4 | GPIO_PA1, true);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_6, PORTA6_FUNC_A6);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_4, PORTA4_FUNC_A4);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_1, PORTA1_FUNC_A1);
}


/**
 * @brief 控制模组电源开关
 * @param MLstate true=开启，false=关闭
 */
void ML307_PWR(bool MLstate)
{
    if (MLstate) {
        gpio_set_pin_value(GPIO_PORT_A, GPIO_BIT_6, 1);
    } else {
        gpio_set_pin_value(GPIO_PORT_A, GPIO_BIT_6, 0);
    }
}


/**
 * @brief ML307 硬件复位
 */
void ML307Arest(void)
{
    gpio_set_pin_value(GPIO_PORT_A, GPIO_BIT_1, 0);
    co_delay_100us(5000);
    gpio_set_pin_value(GPIO_PORT_A, GPIO_BIT_1, 1);

    if (AT_WaitResponse("+MATREADY:", 50000) == 0) {
        R_atcommand.MLinitflag = ML307AHardware_OK;
        co_printf("ML307_TURN_ON\r\n");
    } else {
        co_printf("ML307_Module_ERROR\r\n");
    }
}


/**
 * @brief 控制模组唤醒状态
 * @param MLwkstate true=唤醒，false=睡眠
 */
void ML307Wakeup(bool MLwkstate)
{
    if (MLwkstate) {
        gpio_set_pin_value(GPIO_PORT_A, GPIO_BIT_4, 1);
    } else {
        gpio_set_pin_value(GPIO_PORT_A, GPIO_BIT_4, 0);
    }
}


/**
 * @brief 通过 AT 指令发送函数（支持格式化字符串）
 * @param format 格式化字符串（类似 printf）
 */
void AT_SendCommandFormat(const char* format, ...)
{
    char full_cmd[256];
    TCommDataPacket buf;
    va_list args;

    // 构造完整字符串
    va_start(args, format);
    int len = vsnprintf(full_cmd, sizeof(full_cmd) - 2, format, args);
    va_end(args);

    // 检查长度是否有效
    if (len > 0 && len < sizeof(full_cmd) - 2) {
        // 追加 \r\n
        full_cmd[len] = '\r';
        full_cmd[len + 1] = '\n';
        full_cmd[len + 2] = '\0';

        // 设置帧长度并发送
        buf.FramLen = len + 2;
        memcpy(buf.FrameBuf, full_cmd, len + 2);
        USART_0_listADD(buf);

        // 打印发送的指令
        co_printf(full_cmd);
    } else {
        co_printf("Error: Command too long or invalid.\r\n");
    }
}


/**
 * @brief 等待 AT 指令响应
 * @param expected 期望的响应字符串
 * @param timeout 超时时间（单位：100us）
 * @retval 0: 收到期望响应
 * @retval 1: 超时
 * @retval 2: 收到 ERROR 响应
 */
uint8_t AT_WaitResponse(const char* expected, uint32_t timeout)
{
    uint32_t current_timeout = timeout;

    while (1) {
        current_timeout--;
        co_delay_100us(10);

        if (R_atcommand.RECompleted) {
            R_atcommand.RECompleted = 0;

            // 检查是否收到期望响应
            if (strstr((char*)R_atcommand.REcmd_string, expected) != NULL) {
                return 0;
            }

            // 检查是否收到错误响应
            if (strstr((char*)R_atcommand.REcmd_string, "ERROR") != NULL) {
                return 2;
            }
        }

        // 超时判断
        if (current_timeout == 0) {
            return 1;
        }
    }
}


/**
 * @brief 获取当前时间
 * @return 0: 成功，1: 失败
 */
uint8_t ML_CCLK(void)
{
    AT_SendCommandFormat("AT+CCLK?");

    if (AT_WaitResponse("+CCLK:", 5000) == 0) {
        char *time_str = strstr((char*)R_atcommand.REcmd_string, "+CCLK:");
        if (time_str != NULL) {
            time_str += 6;  // 跳过"+CCLK:"前缀
            return 0;
        }
    }

    return 1;
}


/**
 * @brief ML307A 模组初始化
 * @return 1: 初始化成功，0: 初始化失败
 */
uint8_t ML307A_Init(void)
{
    uint8_t retry = 0;

    // 1. 检测模组是否存在
    for (retry = 0; retry < ML307_INIT_RETRY_COUNT; retry++) {
        AT_SendCommandFormat("AT");
        co_delay_100us(10000);

        if (AT_WaitResponse("OK", ML307_AT_TIMEOUT) == 0) {
            break;
        }
    }

    if (retry >= ML307_INIT_RETRY_COUNT) {
        return 0;
    }

    // 2. 关闭回显
    AT_SendCommandFormat("ATE0");
    if (AT_WaitResponse("OK", ML307_AT_TIMEOUT) != 0) {
        return 0;
    }

    // 3. 检查 SIM 卡状态
    AT_SendCommandFormat("AT+CPIN?");
    if (AT_WaitResponse("READY", ML307_CPIN_TIMEOUT) != 0) {
        return 0;
    }

    // 4. 检查网络附着状态
    AT_SendCommandFormat("AT+CGATT?");
    if (AT_WaitResponse("+CGATT:1", ML307_AT_TIMEOUT) != 0) {
        return 0;
    }

    // 5. 查询信号强度
    AT_SendCommandFormat("AT+CSQ");
    co_delay_100us(1000);

    // 6. 检查网络注册状态
    AT_SendCommandFormat("AT+CREG?");
    if (AT_WaitResponse("+CREG:0,1", ML307_CREG_TIMEOUT) != 0) {
        for (uint8_t i = 0; i < ML307_REG_WAIT_COUNT; i++) {
            co_delay_100us(ML307_REG_POLL_INTERVAL);
            AT_SendCommandFormat("AT+CREG?");

            if (AT_WaitResponse("+CREG:0,1", ML307_AT_TIMEOUT) == 0) {
                break;
            }
        }

        if (AT_WaitResponse("+CREG:0,1", ML307_AT_TIMEOUT) != 0) {
            return 0;
        }
    }

    return 1;
}


/**
 * @brief 获取运营商信息
 * @param operator_info 存储运营商信息的缓冲区
 * @param buffer_size 缓冲区大小
 * @return 0: 成功，1: 失败
 */
uint8_t ML307A_GetOperatorInfo(char* operator_info, uint8_t buffer_size)
{
    if (operator_info == NULL || buffer_size == 0) {
        return 1;
    }

    AT_SendCommandFormat("AT+COPS?");

    if (AT_WaitResponse("+COPS:", 5000) == 0) {
        char *op_response = strstr((char*)R_atcommand.REcmd_string, "+COPS:");
        if (op_response != NULL) {
            char *name_start = strchr(op_response, '"');
            if (name_start != NULL) {
                name_start++;
                char *name_end = strchr(name_start, '"');
                if (name_end != NULL) {
                    uint8_t copy_len = (name_end - name_start) < (buffer_size - 1) ?
                                       (name_end - name_start) : (buffer_size - 1);
                    strncpy(operator_info, name_start, copy_len);
                    operator_info[copy_len] = '\0';
                    return 0;
                }
            }
        }
    }

    return 1;
}


/**
 * @brief 通过 AT 指令发送MQTT消息
 * @param topic 主题
 * @param data 数据
 * @param len 数据长度
 * @return true:成功 false:失败
 */
bool ML307A_MQTTPublish(const char* topic, uint8_t* data, uint16_t len)
{
    if (topic == NULL || data == NULL || len == 0) {
        return false;
    }

    // 构造MQTT发布指令
    // AT+MQTTPUB=<linkid>,<topic>,<len>
    AT_SendCommandFormat("AT+MQTTPUB=0,\"%s\",%d", topic, len);

    // 等待响应
    if (AT_WaitResponse(">", 2000) == 0) {
        // 发送数据
        for (uint16_t i = 0; i < len; i++) {
            uart_putc_noint_no_wait(UART0, data[i]);
        }

        // 等待发送完成
        if (AT_WaitResponse("OK", 5000) == 0) {
            co_printf("MQTT Publish success\r\n");
            return true;
        }
    }

    co_printf("MQTT Publish failed\r\n");
    return false;
}

/**
 * @brief 通过 AT 指令使 4G 模组 MQTT 连接服务器
 * @note 状态机只有 AT 指令成功时才跳转到下一状态
 */
void ML307A_MQTTinit(void)
{
    uint8_t ret = 0;

    switch (R_atcommand.ML307Con) {
        /* ========== 阶段 1: 网络注册检查 ========== */
        case NET_INITIALIZING:
            AT_SendCommandFormat("AT+CEREG?");
            ret = AT_WaitResponse("+CEREG:0,1", ML307_AT_TIMEOUT);
            if (ret == 0) {
                R_atcommand.ML307Con = DIS_CONNECTING;
            } else {
                co_printf("NET_REG_FAIL:%d\r\n", ret);
            }
            break;

        /* ========== 阶段 2: 断开旧连接 ========== */
        case DIS_CONNECTING:
            AT_SendCommandFormat("AT+MQTTDISC=0");
            ret = AT_WaitResponse("OK", ML307_AT_TIMEOUT);
            if (ret == 0) {
                R_atcommand.ML307Con = PING_RESP;
            } else {
                co_printf("MQTT_DISC_FAIL:%d\r\n", ret);
            }
            break;

        /* ========== 阶段 3: 配置 MQTT 参数 ========== */
        case PING_RESP:
            AT_SendCommandFormat("AT+MQTTCFG=\"pingresp\",0,1");
            ret = AT_WaitResponse("OK", ML307_AT_TIMEOUT);
            if (ret == 0) {
                R_atcommand.ML307Con = PING_REQ;
            }
            break;

        case PING_REQ:
            AT_SendCommandFormat("AT+MQTTCFG=\"pingreq\",0,%d", PING_REQ_TIME);
            ret = AT_WaitResponse("OK", ML307_AT_TIMEOUT);
            if (ret == 0) {
                R_atcommand.ML307Con = KEEP_ALIVE;
            }
            break;

        case KEEP_ALIVE:
            AT_SendCommandFormat("AT+MQTTCFG=\"keepalive\",0,%d", KEEP_ALIVE_TIME);
            ret = AT_WaitResponse("OK", ML307_AT_TIMEOUT);
            if (ret == 0) {
                R_atcommand.ML307Con = CONNECTTING;
            }
            break;

        /* ========== 阶段 4: 查询配置（可选） ========== */
        case QUERY_PING_REQ:
            AT_SendCommandFormat("AT+MQTTCFG=\"pingreq\",0");
            ret = AT_WaitResponse("OK", ML307_AT_TIMEOUT);
            if (ret == 0) {
                R_atcommand.ML307Con = QUERY_KEEP_ALIVE;
            }
            break;

        case QUERY_KEEP_ALIVE:
            AT_SendCommandFormat("AT+MQTTCFG=\"keepalive\",0");
            ret = AT_WaitResponse("OK", ML307_AT_TIMEOUT);
            if (ret == 0) {
                R_atcommand.ML307Con = QUERY_PING_RESP;
            }
            break;

        case QUERY_PING_RESP:
            AT_SendCommandFormat("AT+MQTTCFG=\"pingresp\",0");
            ret = AT_WaitResponse("OK", ML307_AT_TIMEOUT);
            if (ret == 0) {
                R_atcommand.ML307Con = CONNECTTING;
            }
            break;

        /* ========== 阶段 5: 建立 MQTT 连接 ========== */
        case CONNECTTING:
            AT_SendCommandFormat("AT+MQTTCONN=0,\"%s\",%d,\"%s\",\"%s\",\"%s\"",
                                 g_mqtt_config.server_addr,
                                 g_mqtt_config.server_port,
                                 g_mqtt_config.client_id,
                                 g_mqtt_config.username,
                                 g_mqtt_config.password);
            ret = AT_WaitResponse("OK", ML307_AT_TIMEOUT);
            if (ret == 0) {
                R_atcommand.ML307Con = SUBSCRIBING;
            } else {
                co_printf("MQTT_CONN_FAIL:%d\r\n", ret);
            }
            break;

        /* ========== 阶段 6: 订阅主题 ========== */
        case SUBSCRIBING:
            AT_SendCommandFormat("AT+MQTTSUB=0,\"%s\",1", g_mqtt_config.subscribe_topic);
            ret = AT_WaitResponse("OK", ML307_AT_TIMEOUT);
            if (ret == 0) {
                R_atcommand.ML307Con = ML_MQTT_OK;
                R_atcommand.MLinitflag = ML307AMQTT_OK;
                co_printf("MQTT Connected Successfully\r\n");
            } else {
                co_printf("MQTT_SUB_FAIL:%d\r\n", ret);
            }
            break;

        /* ========== 错误处理 ========== */
        case ML_ERROR:
            co_printf("MQTT_STATE_ERROR\r\n");
            R_atcommand.ML307Con = NET_INITIALIZING;
            R_atcommand.MLinitflag = ML307A_Idle;
            break;

        /* ========== 已完成 ========== */
        case ML_MQTT_OK:
            break;

        default:
            co_printf("UNKNOWN_STATE:%d\r\n", R_atcommand.ML307Con);
            R_atcommand.ML307Con = NET_INITIALIZING;
            break;
    }
}


/**
 * @brief MQTT 连接超时定时器回调函数
 */
void MQTTinitout_timer_timer_func(void *arg)
{
    if (R_atcommand.MLinitflag != ML307AMQTT_OK &&
        R_atcommand.MLinitflag != ML307AMQTT_ERR) {
        R_atcommand.MLinitflag = ML307AMQTT_inttimeout;
        co_printf("MQTT link out time\r\n");
    }
}


/**
 * @brief ML307 硬件初始化（连接服务器）
 */
void MLHardwareINIT(void)
{
    R_atcommand.MLinitflag = ML307A_Idle;
    R_atcommand.MLouttime = false;
    R_atcommand.RECompleted = false;
    R_atcommand.ML307Con = NET_INITIALIZING;
    memset(&R_atcommand.mqtt_recv, 0, sizeof(R_atcommand.mqtt_recv));

    ML307gpioinit();
    ML307Wakeup(1);
    ML307_PWR(1);
    ML307Arest();
    ML307A_Init();

    os_timer_init(&MQTTinitout_timer, MQTTinitout_timer_timer_func, NULL);
    os_timer_start(&MQTTinitout_timer, 300000, false);

    uint16_t init_loop_count = 0;

    while (R_atcommand.MLinitflag != ML307AMQTT_OK &&
           R_atcommand.MLinitflag != ML307AMQTT_ERR &&
           R_atcommand.MLinitflag != ML307AMQTT_inttimeout) {
        ML307A_MQTTinit();
        co_delay_100us(1000);
        init_loop_count++;

        if (init_loop_count > 10000) {
            co_printf("Init_TIMEOUT\r\n");
            R_atcommand.MLinitflag = ML307AMQTT_inttimeout;
            break;
        }
    }

    // 首次连接成功后，通知应用层并发送初始数据
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        mqtt_handler_connection_callback(true);
        app_task_send_event(APP_EVT_MQTT_CONNECTED, NULL, 0);
    }
}


/**
 * @brief MQTT断线重连（不包含硬件初始化，仅重走MQTT连接状态机）
 * @note 供重连定时器调用，跳过GPIO/硬件复位/基础AT握手
 */
void ML307A_MQTTReconnect(void)
{
    // 如果已经连接，无需重连
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        return;
    }

    co_printf("MQTT reconnecting...\r\n");

    // 重置MQTT接收状态
    memset(&R_atcommand.mqtt_recv, 0, sizeof(R_atcommand.mqtt_recv));
    R_atcommand.ML307Con = NET_INITIALIZING;
    R_atcommand.MLinitflag = ML307A_Idle;
    R_atcommand.RECompleted = false;

    // 走MQTT连接状态机（复用ML307A_MQTTinit）
    // 从DIS_CONNECTING开始，跳过NET_INITIALIZING（网络应在首次初始化时已就绪）
    R_atcommand.ML307Con = DIS_CONNECTING;

    uint16_t reconnect_loop = 0;
    while (R_atcommand.MLinitflag != ML307AMQTT_OK &&
           R_atcommand.MLinitflag != ML307AMQTT_ERR) {
        ML307A_MQTTinit();
        co_delay_100us(1000);
        reconnect_loop++;

        // 重连也设置超时保护（约30秒）
        if (reconnect_loop > 30000) {
            co_printf("Reconnect_TIMEOUT\r\n");
            R_atcommand.MLinitflag = ML307AMQTT_ERR;
            break;
        }
    }

    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        co_printf("MQTT reconnect OK\r\n");
        mqtt_handler_connection_callback(true);
        app_task_send_event(APP_EVT_MQTT_CONNECTED, NULL, 0);
        app_task_stop_reconnect_timer();
    } else {
        co_printf("MQTT reconnect failed\r\n");
        // 重连失败，定时器会自动再次触发
    }
}


/**
 * @brief 处理ML307A的URC主动上报消息
 * @note 供其他模块调用，处理MQTT相关的异步事件
 */
void ML307A_ProcessURC(void)
{
    if (!R_atcommand.mqtt_recv.urc_ready) {
        return;
    }

    R_atcommand.mqtt_recv.urc_ready = false;

    switch (R_atcommand.mqtt_recv.urc_type) {
        case URC_MQTT_RECV:
            // MQTT消息已在uart0_recv_timer_func中直接处理
            // 此处可作为备用处理入口
            co_printf("URC: MQTT data pending\r\n");
            break;

        case URC_MQTT_CLOSED:
            co_printf("URC: MQTT connection closed by server\r\n");
            if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
                R_atcommand.MLinitflag = ML307A_Idle;
                mqtt_handler_connection_callback(false);
                app_task_send_event(APP_EVT_MQTT_DISCONNECTED, NULL, 0);
            }
            break;

        case URC_MQTT_SUB_OK:
            co_printf("URC: MQTT subscribe OK\r\n");
            break;

        default:
            break;
    }
}

