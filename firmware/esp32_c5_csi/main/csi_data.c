/**
 * CSI数据处理模块
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "csi_data.h"
#include "esp_log.h"

static const char *TAG = "CSI_DATA";

static QueueHandle_t csi_queue = NULL;
static uint32_t processed_count = 0;

esp_err_t csi_data_init(void)
{
    csi_queue = xQueueCreate(CSI_QUEUE_SIZE, sizeof(csi_processed_data_t));
    if (csi_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create CSI queue");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "CSI data module initialized");
    return ESP_OK;
}

float csi_calculate_amplitude(const int8_t *csi_data, int len)
{
    if (!csi_data || len <= 0) {
        return 0.0f;
    }
    
    float sum_amplitude = 0.0f;
    int valid_count = 0;
    
    // CSI数据格式：每个子载波占2个字节，实部和虚部各1字节
    for (int i = 0; i < len && i < CSI_DATA_LEN * 2; i += 2) {
        int8_t real = csi_data[i];
        int8_t imag = csi_data[i + 1];
        
        // 计算幅度: sqrt(real^2 + imag^2)
        float amplitude = sqrtf((float)(real * real) + (float)(imag * imag));
        sum_amplitude += amplitude;
        valid_count++;
    }
    
    return valid_count > 0 ? (sum_amplitude / valid_count) : 0.0f;
}

float csi_calculate_phase(const int8_t *csi_data, int len)
{
    if (!csi_data || len <= 0) {
        return 0.0f;
    }
    
    float sum_phase = 0.0f;
    int valid_count = 0;
    
    for (int i = 0; i < len && i < CSI_DATA_LEN * 2; i += 2) {
        int8_t real = csi_data[i];
        int8_t imag = csi_data[i + 1];
        
        // 计算相位: atan2(imag, real)
        float phase = atan2f((float)imag, (float)real);
        sum_phase += phase;
        valid_count++;
    }
    
    return valid_count > 0 ? (sum_phase / valid_count) : 0.0f;
}

float csi_calculate_variance(const int16_t *data, int len)
{
    if (!data || len <= 0) {
        return 0.0f;
    }
    
    // 计算平均值
    float sum = 0.0f;
    for (int i = 0; i < len; i++) {
        sum += (float)data[i];
    }
    float mean = sum / len;
    
    // 计算方差
    float variance = 0.0f;
    for (int i = 0; i < len; i++) {
        float diff = (float)data[i] - mean;
        variance += diff * diff;
    }
    
    return variance / len;
}

void csi_data_process(wifi_csi_info_t *raw_data)
{
    if (!raw_data || !raw_data->buf) {
        return;
    }
    
    csi_processed_data_t processed = {0};
    
    // 获取MAC地址
    memcpy(processed.mac_addr, raw_data->mac, 6);
    
    // 获取时间戳
    processed.timestamp = xTaskGetTickCount();
    
    // 获取RSSI
    processed.rssi = raw_data->rx_ctrl.rssi;
    processed.rate = raw_data->rx_ctrl.rate;
    processed.sig_len = raw_data->len;
    
    // 处理CSI数据
    int8_t *csi_buf = (int8_t *)raw_data->buf;
    int csi_len = raw_data->len;
    
    // 计算每个子载波的幅度和相位
    for (int i = 0, j = 0; i < csi_len - 1 && j < CSI_DATA_LEN; i += 2, j++) {
        int8_t real = csi_buf[i];
        int8_t imag = csi_buf[i + 1];
        
        // 幅度 (sqrt(real^2 + imag^2))
        processed.amplitude[j] = (int16_t)sqrtf((float)(real * real) + (float)(imag * imag));
        
        // 相位 (atan2(imag, real))
        processed.phase[j] = (int16_t)(atan2f((float)imag, (float)real) * 180.0f / M_PI);
    }
    
    // 发送到队列
    if (xQueueSend(csi_queue, &processed, 0) != pdTRUE) {
        // 队列已满，丢弃最旧的数据
        csi_processed_data_t dummy;
        xQueueReceive(csi_queue, &dummy, 0);
        xQueueSend(csi_queue, &processed, 0);
    }
    
    processed_count++;
    
    if (processed_count % 100 == 0) {
        ESP_LOGI(TAG, "Processed %lu CSI packets", processed_count);
    }
}

BaseType_t csi_get_processed_data(csi_processed_data_t *data, TickType_t wait_time)
{
    if (!csi_queue || !data) {
        return pdFALSE;
    }
    
    return xQueueReceive(csi_queue, data, wait_time);
}
