/**
 * Web服务器模块头文件
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_WEB_SERVER_PORT 80
#define CONFIG_MAX_CLIENTS 4

// 启动Web服务器
httpd_handle_t web_server_start(void);

// 停止Web服务器
esp_err_t web_server_stop(httpd_handle_t server);

// 获取系统状态JSON字符串
esp_err_t web_get_status_json(char *buf, size_t max_len);

// 获取闹钟列表JSON字符串
esp_err_t web_get_alarms_json(char *buf, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVER_H */
