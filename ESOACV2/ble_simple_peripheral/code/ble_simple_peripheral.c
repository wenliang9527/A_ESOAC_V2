/**
 * Copyright (c) 2019, Freqchip
 * 
 * All rights reserved.
 * 
 * 
 */
 
 /*
* INCLUDES (????????)
*/
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "os_timer.h"
#include "gap_api.h"
#include "gatt_api.h"
#include "ble_simple_peripheral.h"

#include "sys_utils.h"
#include "flash_usage_config.h"
#include "jump_table.h"

#include "device_config.h"
#include "frspi.h"
#include "co_printf.h"
/*
* MACROS (????)
*/

/*
* CONSTANTS (????????)
*/

// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertisting)
// GAP-??????????,??31?????.??????????????????????????.
static uint8_t adv_data[] =
{
  // service UUID, to notify central devices what services are included
  // in this peripheral. ????central????????????, ???????????????????.
  0x03,   // length of this data
  GAP_ADVTYPE_16BIT_MORE,      // some of the UUID's, but not all
  0xFF,
  0xFE,
};

// GAP - Scan response data (max size = 31 bytes, though this is
// best kept short to conserve power while advertisting)
// GAP-Scan response????,??31?????.??????????????????????????.
 static uint8_t scan_rsp_data[] =
{
  // complete name ???????
  0x12,   // length of this data
  GAP_ADVTYPE_LOCAL_NAME_COMPLETE,
	'E',
	'S',
	'O',
	'A',
	'C',
	'S',
	' ',
	'P',
	'e',
	'r',
	'i',
	'p',
	'N',
	'E',
	'M',
	'A',
	'O', // ????????"Simple_Peripheral"

  // Tx power level ???????
  0x02,   // length of this data
  GAP_ADVTYPE_POWER_LEVEL,
  0,	   // 0dBm
};

/*
* TYPEDEFS (???????)
*/

/*
* GLOBAL VARIABLES (??????)
*/

// BLE???????????? (???20??? + '\0')
static char g_ble_dev_name[BLE_NAME_MAX_LEN + 1] = "ESOAC";

/**
 * @brief Default BLE name: ESOAC + 12 hex digits of public address (__jump_table.addr)
 */
static void ble_generate_device_name(void)
{
    const uint8_t *a = __jump_table.addr.addr;
    snprintf(g_ble_dev_name, sizeof(g_ble_dev_name),
             "ESOAC%02X%02X%02X%02X%02X%02X",
             a[0], a[1], a[2], a[3], a[4], a[5]);
}

/**
 * @brief ?????BLE???????
 *        ?????: Flash???????????? > ???ID????????????
 * @note  device_config_load() ???? app_task_init_early() ???????????????
 */
static void ble_init_device_name(void)
{
    // ??????????????????????config????app_task_init?????
    const char *saved_name = device_config_get_ble_name();
    uint8_t saved_len = device_config_get_ble_name_len();

    if (saved_len > 0 && saved_name[0] != '\0') {
        // Flash??????????????????
        memcpy(g_ble_dev_name, saved_name, saved_len);
        g_ble_dev_name[saved_len] = '\0';
        co_printf("BLE name loaded from flash: \"%s\"\r\n", g_ble_dev_name);
    } else {
        // ????????????????ID???????????
        ble_generate_device_name();
        co_printf("BLE name generated from BD_ADDR: \"%s\"\r\n", g_ble_dev_name);
    }
}

/*
* LOCAL VARIABLES (???????)
*/
os_timer_t update_param_timer;


 
/*
* LOCAL FUNCTIONS (???????)
*/
static void sp_start_adv(void);
/*
* EXTERN FUNCTIONS (??????)
*/

/*
* PUBLIC FUNCTIONS (??????)
*/

/** @function group ble peripheral device APIs (ble????????API)
 * @{
 */

void param_timer_func(void *arg)
{
    co_printf("param_timer_func\r\n");
    gap_conn_param_update(0, 12, 12, 55, 600);
}
/*********************************************************************
 * @fn      app_gap_evt_cb
 *
 * @brief   Application layer GAP event callback function. Handles GAP evnets.
 *
 * @param   p_event - GAP events from BLE stack.
 *       
 *
 * @return  None.
 */
void app_gap_evt_cb(gap_event_t *p_event)
{
    switch(p_event->type)
    {
        case GAP_EVT_ADV_END:
        {
            co_printf("adv_end,status:0x%02x\r\n",p_event->param.adv_end.status);
            
        }
        break;
        
        case GAP_EVT_ALL_SVC_ADDED:
        {
            co_printf("All service added\r\n");
            sp_start_adv();
        }
        break;

        case GAP_EVT_SLAVE_CONNECT:
        {
            co_printf("slave[%d],connect. link_num:%d\r\n",p_event->param.slave_connect.conidx,gap_get_connect_num());
            os_timer_start(&update_param_timer,4000,0);
            //gap_security_req(p_event->param.slave_connect.conidx);
        }
        break;

        case GAP_EVT_DISCONNECT:
        {
            co_printf("Link[%d] disconnect,reason:0x%02X\r\n",p_event->param.disconnect.conidx
                      ,p_event->param.disconnect.reason);
            os_timer_stop(&update_param_timer);
            gap_start_advertising(0);
        }
        break;

        case GAP_EVT_LINK_PARAM_REJECT:
            co_printf("Link[%d]param reject,status:0x%02x\r\n"
                      ,p_event->param.link_reject.conidx,p_event->param.link_reject.status);
            break;

        case GAP_EVT_LINK_PARAM_UPDATE:
            co_printf("Link[%d]param update,interval:%d,latency:%d,timeout:%d\r\n",p_event->param.link_update.conidx
                      ,p_event->param.link_update.con_interval,p_event->param.link_update.con_latency,p_event->param.link_update.sup_to);
            break;

        case GAP_EVT_PEER_FEATURE:
            co_printf("peer[%d] feats ind\r\n",p_event->param.peer_feature.conidx);
            break;

        case GAP_EVT_MTU:
            co_printf("mtu update,conidx=%d,mtu=%d\r\n"
                      ,p_event->param.mtu.conidx,p_event->param.mtu.value);
            break;
        
        case GAP_EVT_LINK_RSSI:
            co_printf("link rssi %d\r\n",p_event->param.link_rssi);
            break;
                
        case GAP_SEC_EVT_SLAVE_ENCRYPT:
            co_printf("slave[%d]_encrypted\r\n",p_event->param.slave_encrypt_conidx);
            os_timer_start(&update_param_timer,4000,0);
            break;

        default:
            break;
    }
}

/*********************************************************************
 * @fn      sp_start_adv
 *
 * @brief   Set advertising data & scan response & advertising parameters and start advertising
 *
 * @param   None. 
 *       
 *
 * @return  None.
 */
static void sp_start_adv(void)
{
    // ????????????????????????????
    uint8_t name_len = strlen(g_ble_dev_name);
    scan_rsp_data[0] = name_len + 1;  // ???? = ????(1) + ???????
    scan_rsp_data[1] = GAP_ADVTYPE_LOCAL_NAME_COMPLETE;
    memset(&scan_rsp_data[2], 0, 20);
    memcpy(&scan_rsp_data[2], g_ble_dev_name, name_len);

    // ????????????????????
    uint8_t power_offset = 2 + name_len;
    scan_rsp_data[power_offset]     = 0x02;
    scan_rsp_data[power_offset + 1] = GAP_ADVTYPE_POWER_LEVEL;
    scan_rsp_data[power_offset + 2] = 0;  // 0dBm

    uint8_t scan_rsp_len = power_offset + 3;
    gap_adv_param_t adv_param;
    adv_param.adv_mode = GAP_ADV_MODE_UNDIRECT;
    adv_param.adv_addr_type = GAP_ADDR_TYPE_PUBLIC;
    adv_param.adv_chnl_map = GAP_ADV_CHAN_ALL;
    adv_param.adv_filt_policy = GAP_ADV_ALLOW_SCAN_ANY_CON_ANY;
    adv_param.adv_intv_min = 600;
    adv_param.adv_intv_max = 600;
        
    gap_set_advertising_param(&adv_param);
    
    // Set advertising data & scan response data
    gap_set_advertising_data(adv_data, sizeof(adv_data));
    gap_set_advertising_rsp_data(scan_rsp_data, scan_rsp_len);
    // Start advertising
    co_printf("Start advertising, name=%s, rsp_len=%d\r\n", g_ble_dev_name, scan_rsp_len);
	gap_start_advertising(0);
}


/*********************************************************************
 * @fn      simple_peripheral_init
 *
 * @brief   Initialize simple peripheral profile, BLE related parameters.
 *
 * @param   None. 
 *       
 *
 * @return  None.
 */
void simple_peripheral_init(void)
{
    // ?????????????????Flash?????????????????????ID????
    ble_init_device_name();

    gap_set_dev_name((uint8_t *)g_ble_dev_name, strlen(g_ble_dev_name) + 1);
    os_timer_init(&update_param_timer, param_timer_func, NULL);
    gap_set_dev_appearance(GAP_APPEARE_GENERIC_RC);

    gap_security_param_t param =
    {
        .mitm = false,
        .ble_secure_conn = false,
        .io_cap = GAP_IO_CAP_NO_INPUT_NO_OUTPUT,
        .pair_init_mode = GAP_PAIRING_MODE_WAIT_FOR_REQ,
        .bond_auth = true,
        .password = 0,
    };
		// Initialize security related settings.
    gap_security_param_init(&param);
    gap_set_cb_func(app_gap_evt_cb);

		//enable bond manage module, which will record bond key and peer service info into flash. 
		//and read these info from flash when func: "gap_bond_manager_init" executes.
    gap_bond_manager_init(BLE_BONDING_INFO_SAVE_ADDR, BLE_REMOTE_SERVICE_SAVE_ADDR, 8, true);
    //gap_bond_manager_delete_all();
    
    mac_addr_t addr;
    gap_address_get(&addr);
    co_printf("Local BDADDR: 0x%2X%2X%2X%2X%2X%2X\r\n", addr.addr[0], addr.addr[1], addr.addr[2], addr.addr[3], addr.addr[4], addr.addr[5]);
    
    
    /* OTA and ESAIR GATT services are registered in user_entry_after_ble_init (proj_main.c). */
}

/*********************************************************************
 * @fn      ble_update_device_name
 *
 * @brief   ???????BLE??????????????
 *
 * ????: ???? -> ?????????? -> ?????????????? -> ??????
 *
 * @param   new_name  ??????????????
 * @param   name_len  ???????(????null?????)
 * @return  0:??? -1:????????
 */
int ble_update_device_name(const char *new_name, uint8_t name_len)
{
    if (new_name == NULL || name_len == 0 || name_len > BLE_NAME_MAX_LEN) {
        co_printf("ble_update_device_name: invalid param (len=%d)\r\n", name_len);
        return -1;
    }

    // ????????????
    ble_name_status_t status = device_config_validate_name(new_name, name_len);
    if (status != NAME_OK) {
        co_printf("ble_update_device_name: name invalid, status=%d\r\n", status);
        return -2;
    }

    bool save_flash = spi_flash_is_present();
    status = device_config_set_ble_name(new_name, name_len, save_flash);
    if (status != NAME_OK) {
        co_printf("ble_update_device_name: config update failed, status=%d\r\n", status);
        return -3;
    }

    // ?????????????????
    memcpy(g_ble_dev_name, new_name, name_len);
    g_ble_dev_name[name_len] = '\0';

    // ???????
    gap_stop_advertising();

    // ??????????????
    gap_set_dev_name((uint8_t *)g_ble_dev_name, name_len + 1);

    // ?????????????????????????
    // scan_rsp_data???: [len, GAP_ADVTYPE_LOCAL_NAME_COMPLETE, name_bytes..., 0x02, GAP_ADVTYPE_POWER_LEVEL, 0]
    scan_rsp_data[0] = name_len + 1;
    scan_rsp_data[1] = GAP_ADVTYPE_LOCAL_NAME_COMPLETE;
    memset(&scan_rsp_data[2], 0, 20);
    memcpy(&scan_rsp_data[2], new_name, name_len);

    // ????????????????????
    uint8_t power_offset = 2 + name_len;
    scan_rsp_data[power_offset]     = 0x02;
    scan_rsp_data[power_offset + 1] = GAP_ADVTYPE_POWER_LEVEL;
    scan_rsp_data[power_offset + 2] = 0;

    uint8_t scan_rsp_len = power_offset + 3;

    // ????????????????????????
    gap_set_advertising_rsp_data(scan_rsp_data, scan_rsp_len);
    gap_start_advertising(0);

    if (save_flash) {
        co_printf("BLE name updated to \"%s\" (saved to W25Q)\r\n", new_name);
    } else {
        co_printf("BLE name updated to \"%s\" (no W25Q; lost after reboot)\r\n", new_name);
    }

    return 0;
}

const char *ble_get_device_name(void)
{
    return g_ble_dev_name;
}
