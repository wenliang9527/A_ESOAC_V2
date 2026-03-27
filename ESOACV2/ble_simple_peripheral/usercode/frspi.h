/**
 * @file frspi.h
 * @brief SPI Flash ???????????? (W25Q ???)
 */

#ifndef _FRSPI_H
#define _FRSPI_H

#include <stdint.h>
#include <stdbool.h>
#include "driver_system.h"
#include "driver_gpio.h"
#include "driver_ssp.h"
#include "os_task.h"
#include "os_msg_q.h"
#include "os_timer.h"
#include "co_printf.h"
#include "sys_utils.h"

/* GPIO ?????? */
#define FLASH_CS_HIGH()         gpio_set_pin_value(GPIO_PORT_A, GPIO_BIT_5, 1)
#define FLASH_CS_LOW()          gpio_set_pin_value(GPIO_PORT_A, GPIO_BIT_5, 0)

/* Flash ??? JEDEC ID */
#define W25Q80                  0xEF13
#define W25Q16                  0xEF14
#define W25Q32                  0xEF15
#define W25Q64                  0xEF16
#define W25Q128                 0xEF17

/* ?›¥??????? */
#define SD4096                  4096
#define W25QADDR0               0
#define W25QADDR1               (SD4096)
#define W25QADDR2               (SD4096 * 5)

/* Flash ???????? */
#define SPIF_SECTOR_SIZE        4096
#define SPIF_PAGE_SIZE          256

/* Flash ??? */
#define SPIF_WriteEnable        0x06
#define SPIF_ReadStatusReg1     0x05
#define SPIF_WriteStatusReg1    0x01
#define SPIF_ReadData           0x03
#define SPIF_PageProgram        0x02
#define SPIF_SectorErase        0x20
#define SPIF_ManufactDeviceID   0x90
#define SPIF_JedecDeviceID      0x9F
#define FLASH_SPI_DUMMY_BYTE    0xA5

/* ???????? */
void fr_spi_flash(void);
uint16_t SpiFlash_ReadID(void);
bool spi_flash_is_present(void);
void SpiFlash_Erase_Sector(uint32_t dwDstAddr);
void SpiFlash_Write_Page(uint8_t *pbBuffer, uint32_t dwWriteAddr, uint32_t dwNumByteToWrite);
void SpiFlash_Read(uint8_t *pbBuffer, uint32_t dwReadAddr, uint32_t dwNumByteToRead);
void SpiFlash_Write(uint8_t *pbBuffer, uint32_t dwWriteAddr, uint32_t dwNumByteToWrite);
void SpiFlash_Write_NoCheck(uint8_t *pbBuffer, uint32_t dwWriteAddr, uint32_t dwNumByteToWrite);

/* ??????? */
uint8_t SPI_WriteByte(uint8_t bWriteValue);
uint8_t SPI_ReadByte(void);
void SpiFlash_Write_Enable(void);
void SpiFlash_Wait_Busy(void);
uint8_t SpiFlash_ReadSR1(void);
void SPI_ReadBytes(uint8_t *pbBuffer, uint32_t dwNumByteToRead);
void SPI_WriteBytes(uint8_t *pbBuffer, uint32_t dwNumByteToWrite);

#endif
