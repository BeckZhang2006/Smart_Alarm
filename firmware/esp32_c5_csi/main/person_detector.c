/**
 * 人体检测模块
 * 
 * 检测原理：
 * 1. 人体会对WiFi信号产生多径效应，改变CSI数据的幅度和相位
 * 2. 通过分析CSI数据的变化方差来检测人体存在
 * 3. 当有人时，CSI数据的方差会显著增加
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "person_detector.h"
#include "esp_log.h"

static const char *TAG = "PERSON_DETECTOR";

// 检测器状态
static struct {
    float threshold;                    // 检测阈值
    float confidence_threshold;         // 置信度阈值
    float amplitude_history[DETECTION_BUFFER_SIZE];
    float variance_history[DETECTION_BUFFER_SIZE];
    uint32_t history_index;
    uint32_t sample_count;
    bool initialized;
} detector_ctx = {
    .threshold = DEFAULT_DETECTION_THRESHOLD,
    .confidence_threshold = DEFAULT_CONFIDENCE_THRESHOLD,
    .history_index = 0,
    .sample_count = 0,
    .initialized = false
};

// 计算滑动窗口平均值
static float calculate_moving_average(const float *data, uint32_t len)
{
    if (len == 0) return 0.0f;
    
    float sum = 0.0f;
    for (uint32_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum / len;
}

// 计算滑动窗口方差
static float calculate_moving_variance(const float *data, uint32_t len, float mean)
{
    if (len < 2) return 0.0f;
    
    float sum_sq_diff = 0.0f;
    for (uint32_t i = 0; i < len; i++) {
        float diff = data[i] - mean;
        sum_sq_diff += diff * diff;
    }
    return sum_sq_diff / len;
}

// 基于幅度变化检测人体
static float detect_by_amplitude_variance(const csi_processed_data_t *csi_data)
{
    // 计算当前CSI数据的平均幅度
    float current_amplitude = 0.0f;
    int valid_count = 0;
    
    for (int i = 0; i < CSI_DATA_LEN; i++) {
        if (csi_data->amplitude[i] > 0) {
            current_amplitude += csi_data->amplitude[i];
            valid_count++;
        }
    }
    
    if (valid_count == 0) {
        return 0.0f;
    }
    
    current_amplitude /= valid_count;
    
    // 更新历史数据
    detector_ctx.amplitude_history[detector_ctx.history_index] = current_amplitude;
    detector_ctx.history_index = (detector_ctx.history_index + 1) % DETECTION_BUFFER_SIZE;
    if (detector_ctx.sample_count < DETECTION_BUFFER_SIZE) {
        detector_ctx.sample_count++;
    }
    
    // 计算方差
    float mean = calculate_moving_average(detector_ctx.amplitude_history, 
                                          detector_ctx.sample_count);
    float variance = calculate_moving_variance(detector_ctx.amplitude_history,
                                               detector_ctx.sample_count, mean);
    
    return variance;
}

// 基于相位变化检测人体
static float detect_by_phase_variance(const csi_processed_data_t *csi_data)
{
    float phase_diff_sum = 0.0f;
    int valid_count = 0;
    
    // 计算相邻子载波之间的相位差变化
    for (int i = 1; i < CSI_DATA_LEN; i++) {
        float diff = fabsf((float)(csi_data->phase[i] - csi_data->phase[i-1]));
        if (diff > 180.0f) {
            diff = 360.0f - diff;  // 处理相位环绕
        }
        phase_diff_sum += diff;
        valid_count++;
    }
    
    return valid_count > 0 ? (phase_diff_sum / valid_count) : 0.0f;
}

// 基于子载波相关性检测人体
static float detect_by_subcarrier_correlation(const csi_processed_data_t *csi_data)
{
    // 计算子载波之间的相关性变化
    // 人体存在会导致多径效应，改变子载波之间的相关性
    
    float correlation_change = 0.0f;
    int count = 0;
    
    // 选取几个代表性的子载波进行比较
    const int subcarriers[] = {5, 20, 40, 60, 80, 100};
    const int num_subcarriers = sizeof(subcarriers) / sizeof(subcarriers[0]);
    
    for (int i = 0; i < num_subcarriers - 1; i++) {
        int idx1 = subcarriers[i];
        int idx2 = subcarriers[i + 1];
        
        if (idx2 < CSI_DATA_LEN) {
            float amp_diff = fabsf((float)(csi_data->amplitude[idx1] - csi_data->amplitude[idx2]));
            correlation_change += amp_diff;
            count++;
        }
    }
    
    return count > 0 ? (correlation_change / count) : 0.0f;
}

esp_err_t person_detector_init(void)
{
    memset(&detector_ctx, 0, sizeof(detector_ctx));
    detector_ctx.threshold = DEFAULT_DETECTION_THRESHOLD;
    detector_ctx.confidence_threshold = DEFAULT_CONFIDENCE_THRESHOLD;
    detector_ctx.initialized = true;
    
    ESP_LOGI(TAG, "Person detector initialized, threshold: %.2f", detector_ctx.threshold);
    return ESP_OK;
}

esp_err_t person_detector_update(const csi_processed_data_t *csi_data, 
                                  person_detection_state_t *state)
{
    if (!csi_data || !state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!detector_ctx.initialized) {
        ESP_LOGE(TAG, "Detector not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 使用多种检测方法
    float amp_variance = detect_by_amplitude_variance(csi_data);
    float phase_variance = detect_by_phase_variance(csi_data);
    float correlation = detect_by_subcarrier_correlation(csi_data);
    
    // 综合评分
    float combined_score = amp_variance * 0.5f + phase_variance * 0.3f + correlation * 0.2f;
    
    // 更新方差历史
    detector_ctx.variance_history[detector_ctx.history_index] = combined_score;
    
    // 计算平滑后的方差
    float smoothed_variance = calculate_moving_average(detector_ctx.variance_history,
                                                       detector_ctx.sample_count);
    
    // 判断是否有人
    bool person_detected = (smoothed_variance > detector_ctx.threshold);
    
    // 计算置信度
    float confidence = 0.0f;
    if (person_detected) {
        confidence = fminf(1.0f, smoothed_variance / (detector_ctx.threshold * 2.0f));
    } else {
        confidence = fmaxf(0.0f, 1.0f - (smoothed_variance / detector_ctx.threshold));
    }
    
    // 更新状态
    state->is_person_present = person_detected && (confidence > detector_ctx.confidence_threshold);
    state->confidence = confidence;
    state->last_update_time = csi_data->timestamp;
    
    // 调试日志
    static uint32_t log_counter = 0;
    if (++log_counter % 50 == 0) {
        ESP_LOGI(TAG, "Amp: %.2f, Phase: %.2f, Corr: %.2f, Score: %.2f, Detected: %s, Conf: %.2f",
                 amp_variance, phase_variance, correlation, smoothed_variance,
                 state->is_person_present ? "YES" : "NO", state->confidence);
    }
    
    return ESP_OK;
}

void person_detector_set_threshold(float threshold)
{
    detector_ctx.threshold = threshold;
    ESP_LOGI(TAG, "Detection threshold set to: %.2f", threshold);
}

float person_detector_get_threshold(void)
{
    return detector_ctx.threshold;
}

void person_detector_reset(void)
{
    memset(detector_ctx.amplitude_history, 0, sizeof(detector_ctx.amplitude_history));
    memset(detector_ctx.variance_history, 0, sizeof(detector_ctx.variance_history));
    detector_ctx.history_index = 0;
    detector_ctx.sample_count = 0;
    
    ESP_LOGI(TAG, "Person detector reset");
}

float person_detector_get_variance(void)
{
    return calculate_moving_average(detector_ctx.variance_history, detector_ctx.sample_count);
}
