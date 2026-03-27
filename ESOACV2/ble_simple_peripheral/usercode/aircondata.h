#ifndef _AIRCONDATA_H
#define _AIRCONDATA_H

#include <stdint.h>
#include <stdbool.h>
#include "stdarg.h"	 	 
#include "stdio.h"	 	 
#include "string.h"


#include "driver_rtc.h"
#include "frusart.h"
#include "frIRConversion.h"
#include "frspi.h"





typedef enum {
	airSW_ON,
	airSW_OFF
}AIRSwitchkey;

typedef enum {
	airmode_cold,
	airmode_hot,
	airmode_Dehumidification,
	airmode_Supply,
	airmode_Auto,
	airmode_Sleep
}AIRmodekey;

typedef enum 
{
	airControl_Temp,
	airControl_wind,
}AIRControlkey;

typedef enum 
{
	airws_min,
	airws_Medium,
	airws_max
}AIRWindspeedkey;

typedef enum 
{
	KP_OnOff,  //开关
	KP_mode,   //模式
	KP_add,    //加
	KP_sub,    //减
	KP_Windspeed    //风速调节
}AIRKeypress;

typedef struct {
	uint16_t ES_Year;
	uint8_t ES_Moon;
	uint8_t ES_Day;
	uint8_t ES_Hours;
	uint8_t ES_Minutes;
	uint8_t ES_Second;
	uint8_t ES_Week;
} Airtimes;
typedef struct {
		uint8_t MUConlyID[6];
	  AIRKeypress AIPKP;                 //空调按键
	  AIRSwitchkey AIRStatus;  					 //空调状态
		AIRmodekey  AIRMODE;   						 //空调模式
	  AIRControlkey AIRCon;  						 //温度/风速控制切换
    uint8_t AIRTemperature;            // 空调温度
	  uint8_t AIRWindspeed;              //风速
	
		uint16_t AIRpowerADCvalue;
		float AIRpowervalue;             //当前功率
	  uint16_t temp_celsiusvalue;
		float temp_celsius;               //环境温度
	
		Airtimes EStime;
} Airparameters;



extern Airparameters ESAirdata;




extern void FR_rtcinit(void);
//extern void Timestebreak(TCommDataPacket buf1,Airparameters buf2);

#endif




