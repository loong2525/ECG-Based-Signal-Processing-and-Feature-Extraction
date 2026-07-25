/*
 * @Compony: NUC
 * @Date: 2026-05-10 11:59:51
 * @LastEditors: Loong2525
 * @LastEditTime: 2026-05-10 13:37:56
 */
#ifndef __USERSPI_H
#define __USERSPI_H
#include "main.h"

#if defined (STM32F10X_LD) || defined (STM32F10X_LD_VL) || defined (STM32F10X_MD) || defined (STM32F10X_MD_VL) || !defined (STM32F10X_HD) || defined (STM32F10X_HD_VL) || defined (STM32F10X_XL) || defined (STM32F10X_CL)

#define SPI_CS_PORT GPIOA
#define SPI_CS_PIN GPIO_PIN_2
#define SPI_CLK_PORT GPIOA
#define SPI_CLK_PIN GPIO_PIN_5
#define SPI_MISO_PORT GPIOA
#define SPI_MISO_PIN GPIO_PIN_6
#define SPI_MOSI_PORT GPIOA
#define SPI_MOSI_PIN GPIO_PIN_7

// GPIO操作
#define CS_LOW() HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_RESET)     // 拉低CS引脚
#define CS_HIGH() HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_SET)     // 拉高CS引脚

#define READ_MISO() HAL_GPIO_ReadPin(SPI_MISO_PORT, SPI_MISO_PIN)   // 读取MISO引脚状态

//Notice:Init仅仅配置CS引脚，其他引脚由CubeMX自动配置为SPI功能引脚
uint8_t SPI_Init(void);

uint8_t SPI_ReadWriteByte(SPI_HandleTypeDef *hspi, uint8_t TxData);

#endif
#endif
