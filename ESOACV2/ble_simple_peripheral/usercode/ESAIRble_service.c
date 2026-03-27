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
#include <stdio.h>
#include <string.h>
#include "gap_api.h"
#include "gatt_api.h"
#include "gatt_sig_uuid.h"
#include "sys_utils.h"
#include "ESAIRble_service.h"

#include "frusart.h"
#include "protocol.h"
#include "app_task.h"
/*
 * MACROS (????)
 */

/*
 * CONSTANTS (????????)
 */


// OTA Service UUID: 0xFE00
static const uint8_t ESAIR_svc_uuid[UUID_SIZE_16] = ESAIR_SVC_UUID;
static uint8_t ESAIR_svc_id = 0;

static bool ESAIR_link_ntf_enable = false;

/*
 * TYPEDEFS (???????)
 */

/*
 * GLOBAL VARIABLES (??????)
 */


/*
 * LOCAL VARIABLES (???????)
 */

/*********************************************************************
 * Profile Attributes - Table
 * ?????????attribute?????^
 * ?????attribute?Service ??????^
 * ?????????(characteristic)???????????????????attribute??????
 * 1. ?????????(Characteristic Declaration)
 * 2. ????????(Characteristic value)
 * 3. ???????????(Characteristic description)
 * ?????notification ????indication ???????????????attribute??????????????????????????????????????????????(client characteristic configuration)??
 *
 */

const gatt_attribute_t ESAIR_svc_att_table[ESAIR_ATT_NB] =
{
    // Update Over The AIR Service Declaration
    [ESAIR_ATT_IDX_SERVICE] = { { UUID_SIZE_2, UUID16_ARR(GATT_PRIMARY_SERVICE_UUID) },
        GATT_PROP_READ, UUID_SIZE_16, (uint8_t *)ESAIR_svc_uuid
    },

    // OTA Information Characteristic Declaration
    [ESAIR_ATT_IDX_CHAR_DECLARATION_VERSION_INFO] = { { UUID_SIZE_2, UUID16_ARR(GATT_CHARACTER_UUID) },
        GATT_PROP_READ, 0, NULL
    },
    [ESAIR_ATT_IDX_CHAR_VALUE_VERSION_INFO]= { { UUID_SIZE_16, ESAIR_CHAR_UUID_VERSION_INFO },
        GATT_PROP_READ, sizeof(uint16_t), NULL
    },

    // Notify Characteristic Declaration
    [ESAIR_ATT_IDX_CHAR_DECLARATION_NOTI] = { { UUID_SIZE_2, UUID16_ARR(GATT_CHARACTER_UUID) },
        GATT_PROP_READ,0, NULL
    },
    [ESAIR_ATT_IDX_CHAR_VALUE_NOTI] = { { UUID_SIZE_16, ESAIR_CHAR_UUID_NOTI },
        GATT_PROP_READ | GATT_PROP_NOTI, ESAIR_NOTIFY_DATA_SIZE, NULL
    },
    [ESAIR_ATT_IDX_CHAR_CFG_NOTI] = { { UUID_SIZE_2, UUID16_ARR(GATT_CLIENT_CHAR_CFG_UUID) },
        GATT_PROP_READ | GATT_PROP_WRITE, 0,0
    },
    [ESAIR_IDX_CHAR_USER_DESCRIPTION_NOTI]= { { UUID_SIZE_2, UUID16_ARR(GATT_CHAR_USER_DESC_UUID) },
        GATT_PROP_READ, 12, NULL
    },

    // Tx Characteristic Declaration
    [ESAIR_ATT_IDX_CHAR_DECLARATION_TX] = { { UUID_SIZE_2, UUID16_ARR(GATT_CHARACTER_UUID) },
        GATT_PROP_READ, 0, NULL
    },
    [ESAIR_ATT_IDX_CHAR_VALUE_TX] = { { UUID_SIZE_16, ESAIR_CHAR_UUID_TX },
        // TX Characteristic value needs NOTI permission so that gatt_notification() is allowed
        GATT_PROP_READ | GATT_PROP_NOTI, ESAIR_MAX_DATA_SIZE, NULL
    },

    // Rx Characteristic Declaration
    [ESAIR_ATT_IDX_CHAR_DECLARATION_RX] = { { UUID_SIZE_2, UUID16_ARR(GATT_CHARACTER_UUID) },
        GATT_PROP_READ, 0, NULL
    },
    [ESAIR_ATT_IDX_CHAR_VALUE_RX] = { { UUID_SIZE_16, ESAIR_CHAR_UUID_RX },
        GATT_PROP_WRITE, ESAIR_MAX_DATA_SIZE, NULL
    },
};

/*********************************************************************
 * @fn      esair_gatt_read_cb
 *
 * @brief   ESAIR GATT read request handler.
 *			????????????????????????????????
 *
 * @param   p_read  - the pointer to read buffer. NOTE: It's just a pointer from lower layer, please create the buffer in application layer.
 *					  ??????????????? ??????????????????????????????????????. ????????, ???????????.
 *          len     - the pointer to the length of read buffer. Application to assign it.
 *                    ????????????????????????????????.
 *          att_idx - index of the attribute value in it's attribute table.
 *					  Attribute???????.
 *
 * @return  ??????????.
 */
static void esair_gatt_read_cb(uint8_t *p_read, uint16_t *len, uint16_t att_idx,uint8_t conn_idx )
{
    (void)conn_idx;

    if (att_idx == ESAIR_IDX_CHAR_USER_DESCRIPTION_NOTI) {
        memcpy(p_read, "ESAIR Notify", strlen("ESAIR Notify"));
        *len = strlen("ESAIR Notify");
    } else if (att_idx == ESAIR_ATT_IDX_CHAR_VALUE_VERSION_INFO) {
        p_read[0] = 0x00;
        p_read[1] = 0x01;
        *len = 2;
    } else if (att_idx == ESAIR_ATT_IDX_CHAR_VALUE_NOTI) {
        memcpy(p_read, "ntf_enable", strlen("ntf_enable"));
        *len = strlen("ntf_enable");
    } else {
        *len = 0;
    }
}
/*********************************************************************
 * @fn      esair_gatt_write_cb
 *
 * @brief   ESAIR GATT write request handler.
 *			????????????????????????????????
 *
 * @param   write_buf   - the buffer for write
 *			              ????????????.
 *					  
 *          len         - the length of write buffer.
 *                        ?????????????.
 *          att_idx     - index of the attribute value in it's attribute table.
 *					      Attribute???????.
 *
 * @return  ??????????.
 */
static void esair_gatt_write_cb(uint8_t *write_buf, uint16_t len, uint16_t att_idx,uint8_t conn_idx)
	{
		co_printf("ESAIR BLE RX: len=%d\r\n", len);

		// CCC write (Client Characteristic Configuration Descriptor) controls notification enable
		// CCC value: 0x0001 => notifications enabled, 0x0000 => disabled
		if (att_idx == ESAIR_ATT_IDX_CHAR_CFG_NOTI) {
			if (len >= 2) {
				uint16_t ccc = (uint16_t)write_buf[0] | ((uint16_t)write_buf[1] << 8);
				ESAIR_link_ntf_enable = (ccc == 0x0001);
				co_printf("ESAIR CCC notify %s\r\n", ESAIR_link_ntf_enable ? "EN" : "DIS");
			}
			return;
		}

		// ????????????????
		app_task_send_event(APP_EVT_BLE_DATA_RECEIVED, write_buf, len);
	}
/*********************************************************************
 * @fn      ESAIR_gatt_msg_handler
 *
 * @brief   ESAIR GATT message callback; read/write handled here.
 *
 * @param   p_msg       - GATT messages from GATT layer.
 *
 * @return  uint16_t    - Length of handled message.
 */
static uint16_t ESAIR_gatt_msg_handler(gatt_msg_t *p_msg)
{
    switch(p_msg->msg_evt)
    {
        case GATTC_MSG_READ_REQ:
            esair_gatt_read_cb((uint8_t *)(p_msg->param.msg.p_msg_data), &(p_msg->param.msg.msg_len), p_msg->att_idx,p_msg->conn_idx );
            return p_msg->param.msg.msg_len;
        
        case GATTC_MSG_WRITE_REQ:
            esair_gatt_write_cb((uint8_t*)(p_msg->param.msg.p_msg_data), (p_msg->param.msg.msg_len), p_msg->att_idx,p_msg->conn_idx);
            return 0;
            
        default:
            break;
    }
    /* For non READ/WRITE events, don't read msg union fields. */
    return 0;
}

/*********************************************************************
 * @fn      ESAIR_gatt_report_notify
 *
 * @brief   Send application data to the central via ESAIR TX notification.
 *
 * @param   conidx  Connection index.
 * @param   p_data  Payload.
 * @param   len     Payload length.
 *
 * @return  none.
 */
void ESAIR_gatt_report_notify(uint8_t conidx, uint8_t *p_data, uint16_t len)
{
    if (ESAIR_link_ntf_enable)
    {
        gatt_ntf_t ntf;
        ntf.conidx = conidx;
        ntf.svc_id = ESAIR_svc_id;
        ntf.att_idx = ESAIR_ATT_IDX_CHAR_VALUE_TX;  // ???TX???????????
        ntf.data_len = len;
        ntf.p_data = p_data;
        gatt_notification(ntf);
        co_printf("ESAIR BLE Notify sent, len=%d\r\n", len);
    }
}

/*********************************************************************
 * @fn      ESAIR_gatt_add_service
 *
 * @brief   Register ESAIR GATT service and attribute table.
 */
void ESAIR_gatt_add_service(void)
{
    gatt_service_t esair_profile_svc;
    esair_profile_svc.p_att_tb = ESAIR_svc_att_table;
    esair_profile_svc.att_nb = ESAIR_ATT_NB;
    esair_profile_svc.gatt_msg_handler = ESAIR_gatt_msg_handler;

    ESAIR_svc_id = gatt_add_service(&esair_profile_svc);
}
