#ifndef ESAIRBLE_SERVICE_H
#define ESAIRBLE_SERVICE_H


/*
 * INCLUDES (????????)
 */
#include <stdio.h>
#include <string.h>
/*
 * MACROS (????)
 */
#define ESAIR_SVC_UUID                {0x10, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x02}

#define ESAIR_CHAR_UUID_TX            {0x10, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x02}
#define ESAIR_CHAR_UUID_RX            {0x11, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x02}
#define ESAIR_CHAR_UUID_NOTI          {0x12, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x02}
#define ESAIR_CHAR_UUID_VERSION_INFO  {0x13, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x02}
//    
#define ESAIR_MAX_DATA_SIZE              128
#define ESAIR_NOTIFY_DATA_SIZE           20

/*
 * CONSTANTS (????????)
 */
enum
{
    ESAIR_ATT_IDX_SERVICE,

    ESAIR_ATT_IDX_CHAR_DECLARATION_VERSION_INFO,
    ESAIR_ATT_IDX_CHAR_VALUE_VERSION_INFO,

    ESAIR_ATT_IDX_CHAR_DECLARATION_NOTI,
    ESAIR_ATT_IDX_CHAR_VALUE_NOTI,
    ESAIR_ATT_IDX_CHAR_CFG_NOTI,
    ESAIR_IDX_CHAR_USER_DESCRIPTION_NOTI,

    ESAIR_ATT_IDX_CHAR_DECLARATION_TX,
    ESAIR_ATT_IDX_CHAR_VALUE_TX,

    ESAIR_ATT_IDX_CHAR_DECLARATION_RX,
    ESAIR_ATT_IDX_CHAR_VALUE_RX,

    ESAIR_ATT_NB,
};

/*
 * TYPEDEFS (???????)
 */

/*
 * GLOBAL VARIABLES (??????)
 */

/*
 * LOCAL VARIABLES (???????)
 */


/*
 * PUBLIC FUNCTIONS (??????)
 */
/*********************************************************************
 * @fn      ESAIR_gatt_add_service
 *
 * @brief   Register the ESAIR custom GATT service with the stack.
 */
void ESAIR_gatt_add_service(void);

/*********************************************************************
 * @fn      ESAIR_gatt_report_notify
 *
 * @brief   Notify the connected central on the ESAIR TX characteristic.
 *
 * @param   conidx  Connection index.
 * @param   p_data  Payload buffer.
 * @param   len     Payload length in bytes.
 */
void ESAIR_gatt_report_notify(uint8_t conidx, uint8_t *p_data, uint16_t len);
















#endif


