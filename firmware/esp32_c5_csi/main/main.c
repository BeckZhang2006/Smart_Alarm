/**
 * ESP32-C5 WiFi CSI 人体检测器
 * 用于智能闹铃系统，检测床上是否有人
 * 
 * 硬件: ESP32-C5 开发板
 * 功能: 
 *   - 采集WiFi CSI数据
 *   - 分析人体存在状态
 *   - 通过UART与WTDKP4C5-S1通信
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "esp_mac.h"

#include "csi_data.h"
#include "person_detector.h"
#include "uart_comm.h"

static const char *TAG = "CSI_DETECTOR";

// 任务句柄
TaskHandle_t csi_task_handle = NULL;
TaskHandle_t detection_task_handle = NULL;
TaskHandle_t uart_task_handle = NULL;

// 检测状态
static person_detection_state_t g_detection_state = {
    .is_person_present = false,
    .confidence = 0.0f,
    .last_update_time = 0
};

// WiFi CSI回调函数
void wifi_csi_cb(void *ctx, wifi_csi_info_t *data)
{
    if (!data || !data->buf) {
        return;
    }
    
    // 将CSI数据放入处理队列
    csi_data_process(data);
}

// CSI数据采集任务
static void csi_collection_task(void *pvParameters)
{
    ESP_LOGI(TAG, "CSI Collection Task Started");
    
    while (1) {
        // 配置WiFi CSI采集
        wifi_csi_config_t csi_config = {
            .lltf_en = true,
            .htltf_en = true,
            .stbc_htltf2_en = true,
            .ltf_merge_en = true,
            .channel_filter_en = true,
            .manu_scale = false,
            .shift = 0,
        };
        
        ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
        ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_cb, NULL));
        ESP_ERROR_CHECK(esp_wifi_set_csi(true));
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 人体检测处理任务
static void person_detection_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Person Detection Task Started");
    
    while (1) {
        // 获取处理后的CSI数据
        csi_processed_data_t processed_data;
        if (csi_get_processed_data(&processed_data, pdMS_TO_TICKS(500)) == pdTRUE) {
            // 运行人体检测算法
            person_detection_state_t new_state;
            if (person_detector_update(&processed_data, &new_state) == ESP_OK) {
                g_detection_state = new_state;
                
                ESP_LOGI(TAG, "Person Detected: %s, Confidence: %.2f%%",
                         new_state.is_person_present ? "YES" : "NO",
                         new_state.confidence * 100);
            }
        }
    }
}

// UART通信任务
static void uart_communication_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UART Communication Task Started");
    
    uart_command_t cmd;
    while (1) {
        if (uart_receive_command(&cmd, pdMS_TO_TICKS(1000)) == pdTRUE) {
            switch (cmd.type) {
                case CMD_GET_STATUS:
                    // 发送当前检测状态
                    uart_send_detection_state(&g_detection_state);
                    break;
                    
                case CMD_RESET_DETECTOR:
                    // 重置检测器
                    person_detector_reset();
                    uart_send_ack(CMD_RESET_DETECTOR);
                    break;
                    
                case CMD_SET_THRESHOLD:
                    // 设置检测阈值
                    if (cmd.data_len >= sizeof(float)) {
                        float threshold = *((float*)cmd.data);
                        person_detector_set_threshold(threshold);
                        uart_send_ack(CMD_SET_THRESHOLD);
                    }
                    break;
                    
                default:
                    ESP_LOGW(TAG, "Unknown command: %d", cmd.type);
                    break;
            }
        }
    }
}

// WiFi初始化
static void wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi...");
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // 配置为STA模式，连接到路由器或AP
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi initialized, connecting to %s", CONFIG_ESP_WIFI_SSID);
}

void app_main(void)
{
    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, "Smart Alarm CSI Detector Starting");
    ESP_LOGI(TAG, "================================");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 初始化CSI数据处理模块
    ESP_ERROR_CHECK(csi_data_init());
    
    // 初始化人体检测器
    ESP_ERROR_CHECK(person_detector_init());
    
    // 初始化UART通信
    ESP_ERROR_CHECK(uart_comm_init());
    
    // 初始化WiFi
    wifi_init();
    
    // 创建任务
    xTaskCreate(csi_collection_task, "csi_collection", 4096, NULL, 5, &csi_task_handle);
    xTaskCreate(person_detection_task, "person_detection", 8192, NULL, 4, &detection_task_handle);
    xTaskCreate(uart_communication_task, "uart_comm", 4096, NULL, 3, &uart_task_handle);
    
    ESP_LOGI(TAG, "All tasks created, system running...");
}
