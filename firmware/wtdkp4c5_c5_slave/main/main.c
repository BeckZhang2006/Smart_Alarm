/**
 * WTDKP4C5-S1 板载ESP32-C5 SDIO Slave固件
 * 
 * 基于esp-hosted-mcu架构：
 * 1. 接收WiFi CSI数据
 * 2. 通过SDIO将CSI数据传输给ESP32-P4
 * 3. 响应P4的控制命令
 * 
 * SDIO引脚（ESP32-C5）：
 * - CMD: GPIO10
 * - CLK: GPIO9  
 * - D0: GPIO8
 * - D1: GPIO7
 * - D2: GPIO14
 * - D3: GPIO13
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "driver/sdio_slave.h"
#include "soc/sdio_slave_pins.h"

#include "sdio_slave_csi.h"

static const char *TAG = "C5_SLAVE";

// SDIO配置
#define SDIO_SLAVE_SLOT         SDSPI_HOST_SLOT_0
#define SDIO_BLOCK_SIZE         512
#define SDIO_TX_QUEUE_SIZE      10
#define SDIO_RX_QUEUE_SIZE      10

// 连接到发射端AP
#define TX_AP_SSID              "SmartAlarm_CSI_TX"
#define TX_AP_PASSWORD          "csicsicsi"

// CSI数据队列
static QueueHandle_t csi_data_queue = NULL;
static uint32_t csi_received_count = 0;
static uint32_t sdio_sent_count = 0;

// CSI数据包结构（与P4共享）
typedef struct __attribute__((packed)) {
    uint32_t magic;              // 魔术字 0x43534944 "CSID"
    uint32_t sequence;
    uint32_t timestamp;
    int16_t rssi;
    uint8_t rate;
    uint8_t channel;
    uint8_t reserved[4];
    uint16_t csi_len;
    int8_t csi_data[256];
} csi_packet_t;

#define CSI_MAGIC               0x43534944  // "CSID"

// WiFi CSI回调 - 接收CSI数据
static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *data)
{
    if (!data || !data->buf) {
        return;
    }
    
    // 分配CSI数据包
    csi_packet_t *packet = malloc(sizeof(csi_packet_t));
    if (!packet) {
        ESP_LOGE(TAG, "Failed to allocate CSI packet");
        return;
    }
    
    // 填充数据
    packet->magic = CSI_MAGIC;
    packet->sequence = csi_received_count++;
    packet->timestamp = xTaskGetTickCount();
    packet->rssi = data->rx_ctrl.rssi;
    packet->rate = data->rx_ctrl.rate;
    // sig_mode field not available in ESP-IDF v5.5+ rx_ctrl
    
    // 获取信道
    uint8_t primary;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    packet->channel = primary;
    
    // 复制CSI数据
    int len = data->len;
    if (len > 256) len = 256;
    packet->csi_len = len;
    memcpy(packet->csi_data, data->buf, len);
    
    // 发送到队列
    if (xQueueSend(csi_data_queue, &packet, 0) != pdTRUE) {
        free(packet);
    }
}

// SDIO发送任务
static void sdio_tx_task(void *pvParameters)
{
    ESP_LOGI(TAG, "SDIO TX task started");
    
    csi_packet_t *packet;
    
    while (1) {
        if (xQueueReceive(csi_data_queue, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            // 通过SDIO发送给P4
            sdio_slave_buf_handle_t buf_handle = sdio_slave_recv_register_buf((uint8_t*)packet);
            if (buf_handle) {
                esp_err_t ret = sdio_slave_send_queue(buf_handle, sizeof(csi_packet_t), 
                                                       NULL, pdMS_TO_TICKS(100));
                if (ret == ESP_OK) {
                    sdio_sent_count++;
                    ESP_LOGD(TAG, "Sent CSI packet %lu via SDIO", packet->sequence);
                } else {
                    ESP_LOGE(TAG, "Failed to queue SDIO buffer: %s", esp_err_to_name(ret));
                    sdio_slave_recv_unregister_buf(buf_handle);
                    free(packet);
                }
            } else {
                ESP_LOGE(TAG, "Failed to register SDIO buffer");
                free(packet);
            }
        }
    }
}

// 初始化SDIO Slave
static esp_err_t sdio_slave_init(void)
{
    ESP_LOGI(TAG, "Initializing SDIO Slave...");
    
    // 配置SDIO Slave (ESP-IDF v5.5+ API)
    sdio_slave_config_t config = {
        .timing = SDIO_SLAVE_TIMING_PSEND_PSAMPLE,
        .sending_mode = SDIO_SLAVE_SEND_STREAM,
        .send_queue_size = 8,
        .recv_buffer_size = SDIO_BLOCK_SIZE * 4,
        .event_cb = NULL,
        .flags = SDIO_SLAVE_FLAG_HIGH_SPEED,
    };
    
    ESP_LOGI(TAG, "Using fixed SDIO pins (IOMUX):");
    ESP_LOGI(TAG, "  CMD: GPIO%d, CLK: GPIO%d", SDIO_SLAVE_SLOT0_IOMUX_PIN_NUM_CMD, SDIO_SLAVE_SLOT0_IOMUX_PIN_NUM_CLK);
    ESP_LOGI(TAG, "  D0-D3: GPIO%d, GPIO%d, GPIO%d, GPIO%d", 
             SDIO_SLAVE_SLOT0_IOMUX_PIN_NUM_D0, SDIO_SLAVE_SLOT0_IOMUX_PIN_NUM_D1,
             SDIO_SLAVE_SLOT0_IOMUX_PIN_NUM_D2, SDIO_SLAVE_SLOT0_IOMUX_PIN_NUM_D3);
    
    ESP_ERROR_CHECK(sdio_slave_initialize(&config));
    
    ESP_LOGI(TAG, "SDIO Slave initialized");
    ESP_LOGI(TAG, "  CMD: GPIO10, CLK: GPIO9");
    ESP_LOGI(TAG, "  D0: GPIO8, D1: GPIO7, D2: GPIO14, D3: GPIO13");
    
    return ESP_OK;
}

// 初始化WiFi CSI接收
static esp_err_t wifi_csi_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi CSI receiver...");
    
    // 初始化WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // 配置为STA模式
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = TX_AP_SSID,
            .password = TX_AP_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // 配置CSI (ESP-IDF v5.5+ API)
    wifi_csi_acquire_config_t csi_config = {
        .enable = true,
        .acquire_csi_legacy = true,
        .acquire_csi_ht20 = true,
        .acquire_csi_ht40 = true,
        .acquire_csi_su = true,
        .acquire_csi_mu = true,
        .acquire_csi_dcm = true,
        .acquire_csi_beamformed = true,
        .val_scale_cfg = 0,
        .dump_ack_en = false,
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
    
    // 连接到AP
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    ESP_LOGI(TAG, "WiFi CSI receiver initialized, connecting to %s", TX_AP_SSID);
    
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "WTDKP4C5-S1 C5 SDIO Slave Starting");
    ESP_LOGI(TAG, "Role: CSI Receiver + SDIO Slave");
    ESP_LOGI(TAG, "======================================");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 创建CSI数据队列
    csi_data_queue = xQueueCreate(20, sizeof(csi_packet_t*));
    if (!csi_data_queue) {
        ESP_LOGE(TAG, "Failed to create CSI data queue");
        return;
    }
    
    // 初始化SDIO Slave
    ESP_ERROR_CHECK(sdio_slave_init());
    
    // 初始化WiFi CSI
    ESP_ERROR_CHECK(wifi_csi_init());
    
    // 启动SDIO
    sdio_slave_start();
    
    // 创建SDIO发送任务
    xTaskCreate(sdio_tx_task, "sdio_tx", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "C5 Slave initialized successfully");
    
    // 主循环 - 状态打印
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        ESP_LOGI(TAG, "Stats: CSI received=%lu, SDIO sent=%lu", 
                 csi_received_count, sdio_sent_count);
        
        // 检查WiFi连接状态
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "Connected to %s, RSSI: %d dBm", ap_info.ssid, ap_info.rssi);
        }
    }
}
