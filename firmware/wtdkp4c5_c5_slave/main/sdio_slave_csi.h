/**
 * SDIO Slave CSI模块
 * 
 * 板上ESP32-C5作为SDIO Slave：
 * 1. 接收WiFi CSI数据
 * 2. 通过SDIO传输给ESP32-P4
 * 
 * SDIO引脚定义（ESP32-C5）：
 * - CMD: GPIO10
 * - CLK: GPIO9
 * - D0: GPIO8
 * - D1: GPIO7
 * - D2: GPIO14
 * - D3: GPIO13
 */

#ifndef SDIO_SLAVE_CSI_H
#define SDIO_SLAVE_CSI_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDIO_BLOCK_SIZE     512
#define CSI_DATA_SIZE       256
#define CSI_QUEUE_SIZE      8

// CSI数据包结构（与P4共享）
typedef struct __attribute__((packed)) {
    uint32_t magic;          // 魔术字 0x43534944 ("CSID")
    uint32_t sequence;
    uint32_t timestamp;
    int16_t rssi;
    uint8_t rate;
    uint8_t channel;
    uint8_t sig_mode;
    uint8_t reserved[3];
    uint16_t csi_len;
    int8_t csi_data[CSI_DATA_SIZE];
} csi_sdio_packet_t;

// 初始化SDIO Slave
esp_err_t sdio_slave_csi_init(void);

// 启动CSI采集和SDIO传输
esp_err_t sdio_slave_csi_start(const char *ssid, const char *password);

// 停止
esp_err_t sdio_slave_csi_stop(void);

// 获取统计
void sdio_slave_csi_get_stats(uint32_t *csi_received, uint32_t *sdio_sent);

#ifdef __cplusplus
}
#endif

#endif /* SDIO_SLAVE_CSI_H */
