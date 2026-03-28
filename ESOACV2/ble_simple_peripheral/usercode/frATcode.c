#include "frATcode.h"
#include "frusart.h"
#include "app_task.h"
#include "mqtt_handler.h"

/* ML307A ��ʱ�����Բ��� */
#define ML307_INIT_RETRY_COUNT      5
#define ML307_AT_TIMEOUT            3000
#define ML307_CPIN_TIMEOUT          5000
#define ML307_CREG_TIMEOUT          10000
#define ML307_REG_WAIT_COUNT        10
#define ML307_REG_POLL_INTERVAL     20000

/* AT ״̬ȫ�� */
at_command_t R_atcommand;
uint16_t MLAT_task_id;
os_timer_t MQTTinitout_timer;

/* MQTT Ĭ��ռλ���ɱ� Flash/Э�鸲�ǣ� */
mqtt_config_t g_mqtt_config = {
    .server_addr = "your-server-address.com",
    .server_port = "1883",
    .client_id = "your-client-id",
    .username = "your-username",
    .password = "your-password",
    .subscribe_topic = "your-subscribe-topic",
    .publish_topic = "your-publish-topic"
};



/* ML307 GPIO */
void ML307gpioinit(void)
{
    system_set_port_pull(GPIO_PA6 | GPIO_PA4 | GPIO_PA1, true);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_6, PORTA6_FUNC_A6);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_4, PORTA4_FUNC_A4);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_1, PORTA1_FUNC_A1);
}



/* ģ���Դ */
void ML307_PWR(bool MLstate)
{
    if (MLstate) {
        gpio_set_pin_value(GPIO_PORT_A, GPIO_BIT_6, 1);
    } else {
        gpio_set_pin_value(GPIO_PORT_A, GPIO_BIT_6, 0);
    }
}



/* Ӳ����λ */
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



/* ���ѿ��� */
void ML307Wakeup(bool MLwkstate)
{
    if (MLwkstate) {
        gpio_set_pin_value(GPIO_PORT_A, GPIO_BIT_4, 1);
    } else {
        gpio_set_pin_value(GPIO_PORT_A, GPIO_BIT_4, 0);
    }
}



/* ��ʽ�� AT ���� UART0 */
void AT_SendCommandFormat(const char* format, ...)
{
    char full_cmd[256];
    TCommDataPacket buf;
    va_list args;

    va_start(args, format);
    int len = vsnprintf(full_cmd, sizeof(full_cmd) - 2, format, args);
    va_end(args);

    if (len > 0 && len < sizeof(full_cmd) - 2) {
        full_cmd[len] = '\r';
        full_cmd[len + 1] = '\n';
        full_cmd[len + 2] = '\0';

        buf.FramLen = len + 2;
        memcpy(buf.FrameBuf, full_cmd, len + 2);
        USART_0_listADD(buf);

        co_printf(full_cmd);
    } else {
        co_printf("Error: Command too long or invalid.\r\n");
    }
}



/* �� AT ��Ӧ��timeout ��λ 100us */
uint8_t AT_WaitResponse(const char* expected, uint32_t timeout)
{
    uint32_t current_timeout = timeout;

    while (1) {
        current_timeout--;
        co_delay_100us(10);

        if (R_atcommand.RECompleted) {
            R_atcommand.RECompleted = 0;

            if (strstr((char*)R_atcommand.REcmd_string, expected) != NULL) {
                return 0;
            }

            if (strstr((char*)R_atcommand.REcmd_string, "ERROR") != NULL) {
                return 2;
            }
        }

        if (current_timeout == 0) {
            return 1;
        }
    }
}



/* �� +CCLK */
uint8_t ML_CCLK(void)
{
    AT_SendCommandFormat("AT+CCLK?");

    if (AT_WaitResponse("+CCLK:", 5000) == 0) {
        char *time_str = strstr((char*)R_atcommand.REcmd_string, "+CCLK:");
        if (time_str != NULL) {
            time_str += 6;  // ????"+CCLK:"??
            return 0;
        }
    }

    return 1;
}



/* ģ����� AT ���� */
uint8_t ML307A_Init(void)
{
    uint8_t retry = 0;

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

    AT_SendCommandFormat("ATE0");
    if (AT_WaitResponse("OK", ML307_AT_TIMEOUT) != 0) {
        return 0;
    }

    AT_SendCommandFormat("AT+CPIN?");
    if (AT_WaitResponse("READY", ML307_CPIN_TIMEOUT) != 0) {
        return 0;
    }

    AT_SendCommandFormat("AT+CGATT?");
    if (AT_WaitResponse("+CGATT:1", ML307_AT_TIMEOUT) != 0) {
        return 0;
    }

    AT_SendCommandFormat("AT+CSQ");
    co_delay_100us(1000);

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



/* MQTT ���� */
bool ML307A_MQTTPublish(const char* topic, uint8_t* data, uint16_t len)
{
    if (topic == NULL || data == NULL || len == 0) {
        return false;
    }

    // AT+MQTTPUB=<linkid>,<topic>,<len>
    AT_SendCommandFormat("AT+MQTTPUB=0,\"%s\",%d", topic, len);

    if (AT_WaitResponse(">", 2000) == 0) {
        for (uint16_t i = 0; i < len; i++) {
            uart_putc_noint_no_wait(UART0, data[i]);
        }

        if (AT_WaitResponse("OK", 5000) == 0) {
            co_printf("MQTT Publish success\r\n");
            return true;
        }
    }

    co_printf("MQTT Publish failed\r\n");
    return false;
}


/* MQTT ����״̬����ÿ���ɹ���ǰ�� */
void ML307A_MQTTinit(void)
{
    uint8_t ret = 0;

    switch (R_atcommand.ML307Con) {
        /* �׶�1���������ע�� */
        case NET_INITIALIZING:
            AT_SendCommandFormat("AT+CEREG?");
            ret = AT_WaitResponse("+CEREG:0,1", ML307_AT_TIMEOUT);
            if (ret == 0) {
                R_atcommand.ML307Con = DIS_CONNECTING;
            } else {
                co_printf("NET_REG_FAIL:%d\r\n", ret);
            }
            break;

        /* �׶�2���Ͽ������� */
        case DIS_CONNECTING:
            AT_SendCommandFormat("AT+MQTTDISC=0");
            ret = AT_WaitResponse("OK", ML307_AT_TIMEOUT);
            if (ret == 0) {
                R_atcommand.ML307Con = PING_RESP;
            } else {
                co_printf("MQTT_DISC_FAIL:%d\r\n", ret);
            }
            break;

        /* �׶�3������ MQTT ���� */
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

        /* �׶�4����ѯ���ã���ѡ�� */
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

        /* �׶�5������ MQTT ���� */
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

        /* �׶�6���������� */
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

        /* ���󣺻س�ʼ̬ */
        case ML_ERROR:
            co_printf("MQTT_STATE_ERROR\r\n");
            R_atcommand.ML307Con = NET_INITIALIZING;
            R_atcommand.MLinitflag = ML307A_Idle;
            break;

        /* ����� */
        case ML_MQTT_OK:
            break;

        default:
            co_printf("UNKNOWN_STATE:%d\r\n", R_atcommand.ML307Con);
            R_atcommand.ML307Con = NET_INITIALIZING;
            break;
    }
}



/* MQTT ��ʼ����ʱ */
void MQTTinitout_timer_timer_func(void *arg)
{
    if (R_atcommand.MLinitflag != ML307AMQTT_OK &&
        R_atcommand.MLinitflag != ML307AMQTT_ERR) {
        R_atcommand.MLinitflag = ML307AMQTT_inttimeout;
        co_printf("MQTT link out time\r\n");
    }
}



/* �ϵ�+�� MQTT */
void MLHardwareINIT(void)
{
    R_atcommand.MLinitflag = ML307A_Idle;
    R_atcommand.MLouttime = false;
    R_atcommand.RECompleted = false;
    R_atcommand.ML307Con = NET_INITIALIZING;
    urc_queue_init(&R_atcommand.urc_queue);

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

    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        mqtt_handler_connection_callback(true);
        app_task_send_event(APP_EVT_MQTT_CONNECTED, NULL, 0);
    }
}



/* ������ MQTT ״̬�� */
void ML307A_MQTTReconnect(void)
{
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        return;
    }

    co_printf("MQTT reconnecting...\r\n");

    urc_queue_init(&R_atcommand.urc_queue);
    R_atcommand.ML307Con = NET_INITIALIZING;
    R_atcommand.MLinitflag = ML307A_Idle;
    R_atcommand.RECompleted = false;

    R_atcommand.ML307Con = DIS_CONNECTING;

    uint16_t reconnect_loop = 0;
    while (R_atcommand.MLinitflag != ML307AMQTT_OK &&
           R_atcommand.MLinitflag != ML307AMQTT_ERR) {
        ML307A_MQTTinit();
        co_delay_100us(1000);
        reconnect_loop++;

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
    }
}



/* ============================================================================
 * �µ�URC�������� - ʹ��URC���кͶ�ʱ������
 * ���ԭ�е�ML307A_ProcessURCʵ��
 * ============================================================================ */

/**
 * @brief URC������ʱ���ص�����
 * ��ʱ��URC������ȡ��������URC��Ŀ
 */
static void urc_timer_handler(void *arg)
{
    (void)arg;
    
    urc_entry_t entry;
    uint8_t processed_count = 0;
    
    // һ�δ������4��URC�����ⳤʱ��ռ��
    while (urc_queue_pop(&R_atcommand.urc_queue, &entry) && processed_count < 4) {
        urc_process_entry(&entry);
        processed_count++;
    }
    
    if (processed_count > 0) {
        co_printf("URC processed: %d entries\r\n", processed_count);
    }
}

/**
 * @brief ��ʼ��URC����ϵͳ
 * Ӧ��ϵͳ����ʱ����
 */
void ML307A_URC_Init(void)
{
    // ��ʼ��URC����
    urc_queue_init(&R_atcommand.urc_queue);
    
    // ��ʼ����ʱ����־
    R_atcommand.urc_timer_inited = false;
    
    co_printf("URC system initialized\r\n");
}

/**
 * @brief ����URC������ʱ��
 * ��ʱ�����ڣ�50ms
 */
void ML307A_URC_StartTimer(void)
{
    if (!R_atcommand.urc_timer_inited) {
        os_timer_init(&R_atcommand.urc_timer, urc_timer_handler, NULL);
        R_atcommand.urc_timer_inited = true;
    }
    
    os_timer_start(&R_atcommand.urc_timer, 50, true);  // 50ms���ڣ��ظ�
    co_printf("URC timer started\r\n");
}

/**
 * @brief ֹͣURC������ʱ��
 */
void ML307A_URC_StopTimer(void)
{
    if (R_atcommand.urc_timer_inited) {
        os_timer_stop(&R_atcommand.urc_timer);
        co_printf("URC timer stopped\r\n");
    }
}

/**
 * @brief URC������ں��������ݾɽӿڣ�
 * ����ֱ�ӴӶ����д������д�������URC
 * ���Ա���ʽ���ã�Ҳ����������ʱ���Զ�����
 */
void ML307A_ProcessURC(void)
{
    urc_entry_t entry;
    
    // �������������д�������URC
    while (urc_queue_pop(&R_atcommand.urc_queue, &entry)) {
        urc_process_entry(&entry);
    }
}

