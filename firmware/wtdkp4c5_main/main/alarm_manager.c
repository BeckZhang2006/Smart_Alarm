/**
 * 闹钟管理模块实现
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "alarm_manager.h"
#include "storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ALARM_MGR";

static alarm_t alarm_list[MAX_ALARMS];
static uint8_t alarm_count = 0;
static uint8_t next_id = 1;
static bool is_ringing = false;

// 从存储加载闹钟
static esp_err_t load_alarms_from_storage(void)
{
    size_t len = sizeof(alarm_list);
    esp_err_t ret = storage_read("alarms", alarm_list, &len);
    
    if (ret == ESP_OK) {
        // 计算闹钟数量和下一个ID
        alarm_count = 0;
        next_id = 1;
        for (int i = 0; i < MAX_ALARMS; i++) {
            if (alarm_list[i].id != 0) {
                alarm_count++;
                if (alarm_list[i].id >= next_id) {
                    next_id = alarm_list[i].id + 1;
                }
            }
        }
        ESP_LOGI(TAG, "Loaded %d alarms from storage", alarm_count);
    } else {
        // 初始化空的闹钟列表
        memset(alarm_list, 0, sizeof(alarm_list));
        ESP_LOGI(TAG, "No alarms in storage, initialized empty list");
    }
    
    return ESP_OK;
}

// 保存闹钟到存储
static esp_err_t save_alarms_to_storage(void)
{
    return storage_write("alarms", alarm_list, sizeof(alarm_list));
}

// 获取当前时间（时和分）
static void get_current_time(uint8_t *hour, uint8_t *minute)
{
    // 在实际应用中，这里应该使用RTC或NTP同步的时间
    // 这里使用简单的tick count模拟
    uint32_t total_minutes = (xTaskGetTickCount() / configTICK_RATE_HZ) / 60;
    *hour = (total_minutes / 60) % 24;
    *minute = total_minutes % 60;
}

// 计算两个时间的分钟差
static int16_t time_diff_minutes(uint8_t h1, uint8_t m1, uint8_t h2, uint8_t m2)
{
    int16_t t1 = h1 * 60 + m1;
    int16_t t2 = h2 * 60 + m2;
    return t2 - t1;
}

esp_err_t alarm_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing alarm manager...");
    
    memset(alarm_list, 0, sizeof(alarm_list));
    alarm_count = 0;
    next_id = 1;
    is_ringing = false;
    
    // 从存储加载闹钟
    load_alarms_from_storage();
    
    ESP_LOGI(TAG, "Alarm manager initialized");
    return ESP_OK;
}

esp_err_t alarm_manager_add(uint8_t hour, uint8_t minute, uint8_t *out_id)
{
    if (hour > 23 || minute > 59) {
        ESP_LOGE(TAG, "Invalid time: %02d:%02d", hour, minute);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (alarm_count >= MAX_ALARMS) {
        ESP_LOGE(TAG, "Maximum number of alarms reached");
        return ESP_ERR_NO_MEM;
    }
    
    // 查找空位
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (alarm_list[i].id == 0) {
            alarm_list[i].id = next_id++;
            alarm_list[i].hour = hour;
            alarm_list[i].minute = minute;
            alarm_list[i].enabled = true;
            alarm_list[i].is_ringing = false;
            alarm_list[i].snooze_until = 0;
            
            alarm_count++;
            
            if (out_id) {
                *out_id = alarm_list[i].id;
            }
            
            ESP_LOGI(TAG, "Alarm added: ID=%d, Time=%02d:%02d", 
                     alarm_list[i].id, hour, minute);
            
            // 保存到存储
            save_alarms_to_storage();
            
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NO_MEM;
}

esp_err_t alarm_manager_delete(uint8_t id)
{
    if (id == ALARM_ID_INVALID) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (alarm_list[i].id == id) {
            ESP_LOGI(TAG, "Alarm deleted: ID=%d", id);
            memset(&alarm_list[i], 0, sizeof(alarm_t));
            alarm_count--;
            save_alarms_to_storage();
            return ESP_OK;
        }
    }
    
    ESP_LOGW(TAG, "Alarm not found: ID=%d", id);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t alarm_manager_set_enabled(uint8_t id, bool enabled)
{
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (alarm_list[i].id == id) {
            alarm_list[i].enabled = enabled;
            ESP_LOGI(TAG, "Alarm %d %s", id, enabled ? "enabled" : "disabled");
            save_alarms_to_storage();
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t alarm_manager_get_all(alarm_t *alarms, uint8_t *count)
{
    if (!alarms || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(alarms, alarm_list, sizeof(alarm_list));
    *count = alarm_count;
    
    return ESP_OK;
}

bool alarm_manager_check_trigger(void)
{
    uint8_t current_hour, current_minute;
    get_current_time(&current_hour, &current_minute);
    
    uint32_t current_tick = xTaskGetTickCount();
    
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (alarm_list[i].id != 0 && alarm_list[i].enabled && !alarm_list[i].is_ringing) {
            // 检查是否在贪睡时间内
            if (current_tick < alarm_list[i].snooze_until) {
                continue;
            }
            
            // 检查时间是否匹配
            if (alarm_list[i].hour == current_hour && 
                alarm_list[i].minute == current_minute) {
                // 检查是否已经在当前分钟触发过
                static uint32_t last_trigger_minute = 0xFFFFFFFF;
                uint32_t current_total_minutes = current_hour * 60 + current_minute;
                
                if (current_total_minutes != last_trigger_minute) {
                    last_trigger_minute = current_total_minutes;
                    alarm_list[i].is_ringing = true;
                    is_ringing = true;
                    ESP_LOGI(TAG, "Alarm triggered: ID=%d", alarm_list[i].id);
                    return true;
                }
            }
        }
    }
    
    return false;
}

esp_err_t alarm_manager_snooze(uint8_t minutes)
{
    if (minutes == 0 || minutes > 60) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Snoozing for %d minutes", minutes);
    
    uint32_t snooze_ticks = pdMS_TO_TICKS(minutes * 60 * 1000);
    uint32_t current_tick = xTaskGetTickCount();
    
    // 停止当前响铃的所有闹钟，并设置贪睡时间
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (alarm_list[i].id != 0 && alarm_list[i].is_ringing) {
            alarm_list[i].is_ringing = false;
            alarm_list[i].snooze_until = current_tick + snooze_ticks;
            ESP_LOGI(TAG, "Alarm %d snoozed until tick %lu", 
                     alarm_list[i].id, alarm_list[i].snooze_until);
        }
    }
    
    is_ringing = false;
    return ESP_OK;
}

esp_err_t alarm_manager_stop(void)
{
    ESP_LOGI(TAG, "Stopping all alarms");
    
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (alarm_list[i].id != 0 && alarm_list[i].is_ringing) {
            alarm_list[i].is_ringing = false;
            alarm_list[i].snooze_until = 0;  // 清除贪睡状态
            ESP_LOGI(TAG, "Alarm %d stopped", alarm_list[i].id);
        }
    }
    
    is_ringing = false;
    return ESP_OK;
}

esp_err_t alarm_manager_get_next(char *time_str, size_t max_len)
{
    if (!time_str || max_len < 6) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t current_hour, current_minute;
    get_current_time(&current_hour, &current_minute);
    
    int16_t min_diff = 24 * 60;  // 最大分钟差
    uint8_t next_hour = 0xFF;
    uint8_t next_minute = 0xFF;
    
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (alarm_list[i].id != 0 && alarm_list[i].enabled) {
            int16_t diff = time_diff_minutes(current_hour, current_minute,
                                              alarm_list[i].hour, alarm_list[i].minute);
            if (diff <= 0) {
                diff += 24 * 60;  // 跨天
            }
            
            if (diff < min_diff) {
                min_diff = diff;
                next_hour = alarm_list[i].hour;
                next_minute = alarm_list[i].minute;
            }
        }
    }
    
    if (next_hour != 0xFF) {
        snprintf(time_str, max_len, "%02d:%02d", next_hour, next_minute);
        return ESP_OK;
    } else {
        strncpy(time_str, "--:--", max_len);
        return ESP_OK;
    }
}

esp_err_t alarm_manager_clear_all(void)
{
    ESP_LOGI(TAG, "Clearing all alarms");
    memset(alarm_list, 0, sizeof(alarm_list));
    alarm_count = 0;
    next_id = 1;
    is_ringing = false;
    save_alarms_to_storage();
    return ESP_OK;
}

bool alarm_manager_exists(uint8_t id)
{
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (alarm_list[i].id == id) {
            return true;
        }
    }
    return false;
}

bool alarm_manager_is_ringing(void)
{
    return is_ringing;
}
