/* 设备配置：BLE 名等写 W25Q */

#include "device_config.h"
#include "frspi.h"
#include <string.h>
#include "co_printf.h"
#include "frATcode.h"

/* RAM 副本；ble_name_len==0 表示用默认 ESOAC+地址 */
static device_config_t g_device_config;

/* 整扇区擦除后写入并回读校验 */
static bool device_config_write_flash(void)
{
    if (!spi_flash_is_present()) {
        co_printf("ERR: device_config write skipped (no W25Q)\r\n");
        return false;
    }

    uint32_t sector_addr = DEVICE_CONFIG_FLASH_ADDR / SPIF_SECTOR_SIZE;
    SpiFlash_Erase_Sector(sector_addr);

    SpiFlash_Write((uint8_t *)&g_device_config, DEVICE_CONFIG_FLASH_ADDR,
                   sizeof(device_config_t));

    // 回读验证
    device_config_t verify;
    SpiFlash_Read((uint8_t *)&verify, DEVICE_CONFIG_FLASH_ADDR,
                  sizeof(device_config_t));

    if (memcmp(&verify, &g_device_config, sizeof(device_config_t)) != 0) {
        co_printf("ERR: device_config verify failed\r\n");
        return false;
    }

    return true;
}

void device_config_load(void)
{
    if (!spi_flash_is_present()) {
        co_printf("device_config: W25Q absent, no stored BLE name\r\n");
        memset(&g_device_config, 0, sizeof(g_device_config));
        return;
    }

    SpiFlash_Read((uint8_t *)&g_device_config, DEVICE_CONFIG_FLASH_ADDR,
                  sizeof(device_config_t));

    if (g_device_config.magic != DEVICE_CONFIG_MAGIC) {
        co_printf("device_config: no valid config in flash, init empty custom name\r\n");
        memset(&g_device_config, 0, sizeof(g_device_config));
        g_device_config.magic = DEVICE_CONFIG_MAGIC;
        g_device_config.ble_name[0] = '\0';
        g_device_config.ble_name_len = 0;
        device_config_write_flash();
        return;
    }

    if (g_device_config.ble_name_len > BLE_NAME_MAX_LEN) {
        co_printf("device_config: corrupt len, reset empty name\r\n");
        memset(&g_device_config, 0, sizeof(g_device_config));
        g_device_config.magic = DEVICE_CONFIG_MAGIC;
        g_device_config.ble_name[0] = '\0';
        g_device_config.ble_name_len = 0;
        device_config_write_flash();
        return;
    }

    if (g_device_config.ble_name_len > 0 &&
        g_device_config.ble_name[g_device_config.ble_name_len] != '\0') {
        co_printf("device_config: corrupt name terminator, reset\r\n");
        memset(&g_device_config, 0, sizeof(g_device_config));
        g_device_config.magic = DEVICE_CONFIG_MAGIC;
        g_device_config.ble_name[0] = '\0';
        g_device_config.ble_name_len = 0;
        device_config_write_flash();
        return;
    }

    /* 旧固件曾把占位符 "ESOAC" 写入 Flash，导致无法生成 ESOAC+地址 */
    if (g_device_config.ble_name_len == 5 &&
        memcmp(g_device_config.ble_name, "ESOAC", 5) == 0) {
        co_printf("device_config: migrate legacy ESOAC placeholder\r\n");
        g_device_config.ble_name[0] = '\0';
        g_device_config.ble_name_len = 0;
        device_config_write_flash();
        return;
    }

    if (g_device_config.ble_name_len > 0) {
        co_printf("device_config: loaded, ble_name=\"%s\", len=%d\r\n",
                  g_device_config.ble_name, g_device_config.ble_name_len);
    } else {
        co_printf("device_config: loaded, no custom name (use hardware default)\r\n");
    }
}

/**
 * @brief 保存设备配置到SPI Flash
 */
bool device_config_save(void)
{
    return device_config_write_flash();
}

/**
 * @brief 校验设备名称合法性
 * @param name 名称字符串
 * @param len  名称长度
 * @return 校验结果
 */
ble_name_status_t device_config_validate_name(const char *name, uint8_t len)
{
    // 空指针检查
    if (name == NULL) {
        return NAME_ERR_EMPTY;
    }

    // 长度为0
    if (len == 0) {
        return NAME_ERR_EMPTY;
    }

    // 超长检查
    if (len > BLE_NAME_MAX_LEN) {
        return NAME_ERR_TOO_LONG;
    }

    // 逐字符校验：只允许可打印ASCII (0x20 ~ 0x7E)
    for (uint8_t i = 0; i < len; i++) {
        if (name[i] < 0x20 || name[i] > 0x7E) {
            return NAME_ERR_INVALID_CHAR;
        }
    }

    // 名称不能全为空格
    bool all_space = true;
    for (uint8_t i = 0; i < len; i++) {
        if (name[i] != ' ') {
            all_space = false;
            break;
        }
    }
    if (all_space) {
        return NAME_ERR_EMPTY;
    }

    return NAME_OK;
}

/**
 * @brief 设置BLE设备名称
 */
ble_name_status_t device_config_set_ble_name(const char *name, uint8_t len, bool save)
{
    // 参数校验
    ble_name_status_t status = device_config_validate_name(name, len);
    if (status != NAME_OK) {
        return status;
    }

    g_device_config.magic = DEVICE_CONFIG_MAGIC;
    memcpy(g_device_config.ble_name, name, len);
    g_device_config.ble_name[len] = '\0';
    g_device_config.ble_name_len = len;

    if (save) {
        if (!device_config_write_flash()) {
            return NAME_ERR_FLASH_FAIL;
        }
    }

    co_printf("device_config: ble_name updated to \"%s\"%s\r\n",
              g_device_config.ble_name, save ? " (flash)" : " (RAM only)");
    return NAME_OK;
}

/**
 * @brief 获取当前BLE设备名称
 */
const char *device_config_get_ble_name(void)
{
    return (const char *)g_device_config.ble_name;
}

/**
 * @brief 获取当前BLE设备名称长度
 */
uint8_t device_config_get_ble_name_len(void)
{
    return g_device_config.ble_name_len;
}

/**
 * @brief 恢复默认设备名称
 */
void device_config_reset_default(bool save)
{
    memset(&g_device_config, 0, sizeof(g_device_config));
    g_device_config.magic = DEVICE_CONFIG_MAGIC;
    g_device_config.ble_name[0] = '\0';
    g_device_config.ble_name_len = 0;

    if (save && spi_flash_is_present()) {
        device_config_write_flash();
    }

    co_printf("device_config: cleared custom name (hardware default on next BLE init)\r\n");
}

/* ============================================================================
 *                      MQTT配置持久化实现
 * ============================================================================ */

bool mqtt_config_load(void)
{
    mqtt_config_storage_t stored;

    SpiFlash_Read((uint8_t *)&stored, MQTT_CONFIG_FLASH_ADDR,
                  sizeof(mqtt_config_storage_t));

    // 校验魔数
    if (stored.magic != MQTT_CONFIG_MAGIC) {
        co_printf("mqtt_config: no valid config in flash\r\n");
        return false;
    }

    // 逐字段校验：确保每个字符串以null结尾
    stored.server_addr[MQTT_ADDR_MAX_LEN - 1] = '\0';
    stored.server_port[MQTT_PORT_MAX_LEN - 1] = '\0';
    stored.client_id[MQTT_CLIENT_ID_MAX_LEN - 1] = '\0';
    stored.username[MQTT_USER_MAX_LEN - 1] = '\0';
    stored.password[MQTT_PASS_MAX_LEN - 1] = '\0';
    stored.subscribe_topic[MQTT_TOPIC_MAX_LEN - 1] = '\0';
    stored.publish_topic[MQTT_TOPIC_MAX_LEN - 1] = '\0';

    // 拷贝到全局配置
    strncpy(g_mqtt_config.server_addr, stored.server_addr, sizeof(g_mqtt_config.server_addr));
    strncpy(g_mqtt_config.server_port, stored.server_port, sizeof(g_mqtt_config.server_port));
    strncpy(g_mqtt_config.client_id, stored.client_id, sizeof(g_mqtt_config.client_id));
    strncpy(g_mqtt_config.username, stored.username, sizeof(g_mqtt_config.username));
    strncpy(g_mqtt_config.password, stored.password, sizeof(g_mqtt_config.password));
    strncpy(g_mqtt_config.subscribe_topic, stored.subscribe_topic, sizeof(g_mqtt_config.subscribe_topic));
    strncpy(g_mqtt_config.publish_topic, stored.publish_topic, sizeof(g_mqtt_config.publish_topic));

    co_printf("mqtt_config: loaded from flash\r\n");
    co_printf("  addr=%s, port=%s\r\n", g_mqtt_config.server_addr, g_mqtt_config.server_port);
    co_printf("  sub=%s, pub=%s\r\n", g_mqtt_config.subscribe_topic, g_mqtt_config.publish_topic);

    return true;
}

bool mqtt_config_save(void)
{
    mqtt_config_storage_t stored;

    memset(&stored, 0, sizeof(stored));
    stored.magic = MQTT_CONFIG_MAGIC;

    strncpy(stored.server_addr, g_mqtt_config.server_addr, sizeof(stored.server_addr) - 1);
    strncpy(stored.server_port, g_mqtt_config.server_port, sizeof(stored.server_port) - 1);
    strncpy(stored.client_id, g_mqtt_config.client_id, sizeof(stored.client_id) - 1);
    strncpy(stored.username, g_mqtt_config.username, sizeof(stored.username) - 1);
    strncpy(stored.password, g_mqtt_config.password, sizeof(stored.password) - 1);
    strncpy(stored.subscribe_topic, g_mqtt_config.subscribe_topic, sizeof(stored.subscribe_topic) - 1);
    strncpy(stored.publish_topic, g_mqtt_config.publish_topic, sizeof(stored.publish_topic) - 1);

    // 擦除扇区
    uint32_t sector_addr = MQTT_CONFIG_FLASH_ADDR / SPIF_SECTOR_SIZE;
    SpiFlash_Erase_Sector(sector_addr);

    // 写入
    SpiFlash_Write((uint8_t *)&stored, MQTT_CONFIG_FLASH_ADDR,
                   sizeof(mqtt_config_storage_t));

    // 回读验证
    mqtt_config_storage_t verify;
    SpiFlash_Read((uint8_t *)&verify, MQTT_CONFIG_FLASH_ADDR,
                  sizeof(mqtt_config_storage_t));

    if (memcmp(&verify, &stored, sizeof(mqtt_config_storage_t)) != 0) {
        co_printf("ERR: mqtt_config verify failed\r\n");
        return false;
    }

    co_printf("mqtt_config: saved to flash\r\n");
    return true;
}

bool mqtt_config_erase(void)
{
    uint32_t sector_addr = MQTT_CONFIG_FLASH_ADDR / SPIF_SECTOR_SIZE;
    SpiFlash_Erase_Sector(sector_addr);

    // 回读验证已擦除（全0xFF）
    uint8_t verify[4];
    SpiFlash_Read(verify, MQTT_CONFIG_FLASH_ADDR, 4);
    if (verify[0] == 0xFF && verify[1] == 0xFF && verify[2] == 0xFF && verify[3] == 0xFF) {
        co_printf("mqtt_config: erased from flash\r\n");
        return true;
    }

    co_printf("ERR: mqtt_config erase verify failed\r\n");
    return false;
}
