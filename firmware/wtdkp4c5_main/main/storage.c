/**
 * 存储模块实现
 * 使用NVS (Non-Volatile Storage) 存储配置数据
 */

#include <stdio.h>
#include <string.h>
#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "STORAGE";
static const char *NVS_NAMESPACE = "smart_alarm";

static nvs_handle_t g_nvs_handle = 0;

esp_err_t storage_init(void)
{
    ESP_LOGI(TAG, "Initializing storage...");
    
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Storage initialized");
    return ESP_OK;
}

esp_err_t storage_write(const char *key, const void *data, size_t len)
{
    if (!key || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_nvs_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = nvs_set_blob(g_nvs_handle, key, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write key '%s': %s", key, esp_err_to_name(ret));
        return ret;
    }
    
    ret = nvs_commit(g_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "Written %d bytes to key '%s'", len, key);
    return ESP_OK;
}

esp_err_t storage_read(const char *key, void *data, size_t *len)
{
    if (!key || !data || !len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_nvs_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t required_len = 0;
    esp_err_t ret = nvs_get_blob(g_nvs_handle, key, NULL, &required_len);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Key '%s' not found", key);
        }
        return ret;
    }
    
    if (*len < required_len) {
        *len = required_len;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    
    *len = required_len;
    ret = nvs_get_blob(g_nvs_handle, key, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read key '%s': %s", key, esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "Read %d bytes from key '%s'", *len, key);
    return ESP_OK;
}

esp_err_t storage_delete(const char *key)
{
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_nvs_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = nvs_erase_key(g_nvs_handle, key);
    if (ret != ESP_OK) {
        if (ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to delete key '%s': %s", key, esp_err_to_name(ret));
        }
        return ret;
    }
    
    ret = nvs_commit(g_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Deleted key '%s'", key);
    return ESP_OK;
}

esp_err_t storage_clear(void)
{
    if (!g_nvs_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = nvs_erase_all(g_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear storage: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = nvs_commit(g_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Storage cleared");
    return ESP_OK;
}
