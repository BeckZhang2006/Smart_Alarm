/**
 * SDIO Host CSI模块（ESP32-P4）
 * 
 * 通过SDIO从板载C5接收CSI数据
 */

#ifndef SDIO_HOST_CSI_H
#define SDIO_HOST_CSI_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "csi_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化SDIO Host
esp_err_t sdio_host_csi_init(void);

// 启动CSI数据接收
esp_err_t sdio_host_csi_start(void);

// 停止
esp_err_t sdio_host_csi_stop(void);

// 注册CSI数据回调
typedef void (*sdio_host_csi_cb_t)(const csi_raw_data_t *data);
esp_err_t sdio_host_csi_register_callback(sdio_host_csi_cb_t cb);

// 获取统计
void sdio_host_csi_get_stats(uint32_t *packets_received, uint32_t *packets_dropped);

#ifdef __cplusplus
}
#endif

#endif /* SDIO_HOST_CSI_H */
