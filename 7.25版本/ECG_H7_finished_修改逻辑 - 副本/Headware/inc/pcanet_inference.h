#ifndef __PCANET_INFERENCE_H
#define __PCANET_INFERENCE_H

#include <stdint.h>

#define ECG_WIN_LEN      500   /* AI model: 1s sliding window */
#define MAX_FEAT_LEN     100   /* MAX feature dimension */

// 鏍规嵁璁烘枃鍙傛暟璁＄畻鍑虹殑浜岀淮鐭╅樀灏哄
#define ACDCT_ROWS       25
#define ACDCT_COLS       20
#define MAX_ROWS         10
#define MAX_COLS         10

// 鏈€缁堢洿鏂瑰浘鎷兼帴鍚庣殑楂樼淮鐗瑰緛鍚戦噺鎬荤淮搴?
// 鐪熷疄缁村害锛欰CDCT(40960) + MAX(2048) = 43008
#define ACDCT_FEATURE_DIM  40960
#define MAX_FEATURE_DIM    2048
#define FINAL_FEATURE_DIM  (ACDCT_FEATURE_DIM + MAX_FEATURE_DIM) 

/**
 * @brief 灏咥DS1292R閲囬泦鍒扮殑鍘熷蹇冪數淇″彿锛岀粡杩囬澶勭悊鏈€缁堣浆鍖栦负鐗瑰緛鍚戦噺
 * @param raw_input_window 纭欢鎴柇鐨勫師濮嬫暟缁勬寚閽? * @param in_len 纭欢鎴柇鏁扮粍鐨勫疄闄呴暱搴? * @param out_combined_feature 鎻愬彇鍚庣殑鐗瑰緛杈撳嚭鎸囬拡
 */
void PCANet_Inference_Pipeline(int32_t *raw_input_window, int in_len, float *out_combined_feature);

/**
 * @brief 璁＄畻涓や釜鐗瑰緛鍚戦噺鐨勬浖鍝堥】璺濈
 */
float Calculate_Manhattan_Distance(float *featA, float *featB, uint32_t dim);

#endif
