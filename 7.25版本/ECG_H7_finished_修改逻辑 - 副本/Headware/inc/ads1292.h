/*
 * @Compony: NUC
 * @Date: 2026-05-08 20:20:39
 * @LastEditors: Loong2525
 * @LastEditTime: 2026-05-10 14:50:44
 */
#ifndef __ADS1292_H
#define __ADS1292_H

#include "main.h"

/*ADS1292命令
@cite: ADS1292数据手册 8.5.2 SPI Command Definitions
SPI应该设置为双线双向全双工，主SPI，8位数据，稳态拉低，数据捕获于第二个时钟边沿，soft管理NSS，预分频32，MSB先行
*/

//根据需要选择合适的GPIO引脚和端口
#define ADS1292_PWD_PORT GPIOA
#define ADS1292_PWD_PIN GPIO_PIN_0
#define ADS1292_DRDY_PORT GPIOA
#define ADS1292_DRDY_PIN GPIO_PIN_3
#define ADS1292_START_PORT GPIOA
#define ADS1292_START_PIN GPIO_PIN_1

//系统命令
#define WAKEUP			0X02	// 唤醒命令
#define STANDBY			0X04	// 进入待机模式命令
#define RESET			0X06	// 复位命令
#define START			0X08	// 启动或转换
#define STOP			0X0A	// 停止转换
#define OFFSETCAL		0X1A	// 通道偏移校准
//数据读取命令
#define RDATAC			0X10	//启用连续数据读取模式命令
#define SDATAC			0X11	//停止连续数据读取模式命令
#define RDATA			0X12	//读取单次数据命令
//寄存器读写命令
#define	RREG			0X20	//读取001r rrrr 000n nnnn寄存器命令，其中rrrr为寄存器地址
#define WREG			0X40	//写入010r rrrr 000n nnnn寄存器命令，其中rrrr为寄存器地址，nnnn为要写入的数据

//寄存器地址
#define ID				0X00	//ID控制寄存器地址
#define CONFIG1			0X01	//配置寄存器1地址
#define CONFIG2			0X02	//配置寄存器2地址
#define LOFF			0X03	//导联脱落控制寄存器
#define CH1SET			0X04	//通道1设置寄存器地址
#define CH2SET			0X05	//通道2设置寄存器地址
#define RLD_SENS		0X06	//右腿驱动设置寄存器地址
#define LOFF_SENS		0X07    //导联脱落检测选择寄存器地址
#define LOFF_STAT		0X08    //导联脱落检测状态寄存器地址
#define	RESP1			0X09	//呼吸检测控制寄存器地址1
#define	RESP2			0X0A	//呼吸检测控制寄存器地址2
#define	GPIO			0X0B	// GPIO寄存器地址


#define t_clk 2




void ADS1292_Init(SPI_HandleTypeDef *hspi);
void ADS1292_Read_Data(SPI_HandleTypeDef *hspi, uint8_t *data);
void ADS1292_Set_start(SPI_HandleTypeDef *hspi);
void ADS1292_Set_stop(SPI_HandleTypeDef *hspi);

uint8_t ADS1292_Read_Byte(SPI_HandleTypeDef *hspi, uint8_t byte);
void ADS1292_Write_Byte(SPI_HandleTypeDef *hspi, uint8_t addr, uint8_t data);

#endif
