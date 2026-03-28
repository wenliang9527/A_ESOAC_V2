# URC�����Ż�����ʵʩ����

## һ��ʵʩ����

�����Ż���ԭ�е�URC�������ƴ�**ͬ��ֱ�Ӵ���**��Ϊ**�첽����+��ʱ������**ģʽ�������˴����ظ��������ϵͳ�Ŀ�ά���Ժ���չ�ԡ�

## �����ļ�����嵥

### 2.1 �����ļ�

| �ļ� | ���� |
|-----|------|
| `usercode/urc_parser.h` | URC������ͷ�ļ�������URC���͡����нṹ���������� |
| `usercode/urc_parser.c` | URC������ʵ�֣��������в�����URC�������ַ����� |

### 2.2 �޸��ļ�

| �ļ� | ������� |
|-----|---------|
| `usercode/frATcode.h` | ����`urc_parser.h`���ã��滻`mqtt_recv_ctx_t`Ϊ`urc_queue_t`������URC��ʱ��������� |
| `usercode/frATcode.c` | ��д`ML307A_ProcessURC()`������`ML307A_URC_Init()`��`ML307A_URC_StartTimer()`��`ML307A_URC_StopTimer()` |
| `usercode/frusart.c` | ��`uart0_recv_timer_func()`��ʹ��`urc_parse()`ʶ��URC����� |
| `usercode/app_task.c` | ��`app_task_init_mqtt_timers()`������URC��ʼ���Ͷ�ʱ������ |

## �����ܹ��仯

### 3.1 �Ż�ǰ�ܹ�
```
UART0���� �� uart0_recv_timer_func()
              ������ +MQTTrecv: �� ֱ�ӽ�������
              ������ +MQTTclosed: �� ֱ�Ӵ����������ظ���
              ������ AT��Ӧ �� R_atcommand.REcmd_string
```

### 3.2 �Ż���ܹ�
```
UART0���� �� uart0_recv_timer_func()
              ������ URCʶ�� �� urc_parse() �� �������
              ������ AT��Ӧ �� R_atcommand.REcmd_string
                              ��
              ML307A_URC_StartTimer() (50ms����)
                              ��
              urc_timer_handler() �� urc_queue_pop() �� urc_process_entry()
```

## �ġ��ؼ�����

### 4.1 URC����
- **���**: 8��
- **�����������**: 256�ֽ�
- **������󳤶�**: 128�ֽ�
- **��������**: ÿ����ദ��4�������ⳤʱ��ռ��

### 4.2 ֧�ֵ�URC����
| URC���� | ǰ׺ | ���� |
|---------|------|------|
| URC_MQTT_RECV | +MQTTrecv: | ����topic�����ݣ�����mqtt_handler |
| URC_MQTT_CLOSED | +MQTTclosed: | ���öϿ���־�����ͶϿ��¼� |
| URC_MQTT_SUB_OK | +MQTTsub: | ��־��� |
| URC_MQTT_UNSUB_OK | +MQTTunsub: | ��־��� |
| URC_MQTT_PUB_OK | +MQTTpub: | ��־��� |
| URC_SIM_READY | +CPIN: READY | ��־��� |
| URC_NET_REGISTERED | +CREG: 0,1/0,5 | ��־��� |

### 4.3 ��ʱ������
- **����**: 50ms
- **ģʽ**: �ظ���ʱ
- **����ʱ��**: `app_task_init_mqtt_timers()`������

## �塢�����Ż�Ч��

### 5.1 ���������Ա�
| ģ�� | �Ż�ǰ | �Ż��� | �仯 |
|-----|-------|-------|------|
| frusart.c uart0_recv_timer_func | ~85�� | ~35�� | ����50�� |
| frATcode.c ML307A_ProcessURC | ~30�� | ~60�� | ����URC�������� |
| ����urc_parser.c | 0 | ~415�� | ͳһURC���� |

### 5.2 �����ظ�����
- ? `+MQTTclosed`�����߼���2���ϲ�Ϊ1��
- ? `+MQTTrecv`�����߼��ӷ�ɢ��ͳһ
- ? URCʶ���߼�ͳһ��ǰ׺����

### 5.3 ��չ������
����URC����ֻ�裺
1. ��`urc_prefix_table`������ǰ׺ӳ��
2. ��`urc_process_entry()`�����Ӵ���case

## ����������˵��

### 6.1 ���ּ��ݵĽӿ�
- `AT_WaitResponse()` - ��ȫ����
- `ML307A_MQTTPublish()` - ��ȫ����
- `ML307A_MQTTinit()` - ��ȫ����
- `ML307A_ProcessURC()` - �ӿڲ��䣬�ڲ�ʵ���Ż�

### 6.2 ���ݽṹ�仯
- `at_command_t`�е�`mqtt_recv_ctx_t mqtt_recv`���滻Ϊ`urc_queue_t urc_queue`
- ԭ��`RECompleted`��`REcmd_string`��AT��Ӧ������ȫ����

## �ߡ����Խ���

### 7.1 ���ܲ���
1. MQTT���ӽ����ͶϿ�
2. MQTT�����շ�
3. ATָ����Ӧ
4. URC�����������

### 7.2 ���ܲ���
1. ��ƵURC�����������Ƿ������
2. ����MQTT���ݽ���
3. ATָ���URC��������

### 7.3 �߽����
1. ������ʱ��URC����
2. �������������
3. ��ʽ�쳣��URC

## �ˡ�ע������

1. **�ڴ�ռ��**: ����Լ2KB�ڴ�����URC���кͻ�����
2. **ʱ��仯**: URC������ͬ����Ϊ�첽���ӳ�Լ0-50ms
3. **������Ϣ**: ����URC�����־������������ⶨλ
4. **���˷���**: ��������⣬�ɿ��ٻ��˵�ԭ�汾

## �š������Ż�����

1. ��������URCͳ�Ƽ����������ڼ��
2. ֧�ֶ�̬����URC�������
3. ����URC������ʱ���
4. ����֧��URC���ȼ�

---

**ʵʩ����**: 2026-03-28  
**�汾**: V1.0  
**״̬**: �����ʵʩ
