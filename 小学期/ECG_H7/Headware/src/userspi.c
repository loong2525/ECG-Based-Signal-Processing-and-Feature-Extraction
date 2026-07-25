/*
 * @Compony: NUC
 * @Date: 2026-05-10 11:59:57
 * @LastEditors: Loong2525
 * @LastEditTime: 2026-05-10 13:38:02
 */
#include "userspi.h"

uint8_t SPI_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    //CS
    GPIO_InitStruct.Pin = SPI_CS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  // 推挽输出
    GPIO_InitStruct.Pull = GPIO_NOPULL;          // 无上拉或下拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; // 高速
    HAL_GPIO_Init(SPI_CS_PORT, &GPIO_InitStruct);

		return 0;

}

uint8_t SPI_ReadWriteByte(SPI_HandleTypeDef *hspi, uint8_t TxData)
{
    uint8_t RxData = 0;
    HAL_StatusTypeDef status;

    // 发送并接收1个字节，超时时间可根据需要调整（单位：毫秒）
    status = HAL_SPI_TransmitReceive(hspi, &TxData, &RxData, 1, 10);

    if (status != HAL_OK)
        return 0;          // 通信失败（超时、错误等）
    else
        return RxData;
}
