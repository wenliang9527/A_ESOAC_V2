/* 红外 NEC 类时序常量、PWM/学习状态与对外发送/学习接口 */

#ifndef _FRIRCONVERSION_H
#define _FRIRCONVERSION_H

#include <stdint.h>
#include <stdbool.h>

#include "aircondata.h"

#include "frspi.h"

#define AIRkeyNumber  12

#define IRLEADLOW     9000     /* 引导码低电平时长（µs，与解码一致） */
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
    uint8_t IR_learn_state;
    uint32_t ir_learn_Date[LERANDATACNTMAX];
    uint8_t ir_learn_data_cnt;
    uint32_t ir_carrier_fre;
    uint32_t IRnumberS;
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

extern void ESOAAIR_readkey(void);
extern void ESOAAIR_Savekey(void);

#endif
