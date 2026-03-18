/**
 * SDIO Slave CSI实现
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sdio_slave_csi.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "driver/sdio_slave.h"
#include "soc/sdio_slave_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "SDIO_SLAVE";

// SDIO配置
#define SDIO_SLAVE_SLOT     SDSPI_HOST_SLOT_0
#define SDIO_FUNC_NUM       1

// 引脚定义（ESP32-C5）
#define SDIO_CMD_GPIO       10
#define SDIO_CLK_GPIO       9
#define SDIO_D0_GPIO        8
#define SDIO_D1_GPIO        7
#define SDIO_D2_GPIO        14
#define SDIO_D3_GPIO        13

static struct {
    bool initialized;
    bool running;
    QueueHandle_t csi_queue;
    TaskHandle_t sdio_task_handle;
    uint32_t csi_received;
    uint32_t sdio_sent;
    uint32_t sequence;
} slave_ctx = {0};

// WiFi CSI回调
static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *data)
{
    if (!data || !data->buf) {
        return;
    }
    
    // 分配数据包
    csi_sdio_packet_t *packet = malloc(sizeof(csi_sdio_packet_t));
    if (!packet) {
        ESP_LOGE(TAG, "Failed to allocate packet");
        return;
    }
    
    // 填充数据
    packet->magic = 0x43534944;  // "CSID"
    packet->sequence = slave_ctx.sequence++;
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
    if (len > CSI_DATA_SIZE) len = CSI_DATA_SIZE;
    packet->csi_len = len;
    memcpy(packet->csi_data, data->buf, len);
    
    // 发送到队列
    if (xQueueSend(slave_ctx.csi_queue, &packet, 0) != pdTRUE) {
        free(packet);
        slave_ctx.csi_received++;  // 计入接收但丢弃
    } else {
        slave_ctx.csi_received++;
    }
}

// SDIO发送任务
static void sdio_tx_task(void *pvParameters)
{
    ESP_LOGI(TAG, "SDIO TX task started");
    
    csi_sdio_packet_t *packet;
    
    while (slave_ctx.running) {
        if (xQueueReceive(slave_ctx.csi_queue, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            // 通过SDIO发送给P4
            // 使用sdio_slave_transmit或类似的API
            
            // 简化的发送（实际应该使用SDIO slave API）
            sdio_slave_buf_handle_t buf_handle = sdio_slave_recv_register_buf((uint8_t*)packet);
            if (buf_handle) {
                esp_err_t ret = sdio_slave_send_queue(buf_handle, sizeof(csi_sdio_packet_t), 
                                                       NULL, portMAX_DELAY);
                if (ret == ESP_OK) {
                    slave_ctx.sdio_sent++;
                    ESP_LOGD(TAG, "Sent CSI packet %lu via SDIO", packet->sequence);
                } else {
                    ESP_LOGE(TAG, "Failed to send via SDIO: %s", esp_err_to_name(ret));
                    free(packet);
                }
            } else {
                free(packet);
            }
        }
        
        // 定期打印统计
        static uint32_t last_print = 0;
        if (xTaskGetTickCount() - last_print > pdMS_TO_TICKS(10000)) {
            ESP_LOGI(TAG, "Stats: CSI received=%lu, SDIO sent=%lu",
                     slave_ctx.csi_received, slave_ctx.sdio_sent);
            last_print = xTaskGetTickCount();
        }
    }
    
    vTaskDelete(NULL);
}

esp_err_t sdio_slave_csi_init(void)
{
    ESP_LOGI(TAG, "Initializing SDIO Slave CSI...");
    
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
    ESP_LOGI(TAG, "  CMD: GPIO%d (fixed)", SDIO_SLAVE_SLOT0_IOMUX_PIN_NUM_CMD);
    ESP_LOGI(TAG, "  CLK: GPIO%d (fixed)", SDIO_SLAVE_SLOT0_IOMUX_PIN_NUM_CLK);
    ESP_LOGI(TAG, "  D0: GPIO%d (fixed)", SDIO_SLAVE_SLOT0_IOMUX_PIN_NUM_D0);
    ESP_LOGI(TAG, "  D1: GPIO%d (fixed)", SDIO_SLAVE_SLOT0_IOMUX_PIN_NUM_D1);
    ESP_LOGI(TAG, "  D2: GPIO%d (fixed)", SDIO_SLAVE_SLOT0_IOMUX_PIN_NUM_D2);
    ESP_LOGI(TAG, "  D3: GPIO%d (fixed)", SDIO_SLAVE_SLOT0_IOMUX_PIN_NUM_D3);
    
    esp_err_t ret = sdio_slave_initialize(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SDIO slave: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 创建队列
    slave_ctx.csi_queue = xQueueCreate(CSI_QUEUE_SIZE, sizeof(csi_sdio_packet_t*));
    if (!slave_ctx.csi_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        sdio_slave_deinit();
        return ESP_FAIL;
    }
    
    slave_ctx.initialized = true;
    
    ESP_LOGI(TAG, "SDIO Slave CSI initialized");
    
    return ESP_OK;
}

esp_err_t sdio_slave_csi_start(const char *ssid, const char *password)
{
    if (!slave_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (slave_ctx.running) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting SDIO Slave CSI...");
    ESP_LOGI(TAG, "Connecting to AP: %s", ssid);
    
    // 配置WiFi为STA模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = {0},
            .password = {0},
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    
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
        // .acquire_csi_he_stbc not available on ESP32-C5
        .val_scale_cfg = 0,
        .dump_ack_en = false,
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
    
    // 启动SDIO
    sdio_slave_start();
    
    // 启动发送任务
    slave_ctx.running = true;
    xTaskCreate(sdio_tx_task, "sdio_tx", 4096, NULL, 5, &slave_ctx.sdio_task_handle);
    
    ESP_LOGI(TAG, "SDIO Slave CSI started");
    return ESP_OK;
}

esp_err_t sdio_slave_csi_stop(void)
{
    if (!slave_ctx.running) {
        return ESP_OK;
    }
    
    slave_ctx.running = false;
    
    if (slave_ctx.sdio_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    sdio_slave_stop();
    ESP_ERROR_CHECK(esp_wifi_stop());
    
    ESP_LOGI(TAG, "SDIO Slave CSI stopped");
    return ESP_OK;
}

void sdio_slave_csi_get_stats(uint32_t *csi_received, uint32_t *sdio_sent)
{
    if (csi_received) *csi_received = slave_ctx.csi_received;
    if (sdio_sent) *sdio_sent = slave_ctx.sdio_sent;
}
