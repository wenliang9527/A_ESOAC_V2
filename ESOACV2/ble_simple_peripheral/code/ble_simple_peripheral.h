#ifndef BLE_SIMPLE_PERIPHERAL_H
#define BLE_SIMPLE_PERIPHERAL_H

#include "gap_api.h"

void app_gap_evt_cb(gap_event_t *p_event);

void simple_peripheral_init(void);

int ble_update_device_name(const char *new_name, uint8_t name_len);

#endif
