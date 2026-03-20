/**
 * CSI处理器模块 (ESP32-P4 边缘计算版)
 * 
 * 功能：
 * 1. 从板上ESP32-C5接收原始CSI数据（通过SDIO/UART）
 * 2. 在P4上进行CSI预处理、特征提取
 * 3. 运行机器学习模型进行人体检测
 * 4. 自适应学习和环境校准
 */

#ifndef CSI_PROCESSOR_H
#define CSI_PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CSI_SUBCARRIER_NUM      128     // 子载波数量
#define CSI_HISTORY_SIZE        50      // 历史数据缓存大小
#define CSI_FEATURE_DIM         16      // 特征维度

// CSI原始数据结构（从C5接收）
typedef struct {
    int8_t csi_data[CSI_SUBCARRIER_NUM * 2];  // 实部+虚部
    int16_t rssi;
    uint32_t timestamp;
    uint8_t rate;
    uint8_t sig_mode;
    uint8_t channel;
} csi_raw_data_t;

// CSI特征向量
typedef struct {
    float amplitude_mean;
    float amplitude_std;
    float amplitude_max;
    float amplitude_min;
    float phase_mean;
    float phase_std;
    float phase_variance;
    float subcarrier_corr;
    float high_freq_energy;
    float low_freq_energy;
    float spectral_entropy;
    float activity_level;
    uint32_t timestamp;
} csi_features_t;

// 检测状态
typedef struct {
    bool person_present;
    float confidence;
    float activity_score;
    uint32_t detection_count;
    uint32_t last_update_time;
} csi_detection_result_t;

// 环境状态（用于自适应校准）
typedef struct {
    float baseline_variance;
    float noise_floor;
    uint32_t calibration_samples;
    bool is_calibrated;
} csi_environment_state_t;

// 初始化CSI处理器
esp_err_t csi_processor_init(void);

// 启动CSI数据采集（配置C5进入CSI接收模式）
esp_err_t csi_processor_start(void);

// 停止CSI采集
esp_err_t csi_processor_stop(void);

// 获取最新检测结果
csi_detection_result_t csi_processor_get_result(void);

// 执行环境校准（建议在无人时执行）
esp_err_t csi_processor_calibrate(void);

// 设置检测阈值
void csi_processor_set_threshold(float threshold);

// 获取当前阈值
float csi_processor_get_threshold(void);

// 获取环境状态
csi_environment_state_t csi_processor_get_env_state(void);

// 重置处理器
void csi_processor_reset(void);

// 获取处理统计
void csi_processor_get_stats(uint32_t *total_samples, uint32_t *detections);

// 特征提取（用于调试）
esp_err_t csi_processor_extract_features(const csi_raw_data_t *raw, 
                                          csi_features_t *features);

// 原始数据回调（从C5接收数据时调用）
typedef void (*csi_raw_data_cb_t)(const csi_raw_data_t *data);
esp_err_t csi_processor_register_raw_callback(csi_raw_data_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* CSI_PROCESSOR_H */
