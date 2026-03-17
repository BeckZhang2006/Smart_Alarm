/**
 * ESP32-C5 WiFi CSI 发射端（独立开发板）
 * 
 * 功能：
 * 1. 作为WiFi AP或STA发送数据包
 * 2. 定期发送802.11帧供CSI采集
 * 3. 支持2.4GHz和5GHz双频
 * 
 * 硬件：独立ESP32-C5开发板
 * 位置：距离WTDKP4C5-S1约2米，形成检测区域
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "csi_transmitter.h"

static const char *TAG = "CSI_TX";

// 事件组定义
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t wifi_event_group;
static int s_retry_num = 0;
#define MAX_RETRY 5

// WiFi事件处理
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "Connect to the AP fail");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// 状态LED任务
static void status_led_task(void *pvParameters)
{
    #define STATUS_LED_GPIO 8
    
    gpio_reset_pin(STATUS_LED_GPIO);
    gpio_set_direction(STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
    
    bool led_state = false;
    while (1) {
        gpio_set_level(STATUS_LED_GPIO, led_state);
        led_state = !led_state;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "WiFi CSI Transmitter Starting");
    ESP_LOGI(TAG, "Board: ESP32-C5 Development Kit");
    ESP_LOGI(TAG, "======================================");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 创建WiFi事件组
    wifi_event_group = xEventGroupCreate();
    
    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 配置发射器
    csi_tx_config_t tx_config = {
        .ssid = CSI_TX_DEFAULT_SSID,
        .password = CSI_TX_DEFAULT_PASS,
        .channel = 6,
        .packet_interval_ms = 50,
        .packet_size = 256,
        .tx_power = 20,
    };
    
    // 初始化发射器
    ESP_ERROR_CHECK(csi_transmitter_init(&tx_config));
    
    // 注册WiFi事件处理
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));
    
    // 启动发射器
    ESP_ERROR_CHECK(csi_transmitter_start());
    
    // 创建状态LED任务
    xTaskCreate(status_led_task, "status_led", 2048, NULL, 2, NULL);
    
    ESP_LOGI(TAG, "CSI Transmitter initialized successfully");
    ESP_LOGI(TAG, "Place this device about 2 meters from the bed");
    ESP_LOGI(TAG, "Sending CSI packets every %d ms", tx_config.packet_interval_ms);
    
    // 主循环 - 打印统计信息
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        uint32_t total_sent, bytes_sent;
        csi_transmitter_get_stats(&total_sent, &bytes_sent);
        
        ESP_LOGI(TAG, "Stats: packets_sent=%lu, bytes_sent=%lu", total_sent, bytes_sent);
        
        // 获取WiFi信息
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "WiFi RSSI: %d dBm", ap_info.rssi);
        }
    }
}
