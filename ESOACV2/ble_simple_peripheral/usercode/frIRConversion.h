#ifndef _FRIRCONVERSION_H
#define _FRIRCONVERSION_H

#include <stdint.h>
#include <stdbool.h>

#include "aircondata.h"

#include "frspi.h"

#define AIRkeyNumber  12

#define IRLEADLOW     9000     //引导码
#define IRLEADHIGHT   4500

#define IRLOGIC0LOW   560      //0
#define IRLOGIC0HIGHT 560

#define IRLOGIC1LOW    560     //1
#define IRLOGIC1HIGHT  1680

#define IRSTOPLOW    600        //停止位
#define IRSTOPHIGHT     13930+500 //停止标志位长度   也是红外学习中判定为停止位的时间长度

#define IR_CF_36K   36000
#define IR_CF_38K   38000
#define IR_CF_56K   56000



#define IR_CARRIER_FRE       IR_CF_38K


#define IR_SLEEP_EN 0  //系统开启sleep的情况下，需要将该宏定义为1，否则定义为0
#define LERANDATABUFMAX 3000
#define LERANDATACNTMAX 200


// IR 协议参数如下（可根据实际协议调整）
#define IR_LOGIC_0_THRESHOLD (IRLOGIC0LOW + IRLOGIC0HIGHT) // 逻辑 0 的总时间阈值
#define IR_LOGIC_1_THRESHOLD (IRLOGIC1LOW + IRLOGIC1HIGHT) // 逻辑 1 的总时间阈值


//红外发送参数结构体
typedef struct
{
    uint8_t IR_Busy;//IR 发送busy
    uint8_t IR_pwm_state;//高为1，低为0
    uint8_t IR_pwm_Num;//IR 待发送间隔
    uint8_t IR_pwm_SendNum;//IR 已发送间隔
    uint32_t IR_Pwm_State_Date[LERANDATACNTMAX];//保存将红外编码转成定时时间的数据
    uint32_t ir_carrier_fre;//载波频率
    uint32_t total_count;//pwm total_count
    uint32_t high_count_half;//pwm high_count
    bool loop;//IR是否循环发送 true:循环发送 false:单次发送
} TYPEDEFIRPWMTIM;

extern TYPEDEFIRPWMTIM IR_PWM_TIM;

//红外学习后的数据结果结构体
typedef struct
{
    uint8_t IR_learn_state;//IR 学习状态，bit0:0：未学习， 1：已学习;  bit1 1:学习中,0:不在学习中
    uint32_t ir_learn_Date[LERANDATACNTMAX];//保存将红外学习来的时间间隔保存到该数据中，可以直接发送
    uint8_t ir_learn_data_cnt;//学习间隔个数
    uint32_t ir_carrier_fre;//载波频率
		uint32_t IRnumberS;
} TYPEDEFIRLEARNDATA;
extern TYPEDEFIRLEARNDATA ir_learn_data;

//红外学习相关的结构体
typedef struct
{
    uint8_t ir_learn_step;//IR学习步骤
    uint32_t ir_carrier_fre;//载波频率
    uint8_t ir_carrier_cycle;//载波周期
    uint8_t ir_learn_start;//1:一开始 0：未开始
    uint16_t ir_carrier_times;//载波计数
    uint16_t ir_timer_cnt;//定时器计数
//  uint8_t ir_learn_data_cnt;//学习间隔个数
//  uint32_t ir_learn_Date[100];//保存红外学习的间隔
    uint32_t ir_learn_data_buf[LERANDATABUFMAX];//ir脉冲间隔缓存
    uint16_t ir_learn_data_buf_cnt;//ir脉冲间隔缓存个数
    uint32_t ir_learn_Date[LERANDATACNTMAX];//保存将红外学习来的时间间隔保存到该数据中，可以直接发送
    uint8_t ir_learn_data_cnt;//学习间隔个数
    uint32_t ir_carrier_cycle_data[6];//载波频率数据缓存
    uint8_t ir_carrier_cycle_data_cnt;//载波频率数据缓存个数
} TYPEDEFIRLEARN;
//extern TYPEDEFIRLEARN ir_learn;



typedef enum 
{
	STIR_OnOff,  //开关
	STIR_mode,   //模式
	STIR_add,    //加
	STIR_sub,    //减
	STIR_Windspeed    //风速调节
}studyIRKeypress;


typedef struct
{
	TYPEDEFIRLEARNDATA airbutton[AIRkeyNumber];
  studyIRKeypress AIPstudyKey;                 //空调按键
	uint8_t keyExistence[AIRkeyNumber];          //判断是否存在按键值
	uint8_t keystudy[AIRkeyNumber];          			//判断按键是否已经学习
	uint8_t EIRlearnStatus;    										//当前是否在红外按键学习状态
}airconditioner;
extern airconditioner   ESairkey;

enum IR_LEARN_STEP
{
    //IR_LEARN_GET_CARRIER,
    IR_WAIT_STOP,
    IR_LEARN_GET_DATA,
};

enum IR_MODE
{
    IR_SEND,
    IR_LEARN,
};


/*

2 在函数 IR_start_send() 和 IR_task_func() 中配置红外使能和停止引脚。
3 在函数 IR_start_learn() 和 IR_stop_learn() 中配置红外学习外部中断服务程序。
4 为以下宏配置红外学习引脚：
    #define IR_LEARN_PIN_INIT() 
    #define IR_LEARN_DISENABLE()
    #define IR_LEARN_ENABLE()
*/
void IR_decode(uint8_t *ir_data,uint8_t data_size,TYPEDEFIRPWMTIM *IR_Send_struct);
void IR_start_send(TYPEDEFIRPWMTIM *IR_Send_struct);
void IR_init(void);
void IR_stop_send(void);

uint8_t IR_start_learn(void);
void IR_stop_learn(void);


/*
1 仅发送红外值，遵循 IR_test_demo0()；
2 从另一个控制器学习红外值，遵循 IR_test_demo1()；调用后，5秒定时器结束后，学习停止
   在学习过程中，如果在 timer0_isr_ram() 中 IR_learn_state = ir_data_check()，则 IR_learn_state = true。学习成功。
3 如果你想重新发送在 IR_test_demo1() 中学到的内容，调用 IR_test_demo2() 来重新发送。
*/
void IR_test_demo0(void);
void IR_test_demo1(void);
void IR_test_demo2(void);


int ir_data_to_int(TYPEDEFIRLEARNDATA *ESIR_learn);


// 声明外部函数 ESOAAIR_IRsend，用于发送指定按键编号的红外数据
// 参数 keynumber：表示要发送的按键编号（对应 airbutton 数组中的索引）
extern void ESOAAIR_IRsend(uint8_t keynumber);

// 声明外部函数 ESOAAIR_IRskeystudy，用于启动指定按键编号的红外学习功能
// 参数 keynumber：表示要学习的按键编号（对应 airbutton 数组中的索引）
extern void ESOAAIR_IRskeystudy(uint8_t keynumber);

extern void ESOAAIR_readkey(void);
extern void ESOAAIR_Savekey(void);
extern void process_ir_data(uint8_t keynumber);

#endif

