#include "task.h"
#include "usart.h"
#include <stdarg.h>

/* ========== DSP Filter Instances ========== */
arm_fir_instance_f32 S1; /* FIR实例1：呼吸波LPF(截止2Hz) */
arm_fir_instance_f32 S2; /* FIR实例2：心电BPF(5-40Hz) */

/* ========== ADS1292 Global Variables ========== */
uint32_t ch1_data; /* ADS1292通道1原始数据（呼吸波） */
uint32_t ch2_data; /* ADS1292通道2原始数据（心电） */
uint8_t flag;      /* 标志位：FIR滤波完成，数据可被使用 */
uint8_t drdy_flag; /* 标志位：ADS1292新数据就绪(DRDY中断) */

float32_t Input_data1;                         /* 通道1 FIR输入(单点,blockSize=1) */
float32_t Output_data1;                        /* 通道1 FIR输出(滤波后呼吸波) */
float32_t firState1[Block_Size + NumTaps - 1]; /* 通道1 FIR状态缓存(129点,保持内部状态) */
float32_t Input_data2;                         /* 通道2 FIR输入(单点) */
float32_t Output_data2;                        /* 通道2 FIR输出(滤波后心电,用于算法+显示) */
float32_t firState2[Block_Size + NumTaps - 1]; /* 通道2 FIR状态缓存(129点) */

int32_t show_ecg[Show_wave] = {0}; /* 归一化心电波形(0-255),用于串口屏显示 */
int16_t ecg_index = 0;             /* 波形显示缓冲区当前写入索引 */

uint32_t blockSize = Block_Size;                  /* FIR滤波块大小(=1,逐点滤波) */
uint32_t numBlocks = Samples_Number / Block_Size; /* FIR滤波总调用次数(=Samples_Number/Block_Size) */

/* ========== FIR Filter Coefficients ========== */
/* ECG bandpass filter coefficients: 250Hz sample rate, 5Hz-40Hz passband */
const float32_t BPF_5Hz_40Hz[NumTaps] = {
    3.523997657e-05, 0.0002562592272, 0.0005757701583, 0.0008397826459, 0.000908970891,
    0.0007304374012, 0.0003793779761, 4.222582356e-05, -6.521392788e-05, 0.0001839015895,
    0.0007320778677, 0.001328663086, 0.001635892317, 0.001413777587, 0.0006883906899,
    -0.0002056905651, -0.0007648666506, -0.0005919140531, 0.0003351111372, 0.001569915912,
    0.002375603188, 0.002117323689, 0.0006689901347, -0.001414557919, -0.003109993879,
    -0.003462586319, -0.00217742566, 8.629632794e-05, 0.001947802957, 0.002011778764,
    -0.0002987752669, -0.004264956806, -0.00809297245, -0.009811084718, -0.008411717601,
    -0.004596390296, -0.0006214127061, 0.0007985962438, -0.001978532877, -0.008395017125,
    -0.01568987407, -0.02018531598, -0.01929843985, -0.01321159769, -0.005181713495,
    -0.0001112028476, -0.001950757345, -0.01125541423, -0.0243169684, -0.03460548073,
    -0.03605531529, -0.02662901953, -0.01020727865, 0.004513713531, 0.008002913557,
    -0.004921500571, -0.03125274926, -0.05950148031, -0.07363011688, -0.05986980721,
    -0.01351031102, 0.05752891302, 0.1343045086, 0.1933406889, 0.2154731899,
    0.1933406889, 0.1343045086, 0.05752891302, -0.01351031102, -0.05986980721,
    -0.07363011688, -0.05950148031, -0.03125274926, -0.004921500571, 0.008002913557,
    0.004513713531, -0.01020727865, -0.02662901953, -0.03605531529, -0.03460548073,
    -0.0243169684, -0.01125541423, -0.001950757345, -0.0001112028476, -0.005181713495,
    -0.01321159769, -0.01929843985, -0.02018531598, -0.01568987407, -0.008395017125,
    -0.001978532877, 0.0007985962438, -0.0006214127061, -0.004596390296, -0.008411717601,
    -0.009811084718, -0.00809297245, -0.004264956806, -0.0002987752669, 0.002011778764,
    0.001947802957, 8.629632794e-05, -0.00217742566, -0.003462586319, -0.003109993879,
    -0.001414557919, 0.0006689901347, 0.002117323689, 0.002375603188, 0.001569915912,
    0.0003351111372, -0.0005919140531, -0.0007648666506, -0.0002056905651, 0.0006883906899,
    0.001413777587, 0.001635892317, 0.001328663086, 0.0007320778677, 0.0001839015895,
    -6.521392788e-05, 4.222582356e-05, 0.0003793779761, 0.0007304374012, 0.000908970891,
    0.0008397826459, 0.0005757701583, 0.0002562592272, 3.523997657e-05};

/* Respiration lowpass filter coefficients: 250Hz sample rate, 2Hz cutoff */
const float32_t LPF_2Hz[NumTaps] = {
    -0.0004293085367, -0.0004170549801, -0.0004080719373, -0.0004015014856, -0.0003963182389,
    -0.000391335343, -0.0003852125083, -0.0003764661378, -0.0003634814057, -0.0003445262846,
    -0.0003177672043, -0.0002812864841, -0.0002331012802, -0.0001711835939, -9.348169988e-05,
    2.057720394e-06, 0.0001174666468, 0.000254732382, 0.0004157739459, 0.0006024184986,
    0.000816378044, 0.001059226575, 0.001332378131, 0.00163706555, 0.00197432097,
    0.002344956854, 0.002749550389, 0.003188427072, 0.003661649302, 0.004169005435,
    0.00471000094, 0.005283853505, 0.005889489781, 0.006525543984, 0.007190360688,
    0.007882000878, 0.008598247543, 0.009336617775, 0.01009437256, 0.01086853724,
    0.01165591553, 0.01245311089, 0.01325654797, 0.01406249963, 0.01486710832,
    0.01566641964, 0.01645640284, 0.01723298989, 0.01799209975, 0.01872966997,
    0.01944169216, 0.02012423798, 0.02077349275, 0.02138578519, 0.02195761539,
    0.0224856846, 0.02296692133, 0.02339850739, 0.02377789468, 0.02410283685,
    0.02437139489, 0.02458196506, 0.02473328263, 0.02482444048, 0.02485488541,
    0.02482444048, 0.02473328263, 0.02458196506, 0.02437139489, 0.02410283685,
    0.02377789468, 0.02339850739, 0.02296692133, 0.0224856846, 0.02195761539,
    0.02138578519, 0.02077349275, 0.02012423798, 0.01944169216, 0.01872966997,
    0.01799209975, 0.01723298989, 0.01645640284, 0.01566641964, 0.01486710832,
    0.01406249963, 0.01325654797, 0.01245311089, 0.01165591553, 0.01086853724,
    0.01009437256, 0.009336617775, 0.008598247543, 0.007882000878, 0.007190360688,
    0.006525543984, 0.005889489781, 0.005283853505, 0.00471000094, 0.004169005435,
    0.003661649302, 0.003188427072, 0.002749550389, 0.002344956854, 0.00197432097,
    0.00163706555, 0.001332378131, 0.001059226575, 0.000816378044, 0.0006024184986,
    0.0004157739459, 0.000254732382, 0.0001174666468, 2.057720394e-06, -9.348169988e-05,
    -0.0001711835939, -0.0002331012802, -0.0002812864841, -0.0003177672043, -0.0003445262846,
    -0.0003634814057, -0.0003764661378, -0.0003852125083, -0.000391335343, -0.0003963182389,
    -0.0004015014856, -0.0004080719373, -0.0004170549801, -0.0004293085367};

/* ========== PCANet / ID Recognition Global Variables ========== */
/* FATFS/FIL放在Shared_Feature_Buffer之前，防止f_read溢出破坏fs_type */
FATFS sd_fs; /* FatFS文件系统对象(SD卡挂载点0:) */
FIL sd_fil;  /* FatFS文件对象(用于.DAT和.TXT读写) */

int32_t ads1292_reg_raw[REG_RAW_POINTS];          /* 注册模式原始心电数据缓冲区(5000点，FIR滤波后积累) */
int32_t ads1292_auth_raw[AUTH_RAW_POINTS];        /* 认证模式原始心电数据缓冲区(1500点) */
int32_t sensor_aligned_window[SENSOR_WIN_LEN];    /* R波对齐后的250点标准窗口(PCANet输入) */
float Shared_Feature_Buffer_1[FINAL_FEATURE_DIM]; /* PCANet特征缓冲1(43008维=ACDCT(40960)+MAX(2048)) */
float Shared_Feature_Buffer_2[FINAL_FEATURE_DIM]; /* PCANet特征缓冲2(43008维，用于存储模板特征) */
volatile uint32_t pcanet_sample_count = 0;        /* 算法已采集样本计数(HMI状态机据此判断采集完成) */
volatile uint32_t pcanet_discard_count = 0;       /* 滤波器瞬态丢弃计数(前150个点不稳定，不采入算法) */

volatile uint8_t pcanet_collect_mode = 0; /* 采集模式:0=空闲 1=认证采集 2=注册采集 */
/* ═══════════════════════════════════════════════════
 * DBG_Printf: 双路调试输出
 * 将格式化字符串发送到 UART1 (PA9, PC调试串口, 115200-8N1)
 * 仅用于系统调试信息 (注册/认证流程日志, 不发送串口屏控制指令)
 * 串口屏控制 (add指令) 仍使用 printf → fputc → UART5
 * ═══════════════════════════════════════════════════ */
void DBG_Printf(const char *fmt, ...)
{
#define DBG_BUF_SZ 384
    static char buf[DBG_BUF_SZ];
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(buf, DBG_BUF_SZ, fmt, args);
    va_end(args);

    if (len > 0)
    {
        if (len >= DBG_BUF_SZ)
            len = DBG_BUF_SZ - 1;
        /* 仅发送到UART1（PC调试串口，系统调试信息） */
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)len, 0xffff);
    }
}

/* ========== 任务1：心电数据采集（从ADS1292读取原始数据） ========== */
/* DRDY中断触发的每次新数据，通过SPI读取9字节，提取通道1(呼吸)和通道2(心电)数据 */
uint8_t ecg_camp_task(void)
{
    /* 检查是否有新数据就绪（由HAL_GPIO_EXTI_Callback设置drdy_flag） */
    if (drdy_flag == 1)
    {
        uint8_t j;
        uint8_t read_data[9];
        CS_LOW();
        for (j = 0; j < 9; j++)
        {
            read_data[j] = SPI_ReadWriteByte(&hspi1, 0);
        }
        CS_HIGH();

        ch1_data = ((uint32_t)read_data[3] << 16) | ((uint32_t)read_data[4] << 8) | (uint32_t)read_data[5];
        ch2_data = ((uint32_t)read_data[6] << 16) | ((uint32_t)read_data[7] << 8) | (uint32_t)read_data[8];
        // 如果第24位（最高位）是1，说明是负数，把高8位全部用1补齐
        if (ch1_data & 0x00800000)
        {
            ch1_data |= 0xFF000000;
        }
        if (ch2_data & 0x00800000)
        {
            ch2_data |= 0xFF000000;
        }
        // =====================================================
        flag = 1;
        drdy_flag = 0;
    }
    return 0;
}

/* ========== 任务2：心电波形显示 + FIR滤波 + 算法数据采集钩子 ========== */
/* 1) 对通道1/2分别做FIR带通/低通滤波
 * 2) 如处于算法采集模式(pcanet_collect_mode!=0)，将滤波后数据喂入算法缓冲区
 * 3) 对波形做自适应归一化(去除异常尖峰)，映射到0-255并发送到串口屏 */
uint8_t ecg_showwave_task(void)
{
    if (flag == 1)
    {
        /* 通道1 FIR滤波：呼吸波（低通2Hz滤波器LPF_2Hz） */
        Input_data1 = (float32_t)((int32_t)ch1_data);
        arm_fir_f32(&S1, &Input_data1, &Output_data1, blockSize);

        /* 通道2 FIR滤波：心电（带通5-40Hz滤波器BPF_5Hz_40Hz） */
        Input_data2 = (float32_t)((int32_t)ch2_data);
        arm_fir_f32(&S2, &Input_data2, &Output_data2, blockSize);
        /* ───────────────── 算法数据采集钩子 ───────────────── */
        /* 当pcanet_collect_mode!=0时，说明HMI状态机进入了认证/注册流程 */
        /* 将FIR滤波后的心电数据(current_ecg_point)积累到对应缓冲区 */
        /* 采集流程：
         *   先丢弃前DISCARD_POINTS个点（滤波器瞬态）
         *   然后按模式积累到ads1292_auth_raw或ads1292_reg_raw
         *   HMI状态机检查pcanet_sample_count达到阈值后进入下一步 */
        if (pcanet_collect_mode != 0)
        {
            int32_t current_ecg_point = (int32_t)Output_data2;
            if (pcanet_discard_count < DISCARD_POINTS)
            {
                pcanet_discard_count++;
            }
            else if (pcanet_collect_mode == 1 && pcanet_sample_count < AUTH_RAW_POINTS)
            {
                ads1292_auth_raw[pcanet_sample_count++] = current_ecg_point;
            }
            else if (pcanet_collect_mode == 2 && pcanet_sample_count < REG_RAW_POINTS)
            {
                ads1292_reg_raw[pcanet_sample_count++] = current_ecg_point;
            }

            /* ═══ 波形显示：队友方案移植 — 滑动窗口自适应归一化 ═══ */
            /* 使用250点环形缓冲区追踪局部极值，浮点比率映射到5~180范围
             * 替代原先的650点批处理+AAD离群剔除方案，避免32位整数乘法溢出 */
            #define DISP_WIN_LEN 250 /* 滑动窗口长度：追踪最近250点≈1s数据的局部极值 */
            static int32_t disp_buffer[DISP_WIN_LEN];
            static uint32_t disp_idx = 0;
            uint16_t i;

            // 1. 将当前点存入环形缓冲区
            disp_buffer[disp_idx] = current_ecg_point;
            disp_idx = (disp_idx + 1) % DISP_WIN_LEN;

            // 2. 找出最近一段时间内的最大值和最小值
            int32_t local_min = disp_buffer[0];
            int32_t local_max = disp_buffer[0];
            for (int i = 1; i < DISP_WIN_LEN; i++)
            {
                if (disp_buffer[i] < local_min)
                    local_min = disp_buffer[i];
                if (disp_buffer[i] > local_max)
                    local_max = disp_buffer[i];
            }
            // 3. 安全防护：防止除数为0
            if (local_max == local_min)
            {
                local_max = local_min + 1;
            }
            // 4. 一步到位的映射公式：精准映射到 5 ~ 150
            // 【修改】：使用浮点计算比例，彻底杜绝 32 位整数乘法溢出导致数值错乱
            float ratio = (float)(current_ecg_point - local_min) / (float)(local_max - local_min);
            int32_t mapped_val = (int32_t)(ratio * 175.0f) + 5;
            // 防止轻微越界（绝对的硬性天花板）
            if (mapped_val < 5)
                mapped_val = 5;
            if (mapped_val > 180)
                mapped_val = 180;
            // 5. 打印映射后的数据
          
            printf("add s0.id,0,%d\xff\xff\xff", (uint8_t)mapped_val);
            DBG_Printf("%d\n", (uint8_t)mapped_val);
            flag = 0;
        }
        return 0;
    }
}

    /* ════════════════════════════════════════════════
     * SD卡初始化：挂载FatFS并打印容量信息
     * 在HMI系统初始化流程(Task_ProcessSysInit)中调用
     * 返回0=成功，1=失败
     * ════════════════════════════════════════════════ */
    uint8_t SD_Init_FATFS(void)
    {
        FRESULT fr;
        FATFS *pfs;
        DWORD freel;

        DBG_Printf("\r\n=== SD Card Init ===\r\n");
        fr = f_mount(&sd_fs, "0:", 1);
        if (fr != FR_OK)
        {
            DBG_Printf("SD Init FAIL: %d\r\n", fr);
            return 1;
        }
        f_getfree("", &freel, &pfs);
        DBG_Printf("SD OK - Total: %u KB, Free: %u KB\r\n",
                   (uint32_t)((pfs->n_fatent - 2) * pfs->csize / 2),
                   (uint32_t)(freel * pfs->csize / 2));
        return 0;
    }

    /* ════════════════════════════════════════════════════════
     * R波自适应寻峰 + 窗口对齐
     * 在原始数据(raw_data)的search_start位置向后搜索250点内的最大值(R峰)
     * 以R峰为中心左偏40%+右偏60%截取SENSOR_WIN_LEN(250)点窗口
     * 返回1=对齐成功, 0=未找到有效R峰
     * ════════════════════════════════════════════════════════ */
    uint8_t Extract_Aligned_Window(int32_t *raw_data, int raw_len, int search_start, int32_t *out_window, int *out_r_idx)
    {
        int max_val = -9999999;
        int max_idx = -1;
        int search_end = search_start + 250;

        if (search_end > raw_len - ALIGN_RIGHT_MARGIN - 1)
        {
            search_end = raw_len - ALIGN_RIGHT_MARGIN - 1;
        }

        for (int i = search_start; i <= search_end; i++)
        {
            if (raw_data[i] > max_val)
            {
                max_val = raw_data[i];
                max_idx = i;
            }
        }

        if (max_idx == -1 || max_idx - ALIGN_LEFT_MARGIN < 0 || max_idx + ALIGN_RIGHT_MARGIN >= raw_len)
        {
            return 0;
        }

        memcpy(out_window, &raw_data[max_idx - ALIGN_LEFT_MARGIN], SENSOR_WIN_LEN * sizeof(int32_t));
        *out_r_idx = max_idx;
        return 1;
    }

    /* ════════════════════════════════════════════════════════════
     * 身份注册核心流程
     * 1) 从注册原始缓冲区(raw_buffer)中提取10个R波对齐窗口
     * 2) 每个窗口送入PCANet_Inference_Pipeline提取43088维特征
     * 3) 将10组特征写入SD卡: <用户名>.DAT (二进制特征文件)
     * 4) 同时保存原始数据到: <用户名>_RAW.TXT (调试用)
     *
     * 注意：数据采集由ecg_showwave_task在pcanet_collect_mode=2时自动完成
     * ════════════════════════════════════════════════════════════ */
    void Task_Identity_Registration(const char *user_name, int32_t *raw_buffer, int raw_len)
    {
        FRESULT fr;
        uint8_t write_success = 1;
        char filename_dat[64];
        char filename_txt[64];
        char filename_cfg[64];
        snprintf(filename_dat, sizeof(filename_dat), "%s.DAT", user_name);
        snprintf(filename_txt, sizeof(filename_txt), "%s_RAW.TXT", user_name);
        snprintf(filename_cfg, sizeof(filename_cfg), "%s_CFG.TXT", user_name); /* 专属自适应门限配置文件名 */
        float max_intra_distance = 0.0f;                                       /* 记录10组心跳模板间的最大类内距离 */

        DBG_Printf("\r\n--- Registration: [%s] ---\r\n", user_name);

        fr = f_mount(&sd_fs, "0:", 1);
        if (fr == FR_OK)
        {
            fr = f_open(&sd_fil, filename_dat, FA_CREATE_ALWAYS | FA_WRITE);
            if (fr == FR_OK)
            {
                int valid_templates = 0;
                int search_start = ALIGN_LEFT_MARGIN;

                for (int i = 0; i < AUTH_WINDOW_COUNT; i++)
                {
                    int r_idx = 0;
                    if (!Extract_Aligned_Window(raw_buffer, raw_len, search_start, sensor_aligned_window, &r_idx))
                    {
                        DBG_Printf("Warning: only %d/10 templates extracted\r\n", valid_templates);
                        break;
                    }

                    PCANet_Inference_Pipeline(sensor_aligned_window, SENSOR_WIN_LEN, Shared_Feature_Buffer_1);

                    /* ── 自适应门限：追踪类内最大曼哈顿距离 ── */
                    if (valid_templates == 0)
                    {
                        /* 第1组模板：拷贝到Buffer_2作为基准 */
                        memcpy(Shared_Feature_Buffer_2, Shared_Feature_Buffer_1, sizeof(Shared_Feature_Buffer_1));
                    }
                    else
                    {
                        /* 第2~10组模板：与基准计算曼哈顿距离，追踪最大波动 */
                        float dist = Calculate_Manhattan_Distance(Shared_Feature_Buffer_2, Shared_Feature_Buffer_1, FINAL_FEATURE_DIM);
                        DBG_Printf("  |- 第 %d 组与基准心跳的空间距离: %.1f\r\n", valid_templates + 1, dist);
                        if (dist > max_intra_distance)
                        {
                            max_intra_distance = dist;
                        }
                    }
                    /* ──────────────────────────────────────────── */

                    uint8_t *pData = (uint8_t *)Shared_Feature_Buffer_1;
                    uint32_t bytes_remaining = sizeof(Shared_Feature_Buffer_1);
                    UINT bw;
                    while (bytes_remaining > 0)
                    {
                        UINT chunk = (bytes_remaining > 512) ? 512 : bytes_remaining;
                        fr = f_write(&sd_fil, pData, chunk, &bw);
                        if (fr != FR_OK || bw != chunk)
                        {
                            write_success = 0;
                            break;
                        }
                        pData += chunk;
                        bytes_remaining -= chunk;
                    }

                    if (write_success == 1)
                    {
                        /* 立即刷写到SD卡，确保模板数据完整落盘 */
                        fr = f_sync(&sd_fil);
                        DBG_Printf(">> Template [%d/10] done (R-idx: %d)\r\n", valid_templates + 1, r_idx);
                        valid_templates++;
                        search_start = r_idx + 150;
                    }
                    else
                    {
                        break;
                    }
                }
                if (f_close(&sd_fil) != FR_OK)
                {
                    DBG_Printf(">>> WARNING: f_close failed, file may be incomplete!\r\n");
                    write_success = 0;
                }
                /* 强制卸载并重新挂载，确保FAT缓冲写入SD卡 */
                f_mount(NULL, "", 0);
                f_mount(&sd_fs, "0:", 1);

                if (write_success && valid_templates == AUTH_WINDOW_COUNT)
                {
                    DBG_Printf(">>> Registration SUCCESS: %s.DAT saved.\r\n", user_name);
                    /* ── 核心新增：计算并保存专属自适应门限 ── */
                    float user_threshold = max_intra_distance * THRESHOLD_TOLERANCE_FACTOR;
                    /* 兜底保护：心跳稳如老狗时，仍给一个保底门限，防止认证太苛刻 */
                    if (user_threshold < 2000.0f)
                    {
                        user_threshold = 2000.0f;
                    }
                    DBG_Printf("\r\n=== 专属动态门限分析 ===\r\n");
                    DBG_Printf("  |- 用户最大心跳变异距离: %.1f\r\n", max_intra_distance);
                    DBG_Printf("  |- 宽容系数倍率: %.1fx\r\n", THRESHOLD_TOLERANCE_FACTOR);
                    DBG_Printf("  |- 自动生成 [%s] 专属门限: %.1f\r\n", user_name, user_threshold);

                    fr = f_open(&sd_fil, filename_cfg, FA_CREATE_ALWAYS | FA_WRITE);
                    if (fr == FR_OK)
                    {
                        char cfg_buf[32];
                        UINT bw;
                        int len = snprintf(cfg_buf, sizeof(cfg_buf), "%.1f\n", user_threshold);
                        f_write(&sd_fil, cfg_buf, len, &bw);
                        f_close(&sd_fil);
                        DBG_Printf(">>> 专属门限已存入 %s\r\n", filename_cfg);
                    }
                    /* ────────────────────────────────────────── */
                }
                else
                {
                    DBG_Printf(">>> Registration FAILED!\r\n");
                }
            }

            if (write_success)
            {
                fr = f_open(&sd_fil, filename_txt, FA_CREATE_ALWAYS | FA_WRITE);
                if (fr == FR_OK)
                {
                    char line_buf[32];
                    UINT bw;
                    for (uint32_t j = 0; j < (uint32_t)raw_len; j++)
                    {
                        int len = snprintf(line_buf, sizeof(line_buf), "%d\r\n", raw_buffer[j]);
                        f_write(&sd_fil, line_buf, len, &bw);
                    }
                    f_close(&sd_fil);
                    DBG_Printf(">>> Raw data saved to %s\r\n", filename_txt);
                }
            }
            f_mount(NULL, "", 0);
        }
    }

    /* ════════════════════════════════════════════════════════════
     * 身份认证核心流程
     * 1) 从认证原始缓冲区(raw_buffer)中提取1个R波对齐窗口
     * 2) 送入PCANet提取43088维特征
     * 3) 从SD卡读取<用户名>.DAT的10组模板特征
     * 4) 逐一计算曼哈顿距离，距离<=MATCH_THRESHOLD则投票+1
     * 5) 投票数>PASS_VOTE_COUNT(4)则认证通过
     * 返回1=认证通过，0=认证失败
     * ════════════════════════════════════════════════════════════ */
    uint8_t Task_Identity_Authentication(const char *user_name, int32_t *raw_buffer, int raw_len)
    {
        FRESULT fr;
        int success_votes = 0;
        char filename_dat[64];
        char filename_cfg[64];
        snprintf(filename_dat, sizeof(filename_dat), "%s.DAT", user_name);
        snprintf(filename_cfg, sizeof(filename_cfg), "%s_CFG.TXT", user_name); /* 专属自适应门限配置文件 */
        float current_threshold = DEFAULT_MATCH_THRESHOLD;                     /* 初始化为默认兜底门限，尝试加载专属门限 */

        DBG_Printf("\r\n--- Authentication: [%s] ---\r\n", user_name);

        int r_idx = 0;
        if (!Extract_Aligned_Window(raw_buffer, raw_len, ALIGN_LEFT_MARGIN, sensor_aligned_window, &r_idx))
        {
            DBG_Printf(">>> Auth FAILED: no valid R-peak found.\r\n");
            return 0;
        }
        DBG_Printf(">> R-peak aligned (idx: %d)\r\n", r_idx);

        PCANet_Inference_Pipeline(sensor_aligned_window, SENSOR_WIN_LEN, Shared_Feature_Buffer_1);

        fr = f_mount(&sd_fs, "0:", 1);
        if (fr == FR_OK)
        {
            /* ── 核心新增：优先加载个人专属自适应门限 ── */
            fr = f_open(&sd_fil, filename_cfg, FA_READ);
            if (fr == FR_OK)
            {
                char cfg_buf[32] = {0};
                UINT br;
                f_read(&sd_fil, cfg_buf, sizeof(cfg_buf) - 1, &br);
                f_close(&sd_fil);
                if (br > 0)
                {
                    sscanf(cfg_buf, "%f", &current_threshold);
                }
            }
            DBG_Printf(">> 动态安全策略：已加载 [%s] 专属自适应门限 -> %.1f\r\n", user_name, current_threshold);
            /* ──────────────────────────────────────────── */

            fr = f_open(&sd_fil, filename_dat, FA_READ);
            if (fr == FR_OK)
            {

                /* 统计实际读取的模板数量 */
                int templates_read = 0;
                /* 一次性打开文件，所有模板共用同一个句柄，避免f_close/f_open循环破坏FatFs内部状态 */
                for (int i = 0; i < AUTH_WINDOW_COUNT; i++)
                {
                    uint32_t tpl_size = sizeof(Shared_Feature_Buffer_2);
                    FSIZE_t tpl_pos = (FSIZE_t)i * (FSIZE_t)tpl_size;
                    uint32_t tpl_read_ok = 0;

                    /* 仅seek到模板位置，不重新打开文件 */
                    {
                        FRESULT fr_ls = f_lseek(&sd_fil, tpl_pos);
                        if (fr_ls != FR_OK)
                        {
                            DBG_Printf(">> Template %d lseek FAIL: fr=%d at pos=%lu\r\n",
                                       i + 1, fr_ls, (unsigned long)tpl_pos);
                            f_close(&sd_fil);
                            break;
                        }
                    }

                    /* 分块读取：每次512字节，避免大块读取可能的问题 */
                    uint8_t *pBuf = (uint8_t *)Shared_Feature_Buffer_2;
                    uint32_t remain = tpl_size;
                    uint8_t read_ok = 1;
                    while (remain > 0)
                    {
                        UINT chunk = (remain > 512) ? 512 : remain;
                        UINT br;
                        fr = f_read(&sd_fil, pBuf, chunk, &br);
                        if (fr != FR_OK || br != chunk)
                        {
                            DBG_Printf(">> Template %d read fail: fr=%d, br=%lu at offset=%lu\r\n",
                                       i + 1, fr, (unsigned long)br,
                                       (unsigned long)(tpl_size - remain));
                            f_close(&sd_fil);
                            read_ok = 0;
                            break;
                        }
                        pBuf += chunk;
                        remain -= chunk;
                    }
                    if (!read_ok)
                        break;

                    templates_read++;

                    float distance = Calculate_Manhattan_Distance(Shared_Feature_Buffer_2, Shared_Feature_Buffer_1, FINAL_FEATURE_DIM);
                    DBG_Printf("Template [%d/10] dist: %.1f -> ", i + 1, distance);

                    if (distance <= current_threshold)
                    {
                        success_votes++;
                        DBG_Printf("PASS (+1)\r\n");
                    }
                    else
                    {
                        DBG_Printf("FAIL\r\n");
                    }
                }
                f_close(&sd_fil);
            }
            else
            {
                DBG_Printf(">>> ERROR: template [%s] not found!\r\n", filename_dat);
            }
            f_mount(NULL, "", 0);
        }

        DBG_Printf(">>> Votes: %d/10 (threshold: >%d)\r\n", success_votes, PASS_VOTE_COUNT);
        if (success_votes > PASS_VOTE_COUNT)
        {
            DBG_Printf("[RESULT]: AUTH SUCCESS.\r\n");
            return 1;
        }
        else
        {
            DBG_Printf("[RESULT]: AUTH FAILED.\r\n");
            return 0;
        }
    }
