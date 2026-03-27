/*
 * pin mapping
 * SPI_FLASH   8010H
 * CLK         PA4
 * CS#         PA5
 * DI          PA6
 * DO          PA7
 * VCC         VDDIO
 * VSS         GND
 */

#include "frspi.h"
#include <stdbool.h>
#include <stddef.h>

// ???????????????????????
uint8_t SpiFlash_SectorBuf[SPIF_SECTOR_SIZE];

static bool s_spi_flash_present;

/**
 * @brief ????? SPI Flash ??????????
 */
void fr_spi_flash(void)
{
    // ???? SPI ???????????
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_4, PORTA4_FUNC_SSP0_CLK);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_5, PORTA5_FUNC_A5);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_6, PORTA6_FUNC_SSP0_DOUT);
    system_set_port_mux(GPIO_PORT_A, GPIO_BIT_7, PORTA7_FUNC_SSP0_DIN);

    FLASH_CS_HIGH();  // ???????????

    // ????? SPI??8 ???????MOTOROLA ?????????????1MHz ???
    ssp_init_(8, SSP_FRAME_MOTO, SSP_MASTER_MODE,  24000000, 2, NULL);

    {
        uint16_t id = SpiFlash_ReadID();
        uint8_t mf  = (uint8_t)(id >> 8);
        uint8_t dev = (uint8_t)(id & 0xFF);
        /* Winbond W25Q80/16/32/64/128: MF 0xEF, device 0x13..0x17 */
        s_spi_flash_present = (mf == 0xEF && dev >= 0x13 && dev <= 0x17);
    }
}

bool spi_flash_is_present(void)
{
    return s_spi_flash_present;
}


/**
 * @brief ????????? Flash???????????????
 * @param  pbBuffer: ?????????
 * @param  dwWriteAddr: ??????
 * @param  dwNumByteToWrite: ??????
 */
void SpiFlash_Write(uint8_t* pbBuffer, uint32_t dwWriteAddr, uint32_t dwNumByteToWrite)
{
    uint32_t dwSectorPos;
    uint16_t wSectorOffset;
    uint16_t wSectorRemain;
    uint16_t i;
    uint8_t *SpiFlash_BUF;
    uint8_t need_erase;

    // ?????????????????
    dwSectorPos = dwWriteAddr / SPIF_SECTOR_SIZE;
    wSectorOffset = dwWriteAddr % SPIF_SECTOR_SIZE;
    wSectorRemain = SPIF_SECTOR_SIZE - wSectorOffset;

    if (dwNumByteToWrite <= wSectorRemain) {
        wSectorRemain = dwNumByteToWrite;
    }

    SpiFlash_BUF = SpiFlash_SectorBuf;

    while (1) {
        // ???????????????
        SpiFlash_Read(SpiFlash_BUF, dwSectorPos * SPIF_SECTOR_SIZE, SPIF_SECTOR_SIZE);

        // ?????????????????????? 0xFF??
        need_erase = 0;
        for (i = 0; i < wSectorRemain; i++) {
            if (SpiFlash_BUF[wSectorOffset + i] != 0xFF) {
                need_erase = 1;
                break;
            }
        }

        if (need_erase) {
            // ????????????????????????????
            SpiFlash_Erase_Sector(dwSectorPos);
            for (i = 0; i < wSectorRemain; i++) {
                SpiFlash_BUF[i + wSectorOffset] = pbBuffer[i];
            }
            SpiFlash_Write_NoCheck(SpiFlash_BUF, dwSectorPos * SPIF_SECTOR_SIZE, SPIF_SECTOR_SIZE);
        } else {
            // ????????????????
            SpiFlash_Write_NoCheck(pbBuffer, dwWriteAddr, wSectorRemain);
        }

        // ?????????????
        if (dwNumByteToWrite == wSectorRemain) {
            break;
        }

        // ????????????????
        dwSectorPos++;
        wSectorOffset = 0;
        pbBuffer += wSectorRemain;
        dwWriteAddr += wSectorRemain;
        dwNumByteToWrite -= wSectorRemain;

        if (dwNumByteToWrite > SPIF_SECTOR_SIZE) {
            wSectorRemain = SPIF_SECTOR_SIZE;
        } else {
            wSectorRemain = dwNumByteToWrite;
        }
    }
}


/**
 * @brief ?? Flash ???????
 * @param  pbBuffer: ?????????
 * @param  dwReadAddr: ??????
 * @param  dwNumByteToRead: ???????
 */
void SpiFlash_Read(uint8_t* pbBuffer, uint32_t dwReadAddr, uint32_t dwNumByteToRead)
{
    FLASH_CS_LOW();
    SPI_WriteByte(SPIF_ReadData);           // ?????????
    SPI_WriteByte((uint8_t)((dwReadAddr) >> 16));  // ???? 24 ?????
    SPI_WriteByte((uint8_t)((dwReadAddr) >> 8));
    SPI_WriteByte((uint8_t)dwReadAddr);
    SPI_ReadBytes(pbBuffer, dwNumByteToRead);
    FLASH_CS_HIGH();
}


/**
 * @brief ???????????????
 * @param  dwDstAddr: ???????
 */
void SpiFlash_Erase_Sector(uint32_t dwDstAddr)
{
    // ????????????????
    dwDstAddr *= SPIF_SECTOR_SIZE;

    SpiFlash_Write_Enable();
    SpiFlash_Wait_Busy();

    FLASH_CS_LOW();
    SPI_WriteByte(SPIF_SectorErase);
    SPI_WriteByte((uint8_t)((dwDstAddr) >> 16));
    SPI_WriteByte((uint8_t)((dwDstAddr) >> 8));
    SPI_WriteByte((uint8_t)dwDstAddr);
    FLASH_CS_HIGH();

    SpiFlash_Wait_Busy();  // ??????????
}


/**
 * @brief ????????????????????????
 * @param  pbBuffer: ?????????
 * @param  dwWriteAddr: ??????
 * @param  dwNumByteToWrite: ??????
 */
void SpiFlash_Write_NoCheck(uint8_t* pbBuffer, uint32_t dwWriteAddr, uint32_t dwNumByteToWrite)
{
    uint16_t wPageRemain;

    // ???????????????
    wPageRemain = SPIF_PAGE_SIZE - dwWriteAddr % SPIF_PAGE_SIZE;

    if (dwNumByteToWrite <= wPageRemain) {
        wPageRemain = dwNumByteToWrite;
    }

    while (1) {
        SpiFlash_Write_Page(pbBuffer, dwWriteAddr, wPageRemain);

        if (dwNumByteToWrite == wPageRemain) {
            break;
        }

        // ????????????
        pbBuffer += wPageRemain;
        dwWriteAddr += wPageRemain;
        dwNumByteToWrite -= wPageRemain;

        if (dwNumByteToWrite > SPIF_PAGE_SIZE) {
            wPageRemain = SPIF_PAGE_SIZE;
        } else {
            wPageRemain = dwNumByteToWrite;
        }
    }
}


/**
 * @brief ?????????????? 256 ????
 * @param  pbBuffer: ?????????
 * @param  dwWriteAddr: ??????
 * @param  dwNumByteToWrite: ??????
 */
void SpiFlash_Write_Page(uint8_t* pbBuffer, uint32_t dwWriteAddr, uint32_t dwNumByteToWrite)
{
    if ((0 < dwNumByteToWrite) && (dwNumByteToWrite <= SPIF_PAGE_SIZE)) {
        SpiFlash_Write_Enable();  // ?????????

        FLASH_CS_LOW();
        SPI_WriteByte(SPIF_PageProgram);      // ????????????
        SPI_WriteByte((uint8_t)((dwWriteAddr) >> 16));  // ???? 24 ?????
        SPI_WriteByte((uint8_t)((dwWriteAddr) >> 8));
        SPI_WriteByte((uint8_t)dwWriteAddr);
        SPI_WriteBytes(pbBuffer, dwNumByteToWrite);
        FLASH_CS_HIGH();

        SpiFlash_Wait_Busy();  // ?????????
    }
}


/**
 * @brief ????????????
 * @param  pbBuffer: ?????????
 * @param  dwNumByteToWrite: ??????
 */
static void SPI_WriteBytes(uint8_t* pbBuffer, uint32_t dwNumByteToWrite)
{
    uint8_t dummy_data;

    while (dwNumByteToWrite--) {
        ssp_send_then_recv(pbBuffer, 1, &dummy_data, 1);
        pbBuffer++;
    }
}


/**
 * @brief ??? Flash ????
 */
static void SpiFlash_Wait_Busy(void)
{
    while ((SpiFlash_ReadSR1() & 0x01) == 0x01);
}


/**
 * @brief ?????????? 1
 * @return ????????
 */
static uint8_t SpiFlash_ReadSR1(void)
{
    uint8_t bReadByte = 0;

    FLASH_CS_LOW();
    SPI_WriteByte(SPIF_ReadStatusReg1);
    bReadByte = (uint8_t)SPI_ReadByte();
    FLASH_CS_HIGH();

    return bReadByte;
}


/**
 * @brief ?????????
 */
static void SpiFlash_Write_Enable(void)
{
    FLASH_CS_LOW();
    SPI_WriteByte(SPIF_WriteEnable);
    FLASH_CS_HIGH();
}


/**
 * @brief ?????? ID
 * @return ??? ID??16 ????
 */
uint16_t SpiFlash_ReadID(void)
{
    uint16_t wReceiveData = 0;

    FLASH_CS_LOW();
    SPI_WriteByte(SPIF_ManufactDeviceID);
    SPI_WriteByte(0x00);
    SPI_WriteByte(0x00);
    SPI_WriteByte(0x00);
    wReceiveData |= SPI_ReadByte() << 8;
    wReceiveData |= SPI_ReadByte();
    FLASH_CS_HIGH();

    return wReceiveData;
}


/**
 * @brief ??????????? Flash
 * @param  bWriteValue: ??????????
 * @return Flash ?????????
 */
static uint8_t SPI_WriteByte(uint8_t bWriteValue)
{
    uint8_t bRxBuff;
    ssp_send_then_recv(&bWriteValue, 1, &bRxBuff, 1);
    return bRxBuff;
}


/**
 * @brief ?? Flash ?????????
 * @return ?????????
 */
static uint8_t SPI_ReadByte(void)
{
    return SPI_WriteByte(FLASH_SPI_DUMMY_BYTE);
}


/**
 * @brief ???????????
 * @param  pbBuffer: ?????????
 * @param  dwNumByteToRead: ???????
 */
static void SPI_ReadBytes(uint8_t *pbBuffer, uint32_t dwNumByteToRead)
{
    uint8_t bWriteValue = FLASH_SPI_DUMMY_BYTE;

    while (dwNumByteToRead--) {
        ssp_send_then_recv(&bWriteValue, 1, pbBuffer, 1);
        pbBuffer++;
    }
}


