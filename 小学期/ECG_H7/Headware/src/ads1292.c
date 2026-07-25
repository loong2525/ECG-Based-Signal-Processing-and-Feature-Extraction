/*
 * @Compony: NUC
 * @Date: 2026-05-08 20:21:06
 * @LastEditors: Loong2525
 * @LastEditTime: 2026-06-02 22:03:28
 */
#include "ads1292.h"
#include "main.h"



// GPIO操作
#define PWD_LOW() HAL_GPIO_WritePin(ADS1292_PWD_PORT, ADS1292_PWD_PIN, GPIO_PIN_RESET)     // 拉低PWD引脚
#define PWD_HIGH() HAL_GPIO_WritePin(ADS1292_PWD_PORT, ADS1292_PWD_PIN, GPIO_PIN_SET)    // 拉高PWD引脚
#define START_LOW() HAL_GPIO_WritePin(ADS1292_START_PORT, ADS1292_START_PIN, GPIO_PIN_RESET)   // 拉低START引脚
#define START_HIGH() HAL_GPIO_WritePin(ADS1292_START_PORT, ADS1292_START_PIN, GPIO_PIN_SET)  // 拉高START引脚
#define DRDY_READ() HAL_GPIO_ReadPin(ADS1292_DRDY_PORT, ADS1292_DRDY_PIN)   // 读取DRDY引脚状态

/**
 * @name: delay_us
 * @param {uint32_t} us
 * @description: 延时函数，延时us微秒
 * @note:对于对延迟精度要求极高（尤其是 < 5 μs）或需要极长延迟（> 数十秒）的应用，建议额外补偿或改用定时器
 */
static void delay_us(uint32_t us)
{
    // 使能 DWT 计数器（若未使能）
    if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // 使能跟踪
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    uint32_t ticks_start = DWT->CYCCNT;
    uint32_t ticks_needed = us * (HAL_RCC_GetHCLKFreq() / 1000000);  
	// 根据当前系统时钟计算 f1 72000000 / 1000000 = 72 ticks/us 
	// F4 168000000 / 1000000 = 168 ticks/us 
	// H7 216000000 / 1000000 = 216 ticks/us 
    while( (uint32_t)(DWT->CYCCNT - ticks_start) < ticks_needed );
}


/**
 * @name: ads1292_GPIO_Int
 * @description: 初始化ADS1292的GPIO引脚
 */
void ADS1292_GPIO_Int(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    
    GPIO_InitStruct.Pull = GPIO_NOPULL;          // 无上拉或下拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; // 高速

    // 配置PWD引脚
    GPIO_InitStruct.Pin = ADS1292_PWD_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  // 推挽输出
    HAL_GPIO_Init(ADS1292_PWD_PORT, &GPIO_InitStruct);

    // 配置START引脚
    GPIO_InitStruct.Pin = ADS1292_START_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  // 推挽输出
    HAL_GPIO_Init(ADS1292_START_PORT, &GPIO_InitStruct);

    // 配置DRDY引脚
    GPIO_InitStruct.Pin = ADS1292_DRDY_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;      // 输入模式
    HAL_GPIO_Init(ADS1292_DRDY_PORT, &GPIO_InitStruct);

}

/**
 * @name: ADS1292_Read_Byte
 * @param {uint8_t} byte
 * @description: 读取ADS1292的一个字节
 */
uint8_t ADS1292_Read_Byte(SPI_HandleTypeDef *hspi, uint8_t byte)
{
    uint8_t Rxdata;

    CS_LOW(); // 选中ADS1292
    SPI_ReadWriteByte(hspi, byte); 
    delay_us(4*t_clk);
    SPI_ReadWriteByte(hspi, 0x00); // 发送一个空字节以产生时钟
    delay_us(4*t_clk); 
    Rxdata = SPI_ReadWriteByte(hspi,  0xFF); // 读取ADS1292返回的数据
    delay_us(4*t_clk);
    CS_HIGH(); 
    return Rxdata;
}

/**
 * @name: ADS1292_Write_Byte
 * @param {uint8_t} addr
 * @param {uint8_t} data
 * @description: 写入ADS1292的一个字节
 */
void ADS1292_Write_Byte(SPI_HandleTypeDef *hspi, uint8_t addr, uint8_t data)
{
    CS_LOW(); // 选中ADS1292
    SPI_ReadWriteByte(hspi, addr); 
    delay_us(4*t_clk);
    SPI_ReadWriteByte(hspi, 0x00); // 发送一个空字节以产生时钟
    delay_us(4*t_clk); 
    SPI_ReadWriteByte(hspi, data); // 读取ADS1292返回的数据
    delay_us(4*t_clk);
    CS_HIGH(); 
}

/**
 * @name: ADS1292_Init
 * @description: 初始化ADS1292
 * @note: ADS1292的初始化应该在系统时钟和SPI初始化后进行
 */
void ADS1292_Init(SPI_HandleTypeDef *hspi)
{
    ADS1292_GPIO_Int(); // 初始化GPIO引脚 

    START_HIGH(); 
    CS_HIGH(); 
    
    PWD_LOW(); // 进入掉电模式，启动系统复位
    HAL_Delay(50); // 等待50ms，确保复位完成
    PWD_HIGH(); // 退出掉电模式
    HAL_Delay(50); // 等待50ms，确保ADS1292稳定
    
    START_LOW(); // 拉低START引脚，准备进入正常工作模式
    CS_LOW(); // 选中ADS1292
    delay_us(4*t_clk); 
    // 复位ADS1292
    SPI_ReadWriteByte(hspi, SDATAC);
    delay_us(4*t_clk);
    CS_HIGH(); 

    // 读取设备ID以确认通信正常
    uint8_t device_id = ADS1292_Read_Byte(hspi,RREG | ID);
//    printf("ID:%02x\r\n",device_id);
    if(device_id == 0x73)
		{
//			printf("This device is ADS1292R\r\n");
			device_id =0;
			HAL_Delay(100);
		}
    else if(device_id ==0x53)
		{   
//			printf("This device is ADS1292\r\n");
            device_id =1;
		}
    else
    {
        printf("the device is not recognized,please check the device\r\n");
        while(1)
        {
            printf("ERROR ID:%02x\r\n", device_id);
        }
    }

    switch (device_id)
    {
        case 0:
            // 配置ADS1292R寄存器
            
            ADS1292_Write_Byte(hspi,WREG | CONFIG1, 0X01);// 设置采样率为250SPS
            delay_us(10);
//            ADS1292_Write_Byte(hspi,WREG | CONFIG2, 0XE3);//使用测试信号
						ADS1292_Write_Byte(hspi,WREG | CONFIG2, 0XE0);//不使用测试信号
            HAL_Delay(10);
            ADS1292_Write_Byte(hspi,WREG | LOFF, 0X10);
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | CH1SET, 0X40);//通道1设置为正常输入，增益为6
            delay_us(10);
 //           ADS1292_Write_Byte(hspi,WREG | CH2SET, 0X05);//测试信号输入，增益为4
						ADS1292_Write_Byte(hspi,WREG | CH2SET, 0X00);//电极输入，增益为4
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | RLD_SENS, 0xEC);
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | LOFF_SENS, 0x0F);
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | LOFF_STAT, 0x00);
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | RESP1,    0xEA);//开启呼吸检测
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | RESP2,    0x03);
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | GPIO, 0x0C);  //GPIO设置位输入
            delay_us(10);
        break;

        case 1:
            // 配置ADS1292寄存器
            ADS1292_Write_Byte(hspi,WREG | CONFIG1, 0X02);// 设置采样率为500SPS
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | CONFIG2, 0XE3);
            HAL_Delay(10);
            ADS1292_Write_Byte(hspi,WREG | LOFF, 0X10);
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | CH1SET, 0X40);
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | CH2SET, 0X05);
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | RLD_SENS, 0xEC);
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | LOFF_SENS, 0x0F);
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | LOFF_STAT, 0x00);
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | RESP1, 0x02); 
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | RESP2, 0x07);
            delay_us(10);
            ADS1292_Write_Byte(hspi,WREG | GPIO, 0x0C);
            delay_us(10);
        break;
    }
}

void ADS1292_Set_start(SPI_HandleTypeDef *hspi)
{
    CS_LOW();
		delay_us(4*t_clk);
    SPI_ReadWriteByte(hspi, RDATAC);		
    delay_us(4*t_clk);
		CS_HIGH();
    START_HIGH();
}

void ADS1292_Set_stop(SPI_HandleTypeDef *hspi)
{

    START_LOW(); // 拉低START引脚，准备进入正常工作模式
    CS_LOW(); // 选中ADS1292
    delay_us(4*t_clk); 
    // 复位ADS1292
    SPI_ReadWriteByte(hspi, SDATAC);
    delay_us(4*t_clk);
    CS_HIGH(); 
}

void ADS1292_Read_Data(SPI_HandleTypeDef *hspi, uint8_t *data)
{
    uint8_t i;
    while (HAL_GPIO_ReadPin(ADS1292_DRDY_PORT, ADS1292_DRDY_PIN) == 1);// 阻塞式等待DRDY引脚拉低，表示数据准备就绪	
    CS_LOW();
	delay_us(4*t_clk);
    for (i = 0; i < 9; i++)		
    {
        *data = SPI_ReadWriteByte(hspi, 0xFF);
        data++;
    }
    START_LOW();
    SPI_ReadWriteByte(hspi, SDATAC);	
    delay_us(4*t_clk);
	CS_HIGH();
}



