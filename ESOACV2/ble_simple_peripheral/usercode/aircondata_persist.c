/* ESAirdata 持久化管理实现 */

#include "aircondata_persist.h"
#include "os_timer.h"
#include "co_printf.h"
#include <string.h>

/* 延迟保存定时器 */
static os_timer_t esairdata_save_timer;
static bool save_timer_initialized = false;

/* 校验和计算 - 简单累加和 */
uint32_t ESAirdata_CalcChecksum(ESAirdata_Persist_t *data)
{
    uint32_t checksum = 0;
    uint8_t *ptr = (uint8_t *)data;
    uint16_t len = sizeof(ESAirdata_Persist_t) - sizeof(uint32_t);
    
    for (uint16_t i = 0; i < len; i++) {
        checksum += ptr[i];
    }
    
    return checksum;
}

/* 参数范围校验 */
bool ESAirdata_ValidateRange(ESAirdata_Persist_t *data)
{
    /* 校验温度范围 */
    if (data->air_temperature < TEMP_MIN || data->air_temperature > TEMP_MAX) {
        co_printf("ESAirdata_ValidateRange: Temperature out of range: %d\r\n", 
                  data->air_temperature);
        return false;
    }
    
    /* 校验风速范围 */
    if (data->air_windspeed > WIND_MAX) {
        co_printf("ESAirdata_ValidateRange: Wind speed out of range: %d\r\n", 
                  data->air_windspeed);
        return false;
    }
    
    /* 校验模式范围 */
    if (data->air_mode > MODE_MAX) {
        co_printf("ESAirdata_ValidateRange: Mode out of range: %d\r\n", 
                  data->air_mode);
        return false;
    }
    
    /* 校验开关状态 */
    if (data->air_status > 1) {
        co_printf("ESAirdata_ValidateRange: Status out of range: %d\r\n", 
                  data->air_status);
        return false;
    }
    
    return true;
}

/* 同步ESAirdata到持久化结构 */
void ESAirdata_SyncToPersist(ESAirdata_Persist_t *persist)
{
    persist->magic = ESAIRDATA_MAGIC;
    persist->version = ESAIRDATA_VERSION;
    persist->reserved = 0;
    
    persist->air_status = (ESAirdata.AIRStatus == airSW_ON) ? 1 : 0;
    persist->air_mode = ESAirdata.AIRMODE;
    persist->air_temperature = ESAirdata.AIRTemperature;
    persist->air_windspeed = ESAirdata.AIRWindspeed;
    
    /* 计算校验和 */
    persist->checksum = ESAirdata_CalcChecksum(persist);
}

/* 从持久化结构同步到ESAirdata */
void ESAirdata_SyncFromPersist(ESAirdata_Persist_t *persist)
{
    ESAirdata.AIRStatus = persist->air_status ? airSW_ON : airSW_OFF;
    ESAirdata.AIRMODE = persist->air_mode;
    ESAirdata.AIRTemperature = persist->air_temperature;
    ESAirdata.AIRWindspeed = persist->air_windspeed;
}

/* 设置默认值 */
void ESAirdata_SetDefault(void)
{
    co_printf("ESAirdata_SetDefault: Setting default values\r\n");
    
    ESAirdata.AIRStatus = airSW_OFF;
    ESAirdata.AIRMODE = airmode_cold;
    ESAirdata.AIRTemperature = TEMP_DEFAULT;
    ESAirdata.AIRWindspeed = WIND_DEFAULT;
    
    /* 立即保存默认值到Flash */
    ESAirdata_Save();
}

/* 保存到Flash */
void ESAirdata_Save(void)
{
    ESAirdata_Persist_t persist_data;
    
    /* 同步当前状态到持久化结构 */
    ESAirdata_SyncToPersist(&persist_data);
    
    /* 擦除Sector */
    uint32_t sector = W25QADDR_ESAIRDATA / SPIF_SECTOR_SIZE;
    SpiFlash_Erase_Sector(sector);
    
    /* 写入Flash */
    SpiFlash_Write((uint8_t *)&persist_data, W25QADDR_ESAIRDATA, sizeof(persist_data));
    
    co_printf("ESAirdata_Save: Saved (temp=%d, mode=%d, wind=%d, status=%s)\r\n",
              persist_data.air_temperature,
              persist_data.air_mode,
              persist_data.air_windspeed,
              persist_data.air_status ? "ON" : "OFF");
}

/* 从Flash加载 */
bool ESAirdata_Load(void)
{
    ESAirdata_Persist_t persist_data;
    
    /* 从Flash读取 */
    SpiFlash_Read((uint8_t *)&persist_data, W25QADDR_ESAIRDATA, sizeof(persist_data));
    
    /* 检查魔法数 */
    if (persist_data.magic != ESAIRDATA_MAGIC) {
        co_printf("ESAirdata_Load: Magic invalid (0x%08X)\r\n", persist_data.magic);
        return false;
    }
    
    /* 检查版本号 */
    if (persist_data.version != ESAIRDATA_VERSION) {
        co_printf("ESAirdata_Load: Version mismatch (exp=%d, got=%d)\r\n",
                  ESAIRDATA_VERSION, persist_data.version);
        return false;
    }
    
    /* 校验校验和 */
    uint32_t calc_checksum = ESAirdata_CalcChecksum(&persist_data);
    if (calc_checksum != persist_data.checksum) {
        co_printf("ESAirdata_Load: Checksum error (calc=0x%08X, stored=0x%08X)\r\n",
                  calc_checksum, persist_data.checksum);
        return false;
    }
    
    /* 参数范围校验 */
    if (!ESAirdata_ValidateRange(&persist_data)) {
        co_printf("ESAirdata_Load: Range validation failed\r\n");
        return false;
    }
    
    /* 同步到ESAirdata */
    ESAirdata_SyncFromPersist(&persist_data);
    
    co_printf("ESAirdata_Load: Loaded (temp=%d, mode=%d, wind=%d, status=%s)\r\n",
              persist_data.air_temperature,
              persist_data.air_mode,
              persist_data.air_windspeed,
              persist_data.air_status ? "ON" : "OFF");
    
    return true;
}

/* 延迟保存定时器回调 */
static void esairdata_save_timer_cb(void *arg)
{
    (void)arg;
    ESAirdata_Save();
}

/* 初始化延迟保存定时器 */
static void esairdata_init_save_timer(void)
{
    if (!save_timer_initialized) {
        os_timer_init(&esairdata_save_timer, esairdata_save_timer_cb, NULL);
        save_timer_initialized = true;
    }
}

/* 触发延迟保存（5秒后保存） */
void ESAirdata_TriggerSave(void)
{
    esairdata_init_save_timer();
    
    /* 如果定时器已在运行，先停止再启动（实现延迟重置） */
    os_timer_stop(&esairdata_save_timer);
    os_timer_start(&esairdata_save_timer, 5000, 0);
    
    co_printf("ESAirdata_TriggerSave: Save in 5s\r\n");
}

/* 取消延迟保存 */
void ESAirdata_CancelSave(void)
{
    if (save_timer_initialized) {
        os_timer_stop(&esairdata_save_timer);
    }
}
