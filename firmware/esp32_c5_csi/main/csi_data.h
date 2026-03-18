/**
 * CSI数据处理模块头文件
 */

#ifndef CSI_DATA_H
#define CSI_DATA_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CSI_DATA_LEN 128
#define CSI_QUEUE_SIZE 10
#define CSI_SAMPLE_RATE_MS 50

// CSI处理后的数据结构
typedef struct {
    int16_t amplitude[CSI_DATA_LEN];    // 幅度数据
    int16_t phase[CSI_DATA_LEN];        // 相位数据
    uint32_t timestamp;                  // 时间戳
    int8_t rssi;                         // RSSI值
    uint8_t rate;                        // 传输速率
    uint8_t sig_len;                     // 信号长度
    uint8_t mac_addr[6];                 // MAC地址
} csi_processed_data_t;

// 初始化CSI数据处理模块
esp_err_t csi_data_init(void);

// 处理原始CSI数据
void csi_data_process(wifi_csi_info_t *raw_data);

// 获取处理后的CSI数据
BaseType_t csi_get_processed_data(csi_processed_data_t *data, TickType_t wait_time);

// 计算CSI幅度
float csi_calculate_amplitude(const int8_t *csi_data, int len);

// 计算CSI相位
float csi_calculate_phase(const int8_t *csi_data, int len);

// 计算方差（用于检测运动）
float csi_calculate_variance(const int16_t *data, int len);

#ifdef __cplusplus
}
#endif

#endif /* CSI_DATA_H */
