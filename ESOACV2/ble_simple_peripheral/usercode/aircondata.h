/* 空调运行状态、按键枚举与 RTC 时间（与协议/红外层共用） */

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
} AIRSwitchkey;

typedef enum {
    airmode_cold,
    airmode_hot,
    airmode_Dehumidification,
    airmode_Supply,
    airmode_Auto,
    airmode_Sleep
} AIRmodekey;

typedef enum {
    airControl_Temp,
    airControl_wind,
} AIRControlkey;

typedef enum {
    airws_min,
    airws_Medium,
    airws_max
} AIRWindspeedkey;

typedef enum {
    KP_OnOff,       /* 电源 */
    KP_mode,        /* 模式 */
    KP_add,         /* 温度+ */
    KP_sub,         /* 温度- */
    KP_Windspeed    /* 风速 */
} AIRKeypress;

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
    AIRKeypress AIPKP;              /* 最近一次遥控按键类型 */
    AIRSwitchkey AIRStatus;         /* 开关状态 */
    AIRmodekey AIRMODE;             /* 运行模式 */
    AIRControlkey AIRCon;           /* 上次调节：温度或风速 */
    uint8_t AIRTemperature;         /* 设定温度 */
    uint8_t AIRWindspeed;           /* 风速档 */

    uint16_t AIRpowerADCvalue;      /* PD4/ADC0 功率通道原始值 */
    uint16_t AIRntcADCvalue;        /* PD5/ADC1 NTC 原始值 */
    float AIRpowervalue;            /* 换算后功率 */
    float temp_celsius;             /* 换算温度（°C） */

    Airtimes EStime;
} Airparameters;

extern Airparameters ESAirdata;

extern void FR_rtcinit(void);
// extern void Timestebreak(TCommDataPacket buf1, Airparameters buf2);

#endif
