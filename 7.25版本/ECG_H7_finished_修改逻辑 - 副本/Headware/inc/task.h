#ifndef __TASK_H
#define __TASK_H

#include "main.h"
#include "arm_math.h"
#include "pcanet_inference.h"
#include "ff.h"

/* ========== ADS1292 System Configuration Macros ========== */
#define Samples_Number  1                                          /* 采样点数：每次arm_fir_f32处理的数据个数 */
#define Block_Size      1                                          /* 块大小：每次调用arm_fir_f32处理的采样点个数 */
#define NumTaps         129                                        /* FIR滤波器系数个数（阶数+1） */
#define Show_wave       650                                       /* 波形显示缓冲区大小 */

/* ========== PCANet / ID Recognition System Configuration ========== */
#define SENSOR_WIN_LEN     250                                     /* AI模型标准输入窗口长度（250个重采样点） */
#define DISCARD_POINTS     150                                     /* 滤波器启动瞬态丢弃点数（开机瞬间数据不可靠） */
#define REG_RAW_POINTS     5000                                    /* 身份注册原始数据采集长度（约20秒@250Hz） */
#define AUTH_RAW_POINTS    1500                                    /* 身份认证原始数据采集长度（约6秒@250Hz） */
#define AUTH_WINDOW_COUNT  10                                      /* 注册时提取的R波对齐窗口模板数 */
#define PASS_VOTE_COUNT    4                                       /* 曼哈顿距离投票通过阈值（>4/10通过） */
#define MATCH_THRESHOLD    1100.0f                                 /* 曼哈顿距离匹配阈值（经过ZScore归一化后的距离） */
#define DEFAULT_MATCH_THRESHOLD     3200.0f                        /* 默认曼哈顿距离匹配阈值（兜底值，当无专属门限文件时使用） */
#define THRESHOLD_TOLERANCE_FACTOR  1.3f                           /* 专属门限宽容系数（类内最大距离 × 系数 = 个人门限） */
#define ALIGN_LEFT_MARGIN  ((int)(SENSOR_WIN_LEN * 0.4f))          /* R波对齐窗口左边界偏移（R峰左侧100点） */
#define ALIGN_RIGHT_MARGIN ((int)(SENSOR_WIN_LEN - ALIGN_LEFT_MARGIN - 1))  /* R波对齐窗口右边界偏移（R峰右侧149点） */

/* ============== H7  Instances (extern) ============= */
extern SPI_HandleTypeDef hspi1;                              /* SPI1：用于ADS1292心电采集通信 */
extern SPI_HandleTypeDef hspi2;                              /* SPI2：用于SD卡读写通信 */


/* ========== DSP Filter Instances (extern) ========== */
extern arm_fir_instance_f32 S1;                              /* 呼吸波FIR滤波器实例（低通2Hz） */
extern arm_fir_instance_f32 S2;                              /* 心电FIR滤波器实例（带通5-40Hz） */

/* ========== ADS1292 Global Variables ========== */
extern uint32_t ch1_data;                                    /* ADS1292通道1原始数据（呼吸波） */
extern uint32_t ch2_data;                                    /* ADS1292通道2原始数据（心电） */
extern uint8_t  flag;                                        /* 标志：FIR滤波完成，数据可用于显示和算法处理 */
extern uint8_t  drdy_flag;                                   /* 标志：ADS1292新数据就绪（DRDY中断设置） */

extern float32_t Input_data1;                                /* 通道1 FIR滤波器输入缓冲区 */
extern float32_t Output_data1;                               /* 通道1 FIR滤波器输出缓冲区 */
extern float32_t firState1[Block_Size + NumTaps - 1];       /* 通道1 FIR滤波器状态缓冲区（保持滤波器内部状态） */
extern float32_t Input_data2;                                /* 通道2 FIR滤波器输入缓冲区 */
extern float32_t Output_data2;                               /* 通道2 FIR滤波器输出缓冲区（用于算法和显示） */
extern float32_t firState2[Block_Size + NumTaps - 1];       /* 通道2 FIR滤波器状态缓冲区 */

extern int32_t show_ecg[Show_wave];                          /* 归一化后的心电波形数据（0-255范围，用于串口屏显示） */
extern int16_t ecg_index;                                    /* 波形显示缓冲区当前写入索引 */

/* ========== PCANet / ID Recognition Global Variables ========== */
extern int32_t ads1292_reg_raw[REG_RAW_POINTS];             /* 注册模式原始心电数据缓冲区（FIR滤波后采集） */
extern int32_t ads1292_auth_raw[AUTH_RAW_POINTS];           /* 认证模式原始心电数据缓冲区（FIR滤波后采集） */
extern int32_t sensor_aligned_window[SENSOR_WIN_LEN];       /* R波对齐后的250点标准窗口（送入PCANet的输入） */
extern float Shared_Feature_Buffer_1[FINAL_FEATURE_DIM];    /* PCANet特征缓冲区1（43008维，用于待比对特征） */
extern float Shared_Feature_Buffer_2[FINAL_FEATURE_DIM];    /* PCANet特征缓冲区2（43008维，用于模板特征） */
extern FATFS sd_fs;                                          /* FatFS文件系统对象 */
extern FIL sd_fil;                                           /* FatFS文件对象 */
extern volatile uint32_t pcanet_sample_count;                /* PCANet已采集样本计数（用于HMI状态机判断采集完成） */
extern volatile uint32_t pcanet_discard_count;               /* 滤波器瞬态丢弃计数（前DISCARD_POINTS个点丢弃） */
extern volatile uint8_t pcanet_collect_mode;                 /* 采集模式：0=空闲, 1=认证模式, 2=注册模式 */

extern uint32_t blockSize;                                   /* FIR滤波块大小（=Block_Size=1，逐点滤波） */
extern uint32_t numBlocks;                                   /* FIR滤波总调用次数（=Samples_Number/Block_Size） */

/* ========== FIR Filter Coefficients ========== */
/* ECG bandpass filter coefficients: 250Hz sample rate, 5Hz-40Hz passband */
extern const float32_t BPF_5Hz_40Hz[NumTaps];
/* Respiration lowpass filter coefficients: 250Hz sample rate, 2Hz cutoff */
extern const float32_t LPF_2Hz[NumTaps];

/* ========== Debug Output ========== */
/**
 * @brief 格式化输出到UART1(PA9, PC调试串口, 115200-8N1)
 *        系统调试信息专用（注册/认证日志, 算法状态）
 *        不发送串口屏控制指令（add波形由printf→fputc→UART5处理）
 */
void DBG_Printf(const char *fmt, ...);

/* ========== Task Interface Functions ========== */
uint8_t ecg_camp_task(void);
uint8_t ecg_showwave_task(void);

/* ========== SD Card & ID Recognition Interface ========== */
uint8_t SD_Init_FATFS(void);
uint8_t Extract_Aligned_Window(int32_t *raw_data, int raw_len, int search_start, int32_t *out_window, int *out_r_idx);
void Task_Identity_Registration(const char* user_name, int32_t *raw_buffer, int raw_len);
uint8_t Task_Identity_Authentication(const char* user_name, int32_t *raw_buffer, int raw_len);

#endif
