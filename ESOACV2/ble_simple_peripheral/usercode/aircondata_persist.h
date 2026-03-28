/* ESAirdata 持久化管理 - 保存空调状态到W25Q Flash */

#ifndef _AIRCONDATA_PERSIST_H
#define _AIRCONDATA_PERSIST_H

#include <stdint.h>
#include <stdbool.h>
#include "aircondata.h"
#include "frspi.h"

/* 版本号定义 */
#define ESAIRDATA_VERSION_MAJOR 1
#define ESAIRDATA_VERSION_MINOR 0
#define ESAIRDATA_VERSION ((ESAIRDATA_VERSION_MAJOR << 8) | ESAIRDATA_VERSION_MINOR)

/* 魔法数，用于识别有效数据 "ESAD" */
#define ESAIRDATA_MAGIC 0x45534144

/* Flash存储地址 - Sector 6 */
#define W25QADDR_ESAIRDATA  (SD4096 * 6)

/* 参数范围定义 */
#define TEMP_MIN 16
#define TEMP_MAX 30
#define TEMP_DEFAULT 20

#define WIND_MIN 0
#define WIND_MAX 2
#define WIND_DEFAULT 0

#define MODE_MIN 0
#define MODE_MAX 5

/* 持久化数据结构 */
typedef struct {
    uint32_t magic;           /* 魔法数 0x45534144 */
    uint16_t version;         /* 版本号 */
    uint16_t reserved;        /* 保留对齐 */
    
    /* 空调状态数据 */
    uint8_t air_status;       /* 开关状态 0=OFF, 1=ON */
    uint8_t air_mode;         /* 运行模式 0-5 */
    uint8_t air_temperature;  /* 设定温度 16-30 */
    uint8_t air_windspeed;    /* 风速 0-2 */
    
    uint32_t checksum;        /* 校验和 */
} ESAirdata_Persist_t;

/* 校验和计算 */
uint32_t ESAirdata_CalcChecksum(ESAirdata_Persist_t *data);

/* 参数范围校验 */
bool ESAirdata_ValidateRange(ESAirdata_Persist_t *data);

/* 保存和加载 */
void ESAirdata_Save(void);
bool ESAirdata_Load(void);

/* 延迟保存机制 */
void ESAirdata_TriggerSave(void);
void ESAirdata_CancelSave(void);

/* 设置默认值 */
void ESAirdata_SetDefault(void);

/* 从ESAirdata同步到持久化结构 */
void ESAirdata_SyncToPersist(ESAirdata_Persist_t *persist);
void ESAirdata_SyncFromPersist(ESAirdata_Persist_t *persist);

#endif
