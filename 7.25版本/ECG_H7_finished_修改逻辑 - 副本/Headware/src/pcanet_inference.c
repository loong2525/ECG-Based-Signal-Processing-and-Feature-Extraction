#include "pcanet_inference.h"
#include "pcanet_weights_stable.h"
#include "arm_math.h"  
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ====================================================================
// 静态内存池 (极其重要：存放在全局 BSS 段，绝对防止单片机栈溢出死机)
// ====================================================================
static float ecg_normalized[ECG_WIN_LEN]; //存放插值+去趋势+ZScore归一化后的AI标准信号

// 将矩阵转化为一维数组，提高访问速度
static float matrix_acdct[ACDCT_ROWS * ACDCT_COLS]; 
static float matrix_max[MAX_ROWS * MAX_COLS];

// PCANet 推理过程中的中间特征图缓存 (按最大尺寸 25x20 = 500 分配)
#define MAX_MAP_SIZE (ACDCT_ROWS * ACDCT_COLS)
static float L1_FeatureMaps[8][MAX_MAP_SIZE]; 
static uint8_t HashMaps[8][MAX_MAP_SIZE];

// ====================================================================
// 内部函数声明
// ====================================================================
static void Extract_Base_Features(float *norm_in);
static float Conv2D_Pixel(const float *map, const float *weight_matrix, int filter_idx, int center_r, int center_c, int rows, int cols);
static void Conv2D_Padding(const float *in_map, const float *weight_matrix, int filter_idx, float *out_map, int rows, int cols);
static void Calculate_Histogram(const uint8_t *hash_map, int start_r, int start_c, int rows, int cols, float *out_hist);
static void Process_Single_PCANet(float *matrix_in, uint8_t rows, uint8_t cols, const float *weights_l1, const float *weights_l2, float *out_hist);

// =======================================================================
// 线性插值重采样 (Linear Interpolation Resampling)
// =======================================================================
static void Resample_Signal(int32_t *input, int in_len, float *output, int out_len) 
{
    if (in_len == out_len) {
        for (int i = 0; i < out_len; i++) output[i] = (float)input[i];
        return;
    }
    float ratio = (float)(in_len - 1) / (float)(out_len - 1);
    for (int i = 0; i < out_len; i++) 
    {
        float pos = i * ratio;
        int left_idx = (int)pos;
        int right_idx = left_idx + 1;
        if (right_idx >= in_len) right_idx = in_len - 1;
        
        float weight = pos - (float)left_idx;
        output[i] = (float)input[left_idx] + weight * ((float)input[right_idx] - (float)input[left_idx]);
    }
}

// ==============================================================================
// 【创新核心算法】：最小二乘去趋势(Detrend) + 峰峰值自适应 Z-Score
// 彻底解决带基线漂移波形导致的 "类间距离 < 类内距离" 的空间映射悖论
// ==============================================================================
static void ZScore_Normalize_Float_Adaptive(float *data, int len) 
{
    // --- 步骤 1. 最小二乘法线性去趋势 (Linear Detrending) ---
    // 目的：计算出当前窗口信号的整体倾斜坡度，并将其拉平
    float sum_y = 0.0f;
    float sum_xy = 0.0f;
    for(int i = 0; i < len; i++) {
        sum_y += data[i];
        sum_xy += (float)i * data[i];
    }
    
    float N = (float)len;
    // 0 到 N-1 的和 与 平方和 (公式化简，大幅降低MCU计算量)
    float sum_x = N * (N - 1.0f) / 2.0f;
    float sum_x2 = N * (N - 1.0f) * (2.0f * N - 1.0f) / 6.0f;
    
    // 计算最佳拟合直线的斜率(slope)和截距(intercept)
    float slope = (N * sum_xy - sum_x * sum_y) / (N * sum_x2 - sum_x * sum_x);
    float intercept = (sum_y - slope * sum_x) / N;
    
    // --- 步骤 2. 减去趋势线，并统计去漂移后的特征 ---
    float mean_new = 0.0f;
    float max_val = -999999.0f;
    float min_val = 999999.0f;
    
    for(int i = 0; i < len; i++) {
        // 核心：把倾斜的基线漂移直接减掉，强行把图2变成图1的状态
        data[i] = data[i] - (slope * (float)i + intercept); 
        
        mean_new += data[i];
        if(data[i] > max_val) max_val = data[i];
        if(data[i] < min_val) min_val = data[i];
    }
    mean_new /= N;
    
    // --- 步骤 3. 计算标准差 ---
    float sq_sum = 0.0f;
    for(int i = 0; i < len; i++) {
        float diff = data[i] - mean_new;
        sq_sum += diff * diff;
    }
    float std_dev = sqrtf(sq_sum / N);
    
    // 极小值兜底保护（防止全0死机）
    if(std_dev < 1e-6f) std_dev = 1.0f; 
    
    // --- 步骤 4. 自适应 Lambda (Adaptive Lambda) ---
    // 废除固定的 500.0f。根据当前去除漂移后的 峰峰值(Peak-to-Peak) 动态提取 10% 作为正则化项。
    // 这样无论采集到的心跳幅值是 1000 还是 10000，Lambda 都能精准匹配。
    float peak_to_peak = max_val - min_val;
    float dynamic_lambda = peak_to_peak * 0.10f; 
    
    float denominator = std_dev + dynamic_lambda;
    
    // --- 步骤 5. 最终 Z-Score 归一化 ---
    for(int i = 0; i < len; i++) {
        data[i] = (data[i] - mean_new) / denominator;
    }
}

// ====================================================================
// 1. 顶层流水线入口
// ====================================================================
void PCANet_Inference_Pipeline(int32_t *raw_input_window, int in_len, float *out_combined_feature)
{
    // (1) 域自适应映射：将采集数组插值成模型标准长度
    Resample_Signal(raw_input_window, in_len, ecg_normalized, ECG_WIN_LEN);

    // (2) 自适应去趋势与归一化：解决基线漂移与悖论问题 (调用新函数)
    ZScore_Normalize_Float_Adaptive(ecg_normalized, ECG_WIN_LEN);

    // (3) 将归一化后的基础特征经过AC/DCT或MAX特征提取后填入 matrix_acdct 和 matrix_max
    Extract_Base_Features(ecg_normalized); 

    // 定义指针划分 feat_acdct_ptr和feat_max_ptr的空间
    float *feat_acdct_ptr = out_combined_feature;
    // MAX 特征必须紧挨在 ACDCT 真实长度之后拼接
    float *feat_max_ptr = out_combined_feature + ACDCT_FEATURE_DIM; 
    
    // (4) 双轨送入 PCANet 提取高维稀疏特征
    Process_Single_PCANet(matrix_acdct, ACDCT_ROWS, ACDCT_COLS, (const float*)V_ACDCT_L1, (const float*)V_ACDCT_L2, feat_acdct_ptr);
    Process_Single_PCANet(matrix_max, MAX_ROWS, MAX_COLS, (const float*)V_MAX_L1, (const float*)V_MAX_L2, feat_max_ptr);
}

// ====================================================================
// 2. 基础特征提取模块 (AC/DCT + MAX)
// ====================================================================
static void Extract_Base_Features(float *norm_in)
{
    static float Rxx[ECG_WIN_LEN];
    memset(Rxx, 0, sizeof(Rxx));
    
    // AC
    for(int m = 0; m < ECG_WIN_LEN; m++) {
        float sum = 0.0f;
        for(int t = 0; t < ECG_WIN_LEN - m; t++) sum += norm_in[t] * norm_in[t + m];
        Rxx[m] = sum;
    }
    
    float rxx_norm_factor = Rxx[0] + 1e-6f; 
    for(int m = 0; m < ECG_WIN_LEN; m++) Rxx[m] /= rxx_norm_factor;
    
    // DCT
    static float dct_out[ECG_WIN_LEN];
    memset(dct_out, 0, sizeof(dct_out));
    float G_0 = sqrtf(1.0f / (float)ECG_WIN_LEN);
    float G_u = sqrtf(2.0f / (float)ECG_WIN_LEN);
    
    for(int u = 0; u < ECG_WIN_LEN; u++) {
        float sum = 0.0f;
        for(int i = 0; i < ECG_WIN_LEN; i++) {
            sum += Rxx[i] * cosf((M_PI * (2.0f * i + 1.0f) * u) / (2.0f * ECG_WIN_LEN));
        }
        if(u == 0) dct_out[u] = G_0 * sum;
        else       dct_out[u] = G_u * sum;
    }
    
    // 存入一维化的二维矩阵
    for(int i = 0; i < ECG_WIN_LEN; i++) matrix_acdct[i] = dct_out[i];

    // MAX
    float max_val = 0.0f;
    int loc = 0; 
    for(int i = 0; i < ECG_WIN_LEN; i++) {
        if(fabsf(norm_in[i]) > max_val) {
            max_val = fabsf(norm_in[i]);
            loc = i;
        }
    }
    
    float max_feat[MAX_FEAT_LEN] = {0}; 
    int left_idx = loc - (MAX_FEAT_LEN/2 - 1);
    int right_idx = loc + (MAX_FEAT_LEN/2);
    
    if(left_idx >= 0 && right_idx < ECG_WIN_LEN) {
        memcpy(max_feat, &norm_in[left_idx], MAX_FEAT_LEN * sizeof(float));
    } else if(left_idx < 0) {
        memcpy(&max_feat[-left_idx], &norm_in[0], (right_idx + 1) * sizeof(float));
    } else if(right_idx >= ECG_WIN_LEN) {
        memcpy(&max_feat[0], &norm_in[left_idx], (ECG_WIN_LEN - left_idx) * sizeof(float));
    }
    
    for(int i = 0; i < MAX_FEAT_LEN; i++) matrix_max[i] = max_feat[i];
}

// ====================================================================
// 3. PCANet 核心前向推理引擎
// ====================================================================
static void Process_Single_PCANet(float *matrix_in, uint8_t rows, uint8_t cols, const float *weights_l1, const float *weights_l2, float *out_hist)
{
    const int num_filters_L1 = 8;
    const int num_filters_L2 = 8;

    // --- 第一层: 2D 卷积 ---
    for(int f = 0; f < num_filters_L1; f++) {
        Conv2D_Padding(matrix_in, weights_l1, f, L1_FeatureMaps[f], rows, cols);
    }

    // --- 第二层: 级联卷积与哈希二值化 ---
    for(int i = 0; i < num_filters_L1; i++) {
        for(int r = 0; r < rows; r++) {
            for(int c = 0; c < cols; c++) {
                uint8_t hash_val = 0;
                for(int f = 0; f < num_filters_L2; f++) {
                    float conv_val = Conv2D_Pixel(L1_FeatureMaps[i], weights_l2, f, r, c, rows, cols);
                    if(conv_val > 0.0f) {
                        hash_val += (1 << f); 
                    }
                }
                HashMaps[i][r * cols + c] = hash_val;
            }
        }
    }

    // --- 第三层: 块直方图池化统计 ---
    int hist_ptr = 0;
    int stride = 4; 
    
    for(int i = 0; i < num_filters_L1; i++) {
        for(int r = 0; r <= rows - 7; r += stride) {
            for(int c = 0; c <= cols - 7; c += stride) {
                Calculate_Histogram(HashMaps[i], r, c, rows, cols, &out_hist[hist_ptr]);
                hist_ptr += 256; 
            }
        }
    }
}

// ====================================================================
// 4. 底层数学处理库
// ====================================================================
static float Conv2D_Pixel(const float *map, const float *weight_matrix, int filter_idx, int center_r, int center_c, int rows, int cols)
{
    float sum = 0.0f;
    const int k_size = 8; 
    const int num_filters = 8; 
    
    int pad_offset_r = k_size / 2; 
    int pad_offset_c = k_size / 2; 
    
    for(int wr = 0; wr < k_size; wr++) {
        for(int wc = 0; wc < k_size; wc++) {
            int map_r = center_r + wr - pad_offset_r;
            int map_c = center_c + wc - pad_offset_c;
            
            if(map_r >= 0 && map_r < rows && map_c >= 0 && map_c < cols) {
                int map_idx = map_r * cols + map_c;
                int pixel_idx = wr * k_size + wc;
                int weight_idx = pixel_idx * num_filters + filter_idx; 
                
                sum += map[map_idx] * weight_matrix[weight_idx];
            }
        }
    }
    return sum;
}

static void Conv2D_Padding(const float *in_map, const float *weight_matrix, int filter_idx, float *out_map, int rows, int cols)
{
    for(int r = 0; r < rows; r++) {
        for(int c = 0; c < cols; c++) {
            int out_idx = r * cols + c;
            out_map[out_idx] = Conv2D_Pixel(in_map, weight_matrix, filter_idx, r, c, rows, cols);
        }
    }
}

static void Calculate_Histogram(const uint8_t *hash_map, int start_r, int start_c, int rows, int cols, float *out_hist)
{
    for(int i = 0; i < 256; i++) out_hist[i] = 0.0f;
    
    for(int br = 0; br < 7; br++) {
        for(int bc = 0; bc < 7; bc++) {
            int map_r = start_r + br;
            int map_c = start_c + bc;
            
            if(map_r < rows && map_c < cols) {
                int map_idx = map_r * cols + map_c;
                uint8_t pixel_val = hash_map[map_idx];
                out_hist[pixel_val] += 1.0f;
            }
        }
    }
}

// ====================================================================
// 5. 相似度判别
// ====================================================================
float Calculate_Manhattan_Distance(float *featA, float *featB, uint32_t dim)
{
    float distance = 0.0f;
    for(uint32_t i = 0; i < dim; i++) {
        distance += fabsf(featA[i] - featB[i]);
    }
    return distance; 
}