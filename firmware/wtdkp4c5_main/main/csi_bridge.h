/**
 * CSI Bridge - 板上C5到P4的数据桥接
 * 
 * WTDKP4C5-S1中，ESP32-C5和ESP32-P4通过SDIO连接
 * 此模块负责：
 * 1. 配置板上C5进入CSI接收模式
 * 2. 通过SDIO接收原始CSI数据
 * 3. 将数据传递给CSI处理器
 */

#ifndef CSI_BRIDGE_H
#define CSI_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "csi_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化CSI桥接
esp_err_t csi_bridge_init(void);

// 启动CSI接收
esp_err_t csi_bridge_start(const char *target_ssid, const char *target_pass);

// 停止CSI接收
esp_err_t csi_bridge_stop(void);

// 检查是否正在接收
bool csi_bridge_is_running(void);

// 获取接收统计
void csi_bridge_get_stats(uint32_t *packets_received, uint32_t *packets_dropped);

// 设置CSI数据回调（供csi_processor使用）
typedef void (*csi_bridge_data_cb_t)(const csi_raw_data_t *data);
esp_err_t csi_bridge_set_callback(csi_bridge_data_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* CSI_BRIDGE_H */
