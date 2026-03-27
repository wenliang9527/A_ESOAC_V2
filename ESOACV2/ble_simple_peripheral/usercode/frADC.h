#ifndef _FRADC_H
#define _FRADC_H

#include <stdint.h>
#include "driver_system.h"
#include "driver_gpio.h"
#include "driver_adc.h"
#include "os_timer.h"
#include "co_printf.h"
#include "aircondata.h"

/*
 * SAR ADC 数据寄存器有效位宽 10 bit，换算与 1023 满量程码一致（可按产线实测改为略低码值）。
 */
#define ADC_CODE_FULL_SCALE     1023.0f
#define ADC_CODE_MAX_UINT       1023u

/* 与 fr_ADC_star、sensor_read_timer 周期一致（ms） */
#define ADC_SAMPLE_PERIOD_MS    10000u

/* 参考电压（mV），与 ADC_REFERENCE_AVDD 应用换算一致；标定可改此宏或改用 adc_get_ref_voltage */
#define ADC_VREF_MV             3300

/* 功率链路系数（按互感/取样/整流原理图标定） */
#define ADC_POWER_VDIV_REF_MV   3300.0f
#define ADC_POWER_I_COEFF       20.0f
#define ADC_POWER_AC_V          220.0f

/* NTC 分压：上拉电阻与 Beta（按原理图核对） */
#define NTC_PULLUP_OHMS         10000.0f
#define NTC_BETA                3950.0f

/* adc_val < 1 时无法分压换算，返回哨兵值（°C） */
#define FR_ADC_TEMP_INVALID_C   (-999.0f)

/*
 * 为 1 时：传感器周期任务在 BLE/MQTT 之外，经 UART1 再发一帧协议功率/温度（PROTOCOL_SRC_UART）。
 * 默认 0，避免未接 UART 主机时总线噪声。
 */
#ifndef FR_ADC_PERIODIC_UART_REPORT
#define FR_ADC_PERIODIC_UART_REPORT 0
#endif

extern void fr_ADC_init(void);
extern void fr_ADC_star(void);
extern void fr_ADC_send(void);
extern float Get_Power_Value(uint16_t adc_val);
extern float Get_Temperature_Value(uint16_t adc_val);

#endif
