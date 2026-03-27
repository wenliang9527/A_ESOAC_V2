/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include <string.h>

#include "gap_api.h"
#include "gatt_api.h"

#include "os_timer.h"
#include "os_mem.h"
#include "sys_utils.h"
#include "jump_table.h"

#include "user_task.h"
#include "app_task.h"

#include "driver_plf.h"
#include "driver_system.h"
#include "driver_pmu.h"
#include "driver_uart.h"
#include "driver_flash.h"
#include "driver_efuse.h"
#include "flash_usage_config.h"

#include "ble_simple_peripheral.h"
#include "frIRConversion.h"
#include "aircondata.h"
#include "LED.h"
#include "frusart.h"
#include "frADC.h"
#include "frspi.h"
#include "ota.h"
#include "ota_service.h"
#include "ESAIRble_service.h"
#include "frATcode.h"
#include "device_config.h"

/*-------------------------------------------*/

/* Stack may call user_entry_after_ble_init more than once; repeating task/GATT init corrupts RAM. */
static uint8_t s_user_after_ble_done;

const struct jump_table_version_t _jump_table_version __attribute__((section("jump_table_3"))) = 
{
    .firmware_version = 0x00000000,
};

const struct jump_table_image_t _jump_table_image __attribute__((section("jump_table_1"))) =
{
    .image_type = IMAGE_TYPE_APP,
    .image_size = 0x20000,
};

/* PMU GPIO ??????????????? GPIO ?? */
__attribute__((section("ram_code"))) void pmu_gpio_isr_ram(void)
{
    uint32_t gpio_value = ool_read32(PMU_REG_GPIOA_V);
    
    ool_write32(PMU_REG_PORTA_LAST, gpio_value);
}

/*********************************************************************
 * @fn      user_custom_parameters
 * @brief   ?????????????????????????
 *          ?????????????ID?????????????????????
 *********************************************************************/
void user_custom_parameters(void)
{
    struct chip_unique_id_t id_data;

    efuse_get_chip_unique_id(&id_data);

    /* ??????????ID???????????? */
    memcpy(ESAirdata.MUConlyID, id_data.unique_id, sizeof(id_data.unique_id));

    /* ???????ID?????????????????2???????1?? */
    id_data.unique_id[5] |= 0xC0;
    memcpy(__jump_table.addr.addr, id_data.unique_id, 6);

    __jump_table.system_clk = SYSTEM_SYS_CLK_48M;
    jump_table_set_static_keys_store_offset(JUMP_TABLE_STATIC_KEY_OFFSET);
    ble_set_addr_type(BLE_ADDR_TYPE_PUBLIC);
    retry_handshake();
}

/*********************************************************************
 * @fn      user_entry_before_sleep_imp
 * @brief   ????????????????????? RAM ??
 * @details ???????? UART1 ???? 's' ????????
 *********************************************************************/
__attribute__((section("ram_code"))) void user_entry_before_sleep_imp(void)
{
	uart_putc_noint_no_wait(UART1, 's');
}

/*********************************************************************
 * @fn      user_entry_after_sleep_imp
 * @brief   ???????????????????????????????????
 * @details ??? UART1 ?????????? UART1 ???? 'w' ????????
 *********************************************************************/
__attribute__((section("ram_code"))) void user_entry_after_sleep_imp(void)
{
    /* ??? UART1 ??????? */
    system_set_port_pull(GPIO_PA2, true);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_2, PORTA2_FUNC_UART1_RXD);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_3, PORTA3_FUNC_UART1_TXD);

    uart_init(UART1, BAUD_RATE_115200);
    uart_putc_noint_no_wait(UART1, 'w');

    NVIC_EnableIRQ(PMU_IRQn);
}

/*********************************************************************
 * @fn      user_entry_before_ble_init
 * @brief   BLE ???????????
 * @details ??????????PMU ?????SPI Flash ?? UART1
 *********************************************************************/
void user_entry_before_ble_init(void)
{    
    pmu_set_sys_power_mode(PMU_SYS_POW_BUCK);
#ifdef FLASH_PROTECT
    flash_protect_enable(1);
#endif
    pmu_enable_irq(PMU_ISR_BIT_ACOK
                   | PMU_ISR_BIT_ACOFF
                   | PMU_ISR_BIT_ONKEY_PO
                   | PMU_ISR_BIT_OTP
                   | PMU_ISR_BIT_LVD
                   | PMU_ISR_BIT_BAT
                   | PMU_ISR_BIT_ONKEY_HIGH);
    NVIC_EnableIRQ(PMU_IRQn);
    
    /* SPI Flash ???????device_config ?? mqtt_config ???? Flash ????? */
    fr_spi_flash();

    /* ?? SPI Flash ????????????????????? ESOAAIR_Savekey ???? */
    ESOAAIR_readkey();

    /* UART1 ????????????????????????? */
    fruart1_init();
}

/*********************************************************************
 * @fn      user_entry_after_ble_init
 * @brief   BLE ?????????????????????????????????
 * @details ???????
 *          1. ???????????????? Flash ????
 *          2. ??????????LED/ADC/UART/RTC??
 *          3. BLE ????? GATT ???????
 *          4. MQTT ????????????????
 *          5. ML307 MQTT??????????????????? BLE ??? GATT ?????????
 *********************************************************************/
void user_entry_after_ble_init(void)
{
    if (s_user_after_ble_done) {
        return;
    }
    s_user_after_ble_done = 1;

    co_printf("BLE Peripheral\r\n");

    system_sleep_disable();

    /* task, protocol, device_config only (no MQTT until BLE is up) */
    app_task_init_early();

    /* ???????? */
    LED_INIT();
    fr_ADC_star();
    /* ML307 AT �� UART0�����ڶ�ʱ��Ͷ���շ�ǰ��������벨���ʳ�ʼ�� */
    fruart0_init();
    fros_uarttmr_INIT();
    FR_rtcinit();

    simple_peripheral_init();

    ota_gatt_add_service();
    ESAIR_gatt_add_service();

    /* mqtt_config_load, mqtt_handler, periodic timers */
    app_task_init_mqtt_timers();

    /* Start MQTT connection in non-blocking mode to avoid blocking BLE stack callback path. */
    app_task_start_reconnect_timer();
}
