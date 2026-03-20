/**
 * SDIO Host CSI模块（ESP32-P4）
 * 
 * 通过SDIO从板载C5接收CSI数据
 * SDIO引脚需要参考WTDKP4C5-S1原理图
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sdio_host_csi.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_types.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "SDIO_HOST";

// SDIO配置
#define SDIO_HOST_SLOT          SDMMC_HOST_SLOT_0
#define SDIO_FREQ_DEFAULT       SDMMC_FREQ_DEFAULT
#define SDIO_FREQ_HIGHSPEED     SDMMC_FREQ_HIGHSPEED
#define SDIO_BLOCK_SIZE         512

// CSI数据包结构（与C5共享）
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t sequence;
    uint32_t timestamp;
    int16_t rssi;
    uint8_t rate;
    uint8_t channel;
    uint8_t sig_mode;
    uint8_t reserved[3];
    uint16_t csi_len;
    int8_t csi_data[256];
} csi_packet_t;

#define CSI_MAGIC               0x43534944  // "CSID"

static struct {
    bool initialized;
    bool running;
    sdmmc_host_t host;
    sdmmc_card_t *card;
    QueueHandle_t rx_queue;
    TaskHandle_t rx_task_handle;
    sdio_host_csi_cb_t data_callback;
    uint32_t packets_received;
    uint32_t packets_dropped;
} host_ctx = {0};

// SDIO接收任务
static void sdio_rx_task(void *pvParameters)
{
    ESP_LOGI(TAG, "SDIO RX task started");
    
    uint8_t *rx_buffer = malloc(SDIO_BLOCK_SIZE);
    if (!rx_buffer) {
        ESP_LOGE(TAG, "Failed to allocate RX buffer");
        vTaskDelete(NULL);
        return;
    }
    
    while (host_ctx.running) {
        // 从SDIO读取数据
        size_t read_len = 0;
        esp_err_t ret = sdmmc_io_read_blocks(host_ctx.card, 1, 0, rx_buffer, 
                                              SDIO_BLOCK_SIZE);
        
        if (ret == ESP_OK && read_len >= sizeof(csi_packet_t)) {
            csi_packet_t *packet = (csi_packet_t*)rx_buffer;
            
            // 验证魔术字
            if (packet->magic == CSI_MAGIC) {
                host_ctx.packets_received++;
                
                // 转换为CSI原始数据结构
                csi_raw_data_t raw_data;
                memcpy(raw_data.csi_data, packet->csi_data, packet->csi_len);
                raw_data.rssi = packet->rssi;
                raw_data.rate = packet->rate;
                raw_data.timestamp = packet->timestamp;
                raw_data.sig_mode = packet->sig_mode;
                raw_data.channel = packet->channel;
                
                // 调用回调函数
                if (host_ctx.data_callback) {
                    host_ctx.data_callback(&raw_data);
                }
                
                ESP_LOGD(TAG, "Received CSI packet %lu", packet->sequence);
            } else {
                ESP_LOGW(TAG, "Invalid magic: 0x%08X", packet->magic);
            }
        } else if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SDIO read failed: %s", esp_err_to_name(ret));
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    free(rx_buffer);
    vTaskDelete(NULL);
}

esp_err_t sdio_host_csi_init(void)
{
    ESP_LOGI(TAG, "Initializing SDIO Host CSI...");
    
    if (host_ctx.initialized) {
        return ESP_OK;
    }
    
    // 配置SDIO主机
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDIO_HOST_SLOT;
    host.max_freq_khz = SDIO_FREQ_HIGHSPEED;
    host.flags = SDMMC_HOST_FLAG_4BIT;
    host_ctx.host = host;
    
    // 配置SDIO插槽（需要参考WTDKP4C5-S1原理图确定GPIO）
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.flags = 0;
    
    // 初始化SDMMC主机
    ESP_ERROR_CHECK(sdmmc_host_init());
    ESP_ERROR_CHECK(sdmmc_host_init_slot(SDIO_HOST_SLOT, &slot_config));
    
    // 分配卡结构
    host_ctx.card = malloc(sizeof(sdmmc_card_t));
    if (!host_ctx.card) {
        ESP_LOGE(TAG, "Failed to allocate card structure");
        return ESP_FAIL;
    }
    
    // 创建接收队列
    host_ctx.rx_queue = xQueueCreate(20, sizeof(csi_raw_data_t));
    if (!host_ctx.rx_queue) {
        ESP_LOGE(TAG, "Failed to create RX queue");
        free(host_ctx.card);
        return ESP_FAIL;
    }
    
    host_ctx.initialized = true;
    
    ESP_LOGI(TAG, "SDIO Host CSI initialized");
    return ESP_OK;
}

esp_err_t sdio_host_csi_start(void)
{
    if (!host_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (host_ctx.running) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting SDIO Host CSI...");
    
    // 初始化SDIO卡（等待C5就绪）
    ESP_LOGI(TAG, "Waiting for SDIO slave (C5)...");
    
    int retry = 0;
    esp_err_t ret;
    do {
        ret = sdmmc_card_init(&host_ctx.host, host_ctx.card);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Card init failed, retry %d...", retry);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        retry++;
    } while (ret != ESP_OK && retry < 10);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SDIO card");
        return ret;
    }
    
    ESP_LOGI(TAG, "SDIO card initialized successfully");
    
    // 启动接收任务
    host_ctx.running = true;
    xTaskCreate(sdio_rx_task, "sdio_rx", 4096, NULL, 5, &host_ctx.rx_task_handle);
    
    ESP_LOGI(TAG, "SDIO Host CSI started");
    return ESP_OK;
}

esp_err_t sdio_host_csi_stop(void)
{
    if (!host_ctx.running) {
        return ESP_OK;
    }
    
    host_ctx.running = false;
    
    if (host_ctx.rx_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "SDIO Host CSI stopped");
    return ESP_OK;
}

esp_err_t sdio_host_csi_register_callback(sdio_host_csi_cb_t cb)
{
    host_ctx.data_callback = cb;
    return ESP_OK;
}

void sdio_host_csi_get_stats(uint32_t *packets_received, uint32_t *packets_dropped)
{
    if (packets_received) *packets_received = host_ctx.packets_received;
    if (packets_dropped) *packets_dropped = host_ctx.packets_dropped;
}
