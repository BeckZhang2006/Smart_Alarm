/**
 * WTDKP4C5-S1 ESP32-P4 主控程序
 * 
 * 功能：
 * 1. SDIO Host - 从板载C5接收CSI数据
 * 2. 边缘计算 - CSI数据解析、特征提取、人体检测
 * 3. 闹钟管理 - 多闹钟、贪睡逻辑
 * 4. Web服务器 - 配置界面和状态监控
 * 
 * 硬件：WTDKP4C5-S1开发板
 * 
 * SDIO与C5连接：
 * P4通过SDIO与板上C5通信，接收CSI数据
 * 
 * WiFi通过ESP-Hosted使用板载C5作为协处理器
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 手动定义 WiFi Remote 缺失的配置（Kconfig 问题绕过）
#define CONFIG_WIFI_RMT_STATIC_RX_BUFFER_NUM 16
#define CONFIG_WIFI_RMT_DYNAMIC_RX_BUFFER_NUM 64
#define CONFIG_WIFI_RMT_TX_BUFFER_TYPE 1
#define CONFIG_WIFI_RMT_DYNAMIC_RX_MGMT_BUF 0
#define CONFIG_WIFI_RMT_ESPNOW_MAX_ENCRYPT_NUM 7

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
/* BSP not available: #include "bsp/wtdkp4c5_s1_board.h" */

#include "csi_processor.h"
#include "sdio_host_csi.h"
#include "alarm_manager.h"
#include "web_server.h"

static const char *WIFI_TAG = "WIFI";

static const char *TAG = "P4_MAIN";

// GPIO定义
#define BUZZER_GPIO             8
#define LED_STATUS_GPIO         3
#define BUTTON_SNOOZE_GPIO      2
#define BUTTON_STOP_GPIO        1

// 任务句柄
static TaskHandle_t alarm_task_handle = NULL;
static TaskHandle_t csi_task_handle = NULL;
static TaskHandle_t web_task_handle = NULL;

// 系统状态
static struct {
    bool is_alarm_ringing;
    bool is_person_present;
    float detection_confidence;
    uint32_t last_detection_time;
} system_state = {0};

// 外部变量 - 供web_server.c使用
bool is_person_present = false;
float detection_confidence = 0.0f;
int wifi_signal_rssi = -50;

// WiFi事件处理函数
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(WIFI_TAG, "Station %02x:%02x:%02x:%02x:%02x:%02x joined, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(WIFI_TAG, "Station %02x:%02x:%02x:%02x:%02x:%02x left, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
    }
}

// 初始化WiFi AP模式
static esp_err_t wifi_init_softap(void)
{
    ESP_LOGI(WIFI_TAG, "Initializing WiFi AP...");
    
    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 手动创建默认WiFi AP接口
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_WIFI_AP();
    esp_netif_t *netif = esp_netif_new(&cfg);
    if (netif == NULL) {
        ESP_LOGE(WIFI_TAG, "Failed to create AP netif");
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(esp_netif_attach_wifi_ap(netif));
    
    // 注册WiFi事件处理
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    
    // 初始化WiFi
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    
    // 配置AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .ssid_len = strlen(CONFIG_ESP_WIFI_SSID),
            .channel = 1,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    
    if (strlen(CONFIG_ESP_WIFI_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(WIFI_TAG, "WiFi AP started: %s", CONFIG_ESP_WIFI_SSID);
    
    return ESP_OK;
}

// CSI数据回调（由SDIO接收触发）
static void csi_data_received_cb(const csi_raw_data_t *raw_data)
{
    // 调用CSI处理器进行边缘计算
    csi_features_t features;
    if (csi_processor_extract_features(raw_data, &features) == ESP_OK) {
        // 获取检测结果（由处理器内部自动更新）
        csi_detection_result_t result = csi_processor_get_result();
        system_state.is_person_present = result.person_present;
        system_state.detection_confidence = result.confidence;
        system_state.last_detection_time = xTaskGetTickCount();
        
        // 更新外部变量（供web_server使用）
        is_person_present = result.person_present;
        detection_confidence = result.confidence;
        
        ESP_LOGD(TAG, "Detection: person=%s, confidence=%.2f%%",
                 result.person_present ? "YES" : "NO",
                 result.confidence * 100);
    }
}

// 蜂鸣器初始化
static void buzzer_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);
    
    ledc_channel_config_t channel_conf = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&channel_conf);
}

// 播放闹钟声音
static void play_alarm_sound(bool play)
{
    if (play) {
        static uint32_t pattern = 0;
        uint32_t duty = (pattern % 4 < 2) ? 512 : 0;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        pattern++;
    } else {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
}

// 按钮中断处理
static void IRAM_ATTR button_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    
    if (gpio_num == BUTTON_SNOOZE_GPIO) {
        // 贪睡5分钟
        alarm_manager_snooze(5);
    } else if (gpio_num == BUTTON_STOP_GPIO) {
        // 停止闹钟
        alarm_manager_stop();
    }
}

// 按钮初始化
static void buttons_init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_SNOOZE_GPIO) | (1ULL << BUTTON_STOP_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);
    
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_SNOOZE_GPIO, button_isr_handler, (void*)BUTTON_SNOOZE_GPIO);
    gpio_isr_handler_add(BUTTON_STOP_GPIO, button_isr_handler, (void*)BUTTON_STOP_GPIO);
}

// 闹钟处理任务
static void alarm_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Alarm task started");
    
    while (1) {
        // 检查是否到达闹钟时间
        if (alarm_manager_check_trigger()) {
            system_state.is_alarm_ringing = true;
            ESP_LOGI(TAG, "Alarm triggered!");
        }
        
        // 如果正在响铃
        if (system_state.is_alarm_ringing) {
            play_alarm_sound(true);
            gpio_set_level(LED_STATUS_GPIO, (xTaskGetTickCount() / 250) % 2);
            
            // 检测人体存在
            if (system_state.is_person_present) {
                ESP_LOGI(TAG, "Person detected in bed, waiting for 5min snooze...");
                vTaskDelay(pdMS_TO_TICKS(60000)); // 等待1分钟
                
                // 如果人还在床上，自动贪睡
                if (system_state.is_person_present) {
                    alarm_manager_snooze(5);
                    system_state.is_alarm_ringing = false;
                    play_alarm_sound(false);
                    ESP_LOGI(TAG, "Auto snooze triggered - person still in bed");
                }
            }
        } else {
            play_alarm_sound(false);
            gpio_set_level(LED_STATUS_GPIO, 0);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// CSI处理任务
static void csi_task(void *pvParameters)
{
    ESP_LOGI(TAG, "CSI task started");
    
    // 初始化SDIO Host
    ESP_ERROR_CHECK(sdio_host_csi_init());
    
    // 注册CSI数据回调
    ESP_ERROR_CHECK(sdio_host_csi_register_callback(csi_data_received_cb));
    
    // 启动SDIO Host
    ESP_ERROR_CHECK(sdio_host_csi_start());
    
    // 初始化CSI处理器
    ESP_ERROR_CHECK(csi_processor_init());
    ESP_ERROR_CHECK(csi_processor_start());
    
    ESP_LOGI(TAG, "CSI processing started");
    
    // 定期打印统计
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        uint32_t sdio_rx, sdio_drop;
        sdio_host_csi_get_stats(&sdio_rx, &sdio_drop);
        
        uint32_t csi_samples, detections;
        csi_processor_get_stats(&csi_samples, &detections);
        
        ESP_LOGI(TAG, "Stats: SDIO rx=%lu, CSI samples=%lu, detections=%lu",
                 sdio_rx, csi_samples, detections);
    }
}

// Web服务器任务
static void web_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting web server...");
    
    httpd_handle_t server = web_server_start();
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to start web server");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Web server started on port 80");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "Smart Alarm System Starting");
    ESP_LOGI(TAG, "Board: WTDKP4C5-S1 (ESP32-P4 + ESP32-C5)");
    ESP_LOGI(TAG, "======================================");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 初始化WiFi（必须在创建web任务之前）
    ESP_ERROR_CHECK(wifi_init_softap());
    
    // 初始化各模块
    ESP_ERROR_CHECK(alarm_manager_init());
    
    // 初始化硬件
    gpio_reset_pin(LED_STATUS_GPIO);
    gpio_set_direction(LED_STATUS_GPIO, GPIO_MODE_OUTPUT);
    buzzer_init();
    buttons_init();
    
    // 初始化BSP显示（如果有LCD）
    // bsp_display_start_with_config(...);
    
    // 创建任务（延迟创建web任务，确保网络已准备好）
    xTaskCreate(alarm_task, "alarm_task", 4096, NULL, 5, &alarm_task_handle);
    xTaskCreate(csi_task, "csi_task", 8192, NULL, 4, &csi_task_handle);
    
    // 给WiFi一些时间完全启动
    vTaskDelay(pdMS_TO_TICKS(500));
    xTaskCreate(web_task, "web_task", 8192, NULL, 3, &web_task_handle);
    
    ESP_LOGI(TAG, "All tasks created, system running...");
    
    // 主循环
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
