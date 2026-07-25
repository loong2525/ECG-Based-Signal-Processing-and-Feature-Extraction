/**
 ******************************************************************************
 * @file    SD.h
 * @brief   SD卡 FatFS 驱动 - 硬件抽象层
 * @note    参考 https://blog.csdn.net/ZHOU_YONG915/article/details/137437634
 *          低层使用 SPI1 (PA5 SCK, PA6 MISO, PA7 MOSI), CS = PC3
 *          SPI 模式: Mode 3 (CPOL=High, CPHA=2Edge)
 ******************************************************************************
 */

#ifndef __SD_H
#define __SD_H

#include "diskio.h"
#include "stm32h7xx_hal.h"

/*============================================================================
 *                         & 命令定义
 *============================================================================*/

#define CMD0     (0x40+0)       /* GO_IDLE_STATE */
#define CMD1     (0x40+1)       /* SEND_OP_COND */
#define CMD8     (0x40+8)       /* SEND_IF_COND */
#define CMD9     (0x40+9)       /* SEND_CSD */
#define CMD10    (0x40+10)      /* SEND_CID */
#define CMD12    (0x40+12)      /* STOP_TRANSMISSION */
#define CMD16    (0x40+16)      /* SET_BLOCKLEN */
#define CMD17    (0x40+17)      /* READ_SINGLE_BLOCK */
#define CMD18    (0x40+18)      /* READ_MULTIPLE_BLOCK */
#define CMD23    (0x40+23)      /* SET_BLOCK_COUNT */
#define CMD24    (0x40+24)      /* WRITE_BLOCK */
#define CMD25    (0x40+25)      /* WRITE_MULTIPLE_BLOCK */
#define CMD41    (0x40+41)      /* SEND_OP_COND (ACMD) */
#define CMD55    (0x40+55)      /* APP_CMD */
#define CMD58    (0x40+58)      /* READ_OCR */

/*============================================================================
 *                         卡类型标志位
 *============================================================================*/

#define CT_MMC    0x01    /* MMC ver 3 */
#define CT_SD1    0x02    /* SD ver 1 */
#define CT_SD2    0x04    /* SD ver 2 */
#define CT_SDC    0x06    /* SD */
#define CT_BLOCK  0x08    /* Block addressing */

/*============================================================================
 *                         硬件接口宏定义
 *============================================================================*/
/* 请根据实际硬件接线修改以下宏定义 */

#define SPI_TIMEOUT     100

extern SPI_HandleTypeDef hspi2;
#define HSPI_SDCARD     &hspi2

#define SD_CS_PORT      GPIOC
#define SD_CS_PIN       GPIO_PIN_3

/*============================================================================
 *                         API 函数声明
 *============================================================================*/

DSTATUS SD_disk_initialize(BYTE pdrv);
DSTATUS SD_disk_status(BYTE pdrv);
DRESULT SD_disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count);
DRESULT SD_disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count);
DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void* buff);

#endif /* __FATFS_SD_H */

