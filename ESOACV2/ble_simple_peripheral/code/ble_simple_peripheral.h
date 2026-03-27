#ifndef BLE_SIMPLE_PERIPHERAL_H
#define BLE_SIMPLE_PERIPHERAL_H

#include "gap_api.h"

void app_gap_evt_cb(gap_event_t *p_event);

void simple_peripheral_init(void);

/**
 * @brief 动态更新 BLE 设备名称并重启广播，同时保存到 Flash
 * @param new_name  新设备名称字符串
 * @param name_len  名称长度（不含 null 终止符）
 * @return 0 成功；-1 参数错误；-2 名称非法；-3 Flash 保存失败
 */
int ble_update_device_name(const char *new_name, uint8_t name_len);

/**
 * @brief 获取当前 BLE 设备名称
 * @return 设备名称字符串指针（静态缓冲区，无需释放）
 */
const char *ble_get_device_name(void);

#endif
