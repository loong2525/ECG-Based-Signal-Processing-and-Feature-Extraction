/*
 * @Compony: NUC
 * @Date: 2026-05-10 11:59:51
 * @LastEditors: Loong2525
 * @LastEditTime: 2026-05-10 13:37:56
 */
#ifndef __USERSPI_H
#define __USERSPI_H
#include "main.h"

#define SPI_CS_PORT GPIOA
#define SPI_CS_PIN GPIO_PIN_2
#define SPI_CLK_PORT GPIOA
#define SPI_CLK_PIN GPIO_PIN_5
#define SPI_MISO_PORT GPIOA
#define SPI_MISO_PIN GPIO_PIN_6
#define SPI_MOSI_PORT GPIOA
#define SPI_MOSI_PIN GPIO_PIN_7

// GPIO鎿嶄綔
#define CS_LOW() HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_RESET)     // 鎷変綆CS寮曡剼
#define CS_HIGH() HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_SET)     // 鎷夐珮CS寮曡剼

#define READ_MISO() HAL_GPIO_ReadPin(SPI_MISO_PORT, SPI_MISO_PIN)   // 璇诲彇MISO寮曡剼鐘舵€?

//Notice:Init浠呬粎閰嶇疆CS寮曡剼锛屽叾浠栧紩鑴氱敱CubeMX鑷姩閰嶇疆涓篠PI鍔熻兘寮曡剼
uint8_t SPI_Init(void);

uint8_t SPI_ReadWriteByte(SPI_HandleTypeDef *hspi, uint8_t TxData);

#endif
