/**
 * CSI处理器实现 (ESP32-P4 边缘计算)
 * 
 * 利用P4的双核360MHz RISC-V处理器和向量指令进行：
 * - 高性能CSI数据处理
 * - 实时特征提取
 * - 轻量级机器学习推理
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "csi_processor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CSI_PROC";

// 处理器状态
static struct {
    bool initialized;
    bool running;
    float threshold;
    
    // 环形缓冲区
    csi_raw_data_t raw_buffer[CSI_HISTORY_SIZE];
    csi_features_t feature_buffer[CSI_HISTORY_SIZE];
    uint32_t buffer_index;
    
    // 环境状态
    csi_environment_state_t env_state;
    
    // 检测结果
    csi_detection_result_t last_result;
    
    // 统计
    uint32_t total_samples;
    uint32_t detection_count;
    
    // 回调
    csi_raw_data_cb_t raw_callback;
    
    // 滑动窗口
    float amplitude_window[CSI_SUBCARRIER_NUM];
    float variance_window[10];
    uint32_t window_index;
    
} processor_ctx = {
    .initialized = false,
    .running = false,
    .threshold = 30.0f,
    .buffer_index = 0,
    .total_samples = 0,
    .detection_count = 0,
    .raw_callback = NULL,
    .window_index = 0
};

// 快速平方根（使用硬件加速）
static inline float fast_sqrt(float x) {
    return sqrtf(x);  // P4有硬件浮点支持
}

// 快速反正切（硬件加速）
static inline float fast_atan2(float y, float x) {
    return atan2f(y, x);
}

// 计算幅度谱
static void calculate_amplitude_spectrum(const int8_t *csi_raw, float *amplitude, int len)
{
    // 使用ESP-DSP加速（如果可用）
    for (int i = 0; i < len; i++) {
        float real = (float)csi_raw[i * 2];
        float imag = (float)csi_raw[i * 2 + 1];
        amplitude[i] = fast_sqrt(real * real + imag * imag);
    }
}

// 计算相位谱
static void calculate_phase_spectrum(const int8_t *csi_raw, float *phase, int len)
{
    for (int i = 0; i < len; i++) {
        float real = (float)csi_raw[i * 2];
        float imag = (float)csi_raw[i * 2 + 1];
        phase[i] = fast_atan2(imag, real);
    }
}

// 计算统计特征
static void calculate_statistics(const float *data, int len, float *mean, float *std, 
                                  float *max_val, float *min_val)
{
    float sum = 0.0f;
    float max_v = -1e10f;
    float min_v = 1e10f;
    
    // 计算均值
    for (int i = 0; i < len; i++) {
        sum += data[i];
        if (data[i] > max_v) max_v = data[i];
        if (data[i] < min_v) min_v = data[i];
    }
    
    float m = sum / len;
    
    // 计算标准差
    float sum_sq = 0.0f;
    for (int i = 0; i < len; i++) {
        float diff = data[i] - m;
        sum_sq += diff * diff;
    }
    
    *mean = m;
    *std = fast_sqrt(sum_sq / len);
    *max_val = max_v;
    *min_val = min_v;
}

// 计算子载波相关性
static float calculate_subcarrier_correlation(const float *amplitude, int len)
{
    if (len < 4) return 0.0f;
    
    // 选取几个代表性的子载波计算相关性变化
    int indices[] = {5, 20, 40, 60, 80, 100, 120};
    int num_indices = sizeof(indices) / sizeof(indices[0]);
    
    float corr_sum = 0.0f;
    int count = 0;
    
    for (int i = 0; i < num_indices - 1; i++) {
        int idx1 = indices[i];
        int idx2 = indices[i + 1];
        
        if (idx2 < len) {
            float diff = fabsf(amplitude[idx1] - amplitude[idx2]);
            corr_sum += diff;
            count++;
        }
    }
    
    return count > 0 ? (corr_sum / count) : 0.0f;
}

// 计算频谱能量分布
static void calculate_spectral_features(const float *amplitude, int len,
                                         float *low_freq_energy,
                                         float *high_freq_energy,
                                         float *spectral_entropy)
{
    // 低频：前1/4子载波，高频：后1/4子载波
    int low_freq_end = len / 4;
    int high_freq_start = len * 3 / 4;
    
    float low_energy = 0.0f;
    float high_energy = 0.0f;
    float total_energy = 0.0f;
    
    for (int i = 0; i < len; i++) {
        float energy = amplitude[i] * amplitude[i];
        total_energy += energy;
        
        if (i < low_freq_end) {
            low_energy += energy;
        } else if (i >= high_freq_start) {
            high_energy += energy;
        }
    }
    
    *low_freq_energy = low_energy / (low_freq_end + 1);
    *high_freq_energy = high_energy / (len - high_freq_start);
    
    // 计算频谱熵（简化版）
    float entropy = 0.0f;
    for (int i = 0; i < len; i++) {
        float p = (amplitude[i] * amplitude[i]) / (total_energy + 1e-10f);
        if (p > 0) {
            entropy -= p * log2f(p + 1e-10f);
        }
    }
    *spectral_entropy = entropy;
}

// 相位差分（检测运动）
static float calculate_phase_difference(const float *phase, int len)
{
    if (len < 2) return 0.0f;
    
    float diff_sum = 0.0f;
    int count = 0;
    
    for (int i = 1; i < len; i++) {
        float diff = fabsf(phase[i] - phase[i-1]);
        if (diff > M_PI) {
            diff = 2 * M_PI - diff;  // 处理相位环绕
        }
        diff_sum += diff;
        count++;
    }
    
    return count > 0 ? (diff_sum / count) : 0.0f;
}

// 特征提取主函数
esp_err_t csi_processor_extract_features(const csi_raw_data_t *raw, 
                                          csi_features_t *features)
{
    if (!raw || !features) {
        return ESP_ERR_INVALID_ARG;
    }
    
    float amplitude[CSI_SUBCARRIER_NUM];
    float phase[CSI_SUBCARRIER_NUM];
    
    // 计算幅度和相位谱
    calculate_amplitude_spectrum(raw->csi_data, amplitude, CSI_SUBCARRIER_NUM);
    calculate_phase_spectrum(raw->csi_data, phase, CSI_SUBCARRIER_NUM);
    
    // 幅度统计特征
    calculate_statistics(amplitude, CSI_SUBCARRIER_NUM,
                         &features->amplitude_mean,
                         &features->amplitude_std,
                         &features->amplitude_max,
                         &features->amplitude_min);
    
    // 相位统计特征
    float phase_mean, phase_std, phase_max, phase_min;
    calculate_statistics(phase, CSI_SUBCARRIER_NUM,
                         &phase_mean, &phase_std, &phase_max, &phase_min);
    features->phase_mean = phase_mean;
    features->phase_std = phase_std;
    
    // 相位差分（运动检测）
    features->phase_variance = calculate_phase_difference(phase, CSI_SUBCARRIER_NUM);
    
    // 子载波相关性
    features->subcarrier_corr = calculate_subcarrier_correlation(amplitude, 
                                                                  CSI_SUBCARRIER_NUM);
    
    // 频谱特征
    calculate_spectral_features(amplitude, CSI_SUBCARRIER_NUM,
                                 &features->low_freq_energy,
                                 &features->high_freq_energy,
                                 &features->spectral_entropy);
    
    // 活动水平（综合指标）
    features->activity_level = 
        features->amplitude_std * 0.3f +
        features->phase_variance * 0.3f +
        features->subcarrier_corr * 0.2f +
        features->spectral_entropy * 0.2f;
    
    features->timestamp = raw->timestamp;
    
    return ESP_OK;
}

// 轻量级ML模型推理（简化版）
// 实际应用中可以加载TensorFlow Lite Micro模型
static float ml_model_predict(const csi_features_t *features)
{
    // 这里使用简化的线性组合模型
    // 权重可以根据训练数据优化
    
    float score = 0.0f;
    
    // 基于活动水平的评分
    score += features->activity_level * 0.4f;
    
    // 基于幅度变化的评分
    float amp_range = features->amplitude_max - features->amplitude_min;
    score += amp_range * 0.2f;
    
    // 基于相位变化的评分
    score += features->phase_variance * 10.0f * 0.2f;
    
    // 基于子载波相关性的评分
    score += features->subcarrier_corr * 0.1f;
    
    // 基于频谱熵的评分
    score += features->spectral_entropy * 0.1f;
    
    return score;
}

// 检测逻辑
static void perform_detection(const csi_features_t *features)
{
    // 获取ML模型预测分数
    float ml_score = ml_model_predict(features);
    
    // 结合环境基线进行判断
    float adjusted_score = ml_score;
    if (processor_ctx.env_state.is_calibrated) {
        // 减去环境基线
        adjusted_score -= processor_ctx.env_state.baseline_variance;
    }
    
    // 计算置信度（使用sigmoid函数）
    float confidence = 1.0f / (1.0f + expf(-(adjusted_score - processor_ctx.threshold) / 10.0f));
    
    // 判断结果
    bool person_detected = (adjusted_score > processor_ctx.threshold) && (confidence > 0.6f);
    
    // 更新滑动窗口（用于平滑）
    processor_ctx.variance_window[processor_ctx.window_index % 10] = adjusted_score;
    processor_ctx.window_index++;
    
    // 计算平滑后的分数
    float smoothed_score = 0.0f;
    for (int i = 0; i < 10; i++) {
        smoothed_score += processor_ctx.variance_window[i];
    }
    smoothed_score /= 10.0f;
    
    // 更新结果
    processor_ctx.last_result.person_present = person_detected;
    processor_ctx.last_result.confidence = confidence;
    processor_ctx.last_result.activity_score = smoothed_score;
    processor_ctx.last_result.last_update_time = features->timestamp;
    
    if (person_detected) {
        processor_ctx.last_result.detection_count++;
        processor_ctx.detection_count++;
    }
    
    // 存储特征到缓冲区
    processor_ctx.feature_buffer[processor_ctx.buffer_index % CSI_HISTORY_SIZE] = *features;
    processor_ctx.buffer_index++;
    processor_ctx.total_samples++;
    
    // 调试日志
    static uint32_t log_counter = 0;
    if (++log_counter % 100 == 0) {
        ESP_LOGI(TAG, "Score: %.2f, Conf: %.2f, Person: %s",
                 adjusted_score, confidence, person_detected ? "YES" : "NO");
    }
}

// 从C5接收CSI数据的回调（由底层驱动调用）
void csi_processor_on_raw_data(const csi_raw_data_t *raw_data)
{
    if (!processor_ctx.running) {
        return;
    }
    
    // 存储原始数据
    processor_ctx.raw_buffer[processor_ctx.buffer_index % CSI_HISTORY_SIZE] = *raw_data;
    
    // 调用用户回调
    if (processor_ctx.raw_callback) {
        processor_ctx.raw_callback(raw_data);
    }
    
    // 特征提取
    csi_features_t features;
    if (csi_processor_extract_features(raw_data, &features) == ESP_OK) {
        // 执行检测
        perform_detection(&features);
    }
}

esp_err_t csi_processor_init(void)
{
    ESP_LOGI(TAG, "Initializing CSI Processor on ESP32-P4...");
    
    if (processor_ctx.initialized) {
        return ESP_OK;
    }
    
    memset(&processor_ctx, 0, sizeof(processor_ctx));
    processor_ctx.threshold = 30.0f;
    processor_ctx.running = false;
    
    // ESP-DSP库不可用，使用软件实现
    ESP_LOGI(TAG, "Using software DSP implementation");
    
    processor_ctx.initialized = true;
    
    ESP_LOGI(TAG, "CSI Processor initialized, threshold: %.2f", processor_ctx.threshold);
    return ESP_OK;
}

esp_err_t csi_processor_start(void)
{
    if (!processor_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    processor_ctx.running = true;
    ESP_LOGI(TAG, "CSI Processor started");
    
    return ESP_OK;
}

esp_err_t csi_processor_stop(void)
{
    processor_ctx.running = false;
    ESP_LOGI(TAG, "CSI Processor stopped");
    return ESP_OK;
}

csi_detection_result_t csi_processor_get_result(void)
{
    return processor_ctx.last_result;
}

esp_err_t csi_processor_calibrate(void)
{
    ESP_LOGI(TAG, "Starting environment calibration...");
    ESP_LOGI(TAG, "Please ensure no person is in the detection area");
    
    // 重置环境状态
    processor_ctx.env_state.calibration_samples = 0;
    processor_ctx.env_state.baseline_variance = 0.0f;
    processor_ctx.env_state.noise_floor = 0.0f;
    
    // 收集100个样本计算基线
    float variance_sum = 0.0f;
    float noise_sum = 0.0f;
    uint32_t target_samples = 100;
    
    for (uint32_t i = 0; i < target_samples; i++) {
        // 等待下一个CSI数据
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // 从缓冲区读取最近的特征
        if (processor_ctx.buffer_index > 0) {
            csi_features_t *feat = &processor_ctx.feature_buffer[
                (processor_ctx.buffer_index - 1) % CSI_HISTORY_SIZE];
            
            variance_sum += feat->activity_level;
            noise_sum += feat->amplitude_std;
        }
    }
    
    processor_ctx.env_state.baseline_variance = variance_sum / target_samples;
    processor_ctx.env_state.noise_floor = noise_sum / target_samples;
    processor_ctx.env_state.calibration_samples = target_samples;
    processor_ctx.env_state.is_calibrated = true;
    
    ESP_LOGI(TAG, "Calibration completed:");
    ESP_LOGI(TAG, "  Baseline variance: %.4f", processor_ctx.env_state.baseline_variance);
    ESP_LOGI(TAG, "  Noise floor: %.4f", processor_ctx.env_state.noise_floor);
    
    return ESP_OK;
}

void csi_processor_set_threshold(float threshold)
{
    processor_ctx.threshold = threshold;
    ESP_LOGI(TAG, "Detection threshold set to: %.2f", threshold);
}

float csi_processor_get_threshold(void)
{
    return processor_ctx.threshold;
}

csi_environment_state_t csi_processor_get_env_state(void)
{
    return processor_ctx.env_state;
}

void csi_processor_reset(void)
{
    memset(&processor_ctx.last_result, 0, sizeof(csi_detection_result_t));
    memset(processor_ctx.variance_window, 0, sizeof(processor_ctx.variance_window));
    processor_ctx.window_index = 0;
    ESP_LOGI(TAG, "CSI Processor reset");
}

void csi_processor_get_stats(uint32_t *total_samples, uint32_t *detections)
{
    if (total_samples) *total_samples = processor_ctx.total_samples;
    if (detections) *detections = processor_ctx.detection_count;
}

esp_err_t csi_processor_register_raw_callback(csi_raw_data_cb_t cb)
{
    processor_ctx.raw_callback = cb;
    return ESP_OK;
}
