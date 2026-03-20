/**
 * 显示模块（可选）
 * 如果需要连接MIPI-DSI LCD屏幕，可以在这里实现显示功能
 */

#include <stdio.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "DISPLAY";

// 如果使用MIPI-DSI屏幕，需要在这里实现初始化函数
// 目前为占位实现

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Display module initialized (placeholder)");
    return ESP_OK;
}

esp_err_t display_show_alarm_time(const char *time_str)
{
    ESP_LOGI(TAG, "Display alarm time: %s", time_str);
    return ESP_OK;
}

esp_err_t display_show_detection_status(bool person_present)
{
    ESP_LOGI(TAG, "Display detection: %s", person_present ? "PERSON" : "NO PERSON");
    return ESP_OK;
}

esp_err_t display_clear(void)
{
    ESP_LOGI(TAG, "Display cleared");
    return ESP_OK;
}
