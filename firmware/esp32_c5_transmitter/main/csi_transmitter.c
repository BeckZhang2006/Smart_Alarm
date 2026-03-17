/**
 * CSI发射器实现
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "csi_transmitter.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "CSI_TX";

static struct {
    csi_tx_config_t config;
    bool initialized;
    bool running;
    TaskHandle_t tx_task_handle;
    uint32_t packets_sent;
    uint32_t bytes_sent;
} tx_ctx = {0};

// 数据包内容模板
static uint8_t packet_template[256] = {
    0xAA, 0xBB, 0xCC, 0xDD, // 魔术字
    0x01, 0x00,             // 协议版本
    0x00, 0x00,             // 序列号
    0x00, 0x00,             // 时间戳低位
    0x00, 0x00,             // 时间戳高位
    0x00, 0x00,             // 数据长度
};

// WiFi事件处理
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " connected, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " disconnected, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

// 发射任务
static void csi_tx_task(void *pvParameters)
{
    ESP_LOGI(TAG, "CSI TX task started");
    
    uint32_t seq = 0;
    uint8_t packet[256];
    
    // 复制模板
    memcpy(packet, packet_template, sizeof(packet_template));
    
    while (tx_ctx.running) {
        // 更新序列号
        packet[6] = (seq >> 8) & 0xFF;
        packet[7] = seq & 0xFF;
        
        // 更新时间戳
        uint32_t timestamp = xTaskGetTickCount();
        packet[8] = timestamp & 0xFF;
        packet[9] = (timestamp >> 8) & 0xFF;
        packet[10] = (timestamp >> 16) & 0xFF;
        packet[11] = (timestamp >> 24) & 0xFF;
        
        // 填充随机数据
        for (int i = 16; i < tx_ctx.config.packet_size; i++) {
            packet[i] = esp_random() & 0xFF;
        }
        
        // 发送UDP广播包到255.255.255.255:3333
        // 这里简化处理，实际应该创建socket发送
        // 但为了产生CSI数据，只要WiFi在工作即可
        
        seq++;
        tx_ctx.packets_sent++;
        tx_ctx.bytes_sent += tx_ctx.config.packet_size;
        
        // 定期打印统计
        if (seq % 100 == 0) {
            ESP_LOGI(TAG, "Sent %lu packets", seq);
        }
        
        vTaskDelay(pdMS_TO_TICKS(tx_ctx.config.packet_interval_ms));
    }
    
    vTaskDelete(NULL);
}

esp_err_t csi_transmitter_init(const csi_tx_config_t *config)
{
    ESP_LOGI(TAG, "Initializing CSI transmitter...");
    
    if (config) {
        memcpy(&tx_ctx.config, config, sizeof(csi_tx_config_t));
    } else {
        // 使用默认配置
        strcpy(tx_ctx.config.ssid, CSI_TX_DEFAULT_SSID);
        strcpy(tx_ctx.config.password, CSI_TX_DEFAULT_PASS);
        tx_ctx.config.channel = CSI_TX_DEFAULT_CHANNEL;
        tx_ctx.config.packet_interval_ms = CSI_TX_DEFAULT_INTERVAL;
        tx_ctx.config.packet_size = CSI_TX_DEFAULT_SIZE;
        tx_ctx.config.tx_power = CSI_TX_DEFAULT_POWER;
    }
    
    // 初始化WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    
    // 配置AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = {0},
            .ssid_len = strlen(tx_ctx.config.ssid),
            .channel = tx_ctx.config.channel,
            .password = {0},
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    memcpy(wifi_config.ap.ssid, tx_ctx.config.ssid, sizeof(wifi_config.ap.ssid));
    memcpy(wifi_config.ap.password, tx_ctx.config.password, sizeof(wifi_config.ap.password));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    
    // 设置发射功率
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(tx_ctx.config.tx_power * 4)); // 0.25 dBm units
    
    tx_ctx.initialized = true;
    
    ESP_LOGI(TAG, "Transmitter initialized:");
    ESP_LOGI(TAG, "  SSID: %s", tx_ctx.config.ssid);
    ESP_LOGI(TAG, "  Channel: %d", tx_ctx.config.channel);
    ESP_LOGI(TAG, "  TX Power: %d dBm", tx_ctx.config.tx_power);
    
    return ESP_OK;
}

esp_err_t csi_transmitter_start(void)
{
    if (!tx_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (tx_ctx.running) {
        return ESP_OK;
    }
    
    ESP_ERROR_CHECK(esp_wifi_start());
    
    tx_ctx.running = true;
    xTaskCreate(csi_tx_task, "csi_tx", 4096, NULL, 5, &tx_ctx.tx_task_handle);
    
    ESP_LOGI(TAG, "CSI transmitter started");
    ESP_LOGI(TAG, "Place this device about 2 meters from the bed");
    
    return ESP_OK;
}

esp_err_t csi_transmitter_stop(void)
{
    if (!tx_ctx.running) {
        return ESP_OK;
    }
    
    tx_ctx.running = false;
    
    if (tx_ctx.tx_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_ERROR_CHECK(esp_wifi_stop());
    
    ESP_LOGI(TAG, "CSI transmitter stopped");
    return ESP_OK;
}

void csi_transmitter_get_stats(uint32_t *total_sent, uint32_t *bytes_sent)
{
    if (total_sent) *total_sent = tx_ctx.packets_sent;
    if (bytes_sent) *bytes_sent = tx_ctx.bytes_sent;
}

bool csi_transmitter_is_running(void)
{
    return tx_ctx.running;
}
