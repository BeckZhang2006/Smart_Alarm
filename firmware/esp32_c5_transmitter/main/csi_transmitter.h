/**
 * CSI发射器模块
 * 独立ESP32-C5开发板作为发射端
 */

#ifndef CSI_TRANSMITTER_H
#define CSI_TRANSMITTER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 发射器配置
typedef struct {
    char ssid[32];
    char password[64];
    uint8_t channel;
    uint16_t packet_interval_ms;
    uint16_t packet_size;
    int8_t tx_power;
} csi_tx_config_t;

// 默认配置
#define CSI_TX_DEFAULT_SSID     "SmartAlarm_CSI_TX"
#define CSI_TX_DEFAULT_PASS     "csicsicsi"
#define CSI_TX_DEFAULT_CHANNEL  6
#define CSI_TX_DEFAULT_INTERVAL 50
#define CSI_TX_DEFAULT_SIZE     256
#define CSI_TX_DEFAULT_POWER    20

// 初始化发射器
esp_err_t csi_transmitter_init(const csi_tx_config_t *config);

// 启动发射
esp_err_t csi_transmitter_start(void);

// 停止发射
esp_err_t csi_transmitter_stop(void);

// 获取统计信息
void csi_transmitter_get_stats(uint32_t *total_sent, uint32_t *bytes_sent);

// 检查是否运行中
bool csi_transmitter_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* CSI_TRANSMITTER_H */
