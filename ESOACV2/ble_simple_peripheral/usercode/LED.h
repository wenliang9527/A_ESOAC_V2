

#ifndef _LED_H
#define _LED_H

#include <stdint.h>
#include "driver_system.h"
#include "driver_gpio.h"
#include "driver_timer.h"
#include "os_task.h"
#include "os_msg_q.h"
#include "os_timer.h"
#include "co_printf.h"
#include "driver_pmu.h"



#define LED_ON  0
#define LED_OFF 1

void LED_INIT(void);

#endif












