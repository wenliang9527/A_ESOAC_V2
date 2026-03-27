#ifndef _FRIIC_H
#define _FRIIC_H

#include <stdint.h>
#include <string.h>
#include "driver_gpio.h"
#include "driver_iic.h"

#define IIC_MAX_BUFF              40
#define IIC_SLAVE_ADDRESS         0x5A
#define IIC_MS_SEND_ADDRESS       (0x5A<<1)

struct iic_recv_data {
    uint8_t  start_flag;
    uint8_t  finish_flag;
    uint16_t length;
    uint16_t recv_cnt;
    uint8_t  recv_length_buff[2];
    uint8_t  buff[IIC_MAX_BUFF];
};

#endif
