/* Copyright (c) 2019, Freqchip. All rights reserved. */
/* ESAIR GATT 表与读写回调：RX 投递应用任务，TX Notify 上报 */

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

/* 主服务 UUID 0xFE00 */
static const uint8_t ESAIR_svc_uuid[UUID_SIZE_16] = ESAIR_SVC_UUID;
static uint8_t ESAIR_svc_id = 0;

static bool ESAIR_link_ntf_enable = false;

/*
 * 属性表：服务声明、版本特征、Notify（含 CCC）、TX/RX 数据通道。
 * Notify 需客户端写 CCC(0x2902) 后才可 gatt_notification。
 */
const gatt_attribute_t ESAIR_svc_att_table[ESAIR_ATT_NB] =
{
    [ESAIR_ATT_IDX_SERVICE] = { { UUID_SIZE_2, UUID16_ARR(GATT_PRIMARY_SERVICE_UUID) },
        GATT_PROP_READ, UUID_SIZE_16, (uint8_t *)ESAIR_svc_uuid
    },

    [ESAIR_ATT_IDX_CHAR_DECLARATION_VERSION_INFO] = { { UUID_SIZE_2, UUID16_ARR(GATT_CHARACTER_UUID) },
        GATT_PROP_READ, 0, NULL
    },
    [ESAIR_ATT_IDX_CHAR_VALUE_VERSION_INFO]= { { UUID_SIZE_16, ESAIR_CHAR_UUID_VERSION_INFO },
        GATT_PROP_READ, sizeof(uint16_t), NULL
    },

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

    [ESAIR_ATT_IDX_CHAR_DECLARATION_TX] = { { UUID_SIZE_2, UUID16_ARR(GATT_CHARACTER_UUID) },
        GATT_PROP_READ, 0, NULL
    },
    [ESAIR_ATT_IDX_CHAR_VALUE_TX] = { { UUID_SIZE_16, ESAIR_CHAR_UUID_TX },
        GATT_PROP_READ | GATT_PROP_NOTI, ESAIR_MAX_DATA_SIZE, NULL
    },

    [ESAIR_ATT_IDX_CHAR_DECLARATION_RX] = { { UUID_SIZE_2, UUID16_ARR(GATT_CHARACTER_UUID) },
        GATT_PROP_READ, 0, NULL
    },
    [ESAIR_ATT_IDX_CHAR_VALUE_RX] = { { UUID_SIZE_16, ESAIR_CHAR_UUID_RX },
        GATT_PROP_WRITE, ESAIR_MAX_DATA_SIZE, NULL
    },
};

/* GATT 读：用户描述 / 版本号 / Notify 占位 */
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

/* GATT 写：CCC 开关 Notify；RX 负载投递应用任务 */
static void esair_gatt_write_cb(uint8_t *write_buf, uint16_t len, uint16_t att_idx,uint8_t conn_idx)
{
    (void)conn_idx;
    co_printf("ESAIR BLE RX: len=%d\r\n", len);

    if (att_idx == ESAIR_ATT_IDX_CHAR_CFG_NOTI) {
        if (len >= 2) {
            uint16_t ccc = (uint16_t)write_buf[0] | ((uint16_t)write_buf[1] << 8);
            ESAIR_link_ntf_enable = (ccc == 0x0001);
            co_printf("ESAIR CCC notify %s\r\n", ESAIR_link_ntf_enable ? "EN" : "DIS");
        }
        return;
    }

    app_task_send_event(APP_EVT_BLE_DATA_RECEIVED, write_buf, len);
}

/* 仅处理 READ/WRITE；其它事件勿读 union */
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
    return 0;
}

void ESAIR_gatt_report_notify(uint8_t conidx, uint8_t *p_data, uint16_t len)
{
    if (ESAIR_link_ntf_enable)
    {
        gatt_ntf_t ntf;
        ntf.conidx = conidx;
        ntf.svc_id = ESAIR_svc_id;
        ntf.att_idx = ESAIR_ATT_IDX_CHAR_VALUE_TX;
        ntf.data_len = len;
        ntf.p_data = p_data;
        gatt_notification(ntf);
        co_printf("ESAIR BLE Notify sent, len=%d\r\n", len);
    }
}

void ESAIR_gatt_add_service(void)
{
    gatt_service_t esair_profile_svc;
    esair_profile_svc.p_att_tb = ESAIR_svc_att_table;
    esair_profile_svc.att_nb = ESAIR_ATT_NB;
    esair_profile_svc.gatt_msg_handler = ESAIR_gatt_msg_handler;

    ESAIR_svc_id = gatt_add_service(&esair_profile_svc);
}
