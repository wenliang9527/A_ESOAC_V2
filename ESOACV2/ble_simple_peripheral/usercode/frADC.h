#ifndef _FRADC_H
#define _FRADC_H

#include <stdint.h>
#include "driver_system.h"
#include "driver_gpio.h"
#include "driver_adc.h"
#include "os_task.h"
#include "os_msg_q.h"
#include "os_timer.h"
#include "co_printf.h"
#include "aircondata.h"

#define ADC_VREF_MV         3300
#define CT_RATIO            1000
#define SAMPLING_RATE_MS    1000

extern void fr_ADC_init(void);
extern void fr_ADC_star(void);
extern void fr_ADC_send(void);
extern void fr_ADC_stop(void);
extern float Get_Power_Value(uint16_t adc_val);
extern float Get_Temperature_Value(uint16_t adc_val);

#endif
