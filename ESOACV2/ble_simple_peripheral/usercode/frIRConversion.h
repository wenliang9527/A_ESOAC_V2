/* 红外 NEC 类时序常量、PWM/学习状态与对外发送/学习接口 */

#ifndef _FRIRCONVERSION_H
#define _FRIRCONVERSION_H

#include <stdint.h>
#include <stdbool.h>

#include "aircondata.h"

#include "frspi.h"

#define AIRkeyNumber  12

/* ========== 协议识别类型定义 ========== */

/* 红外协议类型 */
typedef enum {
    IR_PROTO_UNKNOWN = 0,
    IR_PROTO_NEC,           /* NEC协议 */
    IR_PROTO_GREE,          /* 格力 */
    IR_PROTO_MIDEA,         /* 美的 */
    IR_PROTO_HAIER,         /* 海尔 */
    IR_PROTO_CHIGO,         /* 志高 */
    IR_PROTO_AUX,           /* 奥克斯 */
    IR_PROTO_SONY,          /* 索尼 */
    IR_PROTO_RC5,           /* RC5 */
    IR_PROTO_RC6,           /* RC6 */
    IR_PROTO_CUSTOM         /* 自定义 */
} ir_protocol_type_t;

/* 按键功能类型 */
typedef enum {
    KEY_FUNC_UNKNOWN = 0,
    KEY_FUNC_POWER,         /* 电源 */
    KEY_FUNC_MODE,          /* 模式 */
    KEY_FUNC_TEMP_UP,       /* 温度+ */
    KEY_FUNC_TEMP_DOWN,     /* 温度- */
    KEY_FUNC_WIND,          /* 风速 */
    KEY_FUNC_TIMER,         /* 定时 */
    KEY_FUNC_SLEEP,         /* 睡眠 */
    KEY_FUNC_SWING,         /* 摆风 */
    KEY_FUNC_ECO            /* 节能 */
} key_function_type_t;

/* 温度控制类型 */
typedef enum {
    TEMP_CTRL_UNKNOWN = 0,
    TEMP_CTRL_INCREMENTAL,  /* 温度+/- */
    TEMP_CTRL_DIRECT,       /* 直接设定 */
    TEMP_CTRL_CYCLE         /* 循环 */
} temp_control_type_t;

/* 温度步进 */
typedef enum {
    TEMP_STEP_UNKNOWN = 0,
    TEMP_STEP_0_5,          /* 0.5度 */
    TEMP_STEP_1_0,          /* 1度 */
    TEMP_STEP_2_0           /* 2度 */
} temp_step_type_t;

#define IRLEADLOW     9000     /* 引导码低电平时长（?s，与解码一致） */
#define IRLEADHIGHT   4500

#define IRLOGIC0LOW   560      /* 数据 0 */
#define IRLOGIC0HIGHT 560

#define IRLOGIC1LOW    560     /* 数据 1 */
#define IRLOGIC1HIGHT  1680

#define IRSTOPLOW    600       /* 停止位 */
#define IRSTOPHIGHT     13930 + 500  /* 帧间隔，可按机型微调 */

#define IR_CF_36K   36000
#define IR_CF_38K   38000
#define IR_CF_56K   56000

#define IR_CARRIER_FRE       IR_CF_38K

/* IR_SLEEP_EN=1 时部分逻辑可放主循环；本工程为 0 */
#define IR_SLEEP_EN 0
#define LERANDATABUFMAX 3000
#define LERANDATACNTMAX 200

#define IR_LOGIC_0_THRESHOLD (IRLOGIC0LOW + IRLOGIC0HIGHT)
#define IR_LOGIC_1_THRESHOLD (IRLOGIC1LOW + IRLOGIC1HIGHT)

typedef struct {
    uint8_t IR_Busy;
    uint8_t IR_pwm_state;
    uint8_t IR_pwm_Num;
    uint8_t IR_pwm_SendNum;
    uint32_t IR_Pwm_State_Date[LERANDATACNTMAX];
    uint32_t ir_carrier_fre;
    uint32_t total_count;
    uint32_t high_count_half;
    bool loop;
} TYPEDEFIRPWMTIM;

extern TYPEDEFIRPWMTIM IR_PWM_TIM;

typedef struct {
    /* 基础数据 */
    uint8_t IR_learn_state;
    uint32_t ir_learn_Date[LERANDATACNTMAX];
    uint8_t ir_learn_data_cnt;
    uint32_t ir_carrier_fre;
    uint32_t IRnumberS;
    
    /* 协议识别信息 */
    uint8_t protocol_type;          /* ir_protocol_type_t */
    uint8_t key_function;           /* key_function_type_t */
    
    /* 温度键专用信息 */
    uint8_t temp_ctrl_type;         /* temp_control_type_t */
    uint8_t temp_step;              /* temp_step_type_t */
    
    /* 关联信息 */
    uint8_t paired_key_index;       /* 配对按键索引 */
    uint8_t cycle_count;            /* 循环档位数 */
} TYPEDEFIRLEARNDATA;
extern TYPEDEFIRLEARNDATA ir_learn_data;

typedef struct {
    uint8_t ir_learn_step;
    uint32_t ir_carrier_fre;
    uint8_t ir_carrier_cycle;
    uint8_t ir_learn_start;
    uint16_t ir_carrier_times;
    uint16_t ir_timer_cnt;
    uint32_t ir_learn_data_buf[LERANDATABUFMAX];
    uint16_t ir_learn_data_buf_cnt;
    uint32_t ir_learn_Date[LERANDATACNTMAX];
    uint8_t ir_learn_data_cnt;
    uint32_t ir_carrier_cycle_data[6];
    uint8_t ir_carrier_cycle_data_cnt;
} TYPEDEFIRLEARN;

typedef enum {
    STIR_OnOff,
    STIR_mode,
    STIR_add,
    STIR_sub,
    STIR_Windspeed
} studyIRKeypress;

typedef struct {
    TYPEDEFIRLEARNDATA airbutton[AIRkeyNumber];
    studyIRKeypress AIPstudyKey;
    uint8_t keyExistence[AIRkeyNumber];
    uint8_t keystudy[AIRkeyNumber];
    uint8_t EIRlearnStatus;
} airconditioner;
extern airconditioner ESairkey;

enum IR_LEARN_STEP {
    IR_WAIT_STOP,
    IR_LEARN_GET_DATA,
};

enum IR_MODE {
    IR_SEND,
    IR_LEARN,
};

void IR_decode(uint8_t *ir_data, uint8_t data_size, TYPEDEFIRPWMTIM *IR_Send_struct);
void IR_start_send(TYPEDEFIRPWMTIM *IR_Send_struct);
void IR_init(void);
void IR_stop_send(void);

uint8_t IR_start_learn(void);
void IR_stop_learn(void);

void IR_test_demo0(void);
void IR_test_demo1(void);
void IR_test_demo2(void);

extern void ESOAAIR_IRsend(uint8_t keynumber);
extern void ESOAAIR_IRskeystudy(uint8_t keynumber);
extern void ESOAAIR_UpdateStatusByKey(uint8_t keynumber);

extern void ESOAAIR_readkey(void);
extern void ESOAAIR_Savekey(void);

#endif
