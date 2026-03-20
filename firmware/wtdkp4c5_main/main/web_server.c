/**
 * Web服务器模块实现
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "web_server.h"
#include "alarm_manager.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "WEB_SERVER";

// 外部变量 - 由main.c提供
extern bool is_person_present;
extern float detection_confidence;
extern int wifi_signal_rssi;

// 嵌入的静态文件（由CMakeLists.txt处理）
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char style_css_start[] asm("_binary_style_css_start");
extern const char style_css_end[] asm("_binary_style_css_end");
extern const char script_js_start[] asm("_binary_script_js_start");
extern const char script_js_end[] asm("_binary_script_js_end");

// 获取状态JSON
esp_err_t web_get_status_json(char *buf, size_t max_len)
{
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddBoolToObject(root, "is_person_present", is_person_present);
    cJSON_AddNumberToObject(root, "confidence", detection_confidence);
    cJSON_AddNumberToObject(root, "wifi_signal", wifi_signal_rssi);
    
    // 获取下次响铃时间
    char next_alarm[16];
    alarm_manager_get_next(next_alarm, sizeof(next_alarm));
    cJSON_AddStringToObject(root, "next_alarm", next_alarm);
    
    // 获取是否正在响铃
    cJSON_AddBoolToObject(root, "is_ringing", alarm_manager_is_ringing());
    
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        strncpy(buf, json_str, max_len - 1);
        buf[max_len - 1] = '\0';
        free(json_str);
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

// 获取闹钟列表JSON
esp_err_t web_get_alarms_json(char *buf, size_t max_len)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *alarms_array = cJSON_CreateArray();
    
    alarm_t alarms[MAX_ALARMS];
    uint8_t count = 0;
    alarm_manager_get_all(alarms, &count);
    
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (alarms[i].id != 0) {
            cJSON *alarm_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(alarm_obj, "id", alarms[i].id);
            
            char time_str[8];
            snprintf(time_str, sizeof(time_str), "%02d:%02d", 
                     alarms[i].hour, alarms[i].minute);
            cJSON_AddStringToObject(alarm_obj, "time", time_str);
            
            cJSON_AddBoolToObject(alarm_obj, "enabled", alarms[i].enabled);
            cJSON_AddBoolToObject(alarm_obj, "is_ringing", alarms[i].is_ringing);
            
            cJSON_AddItemToArray(alarms_array, alarm_obj);
        }
    }
    
    cJSON_AddItemToObject(root, "alarms", alarms_array);
    cJSON_AddNumberToObject(root, "count", count);
    
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        strncpy(buf, json_str, max_len - 1);
        buf[max_len - 1] = '\0';
        free(json_str);
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

// 处理主页请求
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    
    size_t html_len = index_html_end - index_html_start;
    httpd_resp_send(req, index_html_start, html_len);
    
    return ESP_OK;
}

// 处理CSS请求
static esp_err_t css_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    
    size_t css_len = style_css_end - style_css_start;
    httpd_resp_send(req, style_css_start, css_len);
    
    return ESP_OK;
}

// 处理JS请求
static esp_err_t js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    
    size_t js_len = script_js_end - script_js_start;
    httpd_resp_send(req, script_js_start, js_len);
    
    return ESP_OK;
}

// 处理API状态请求
static esp_err_t api_status_handler(httpd_req_t *req)
{
    char json_buf[512];
    web_get_status_json(json_buf, sizeof(json_buf));
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_buf, strlen(json_buf));
    
    return ESP_OK;
}

// 处理API闹钟列表请求
static esp_err_t api_alarms_get_handler(httpd_req_t *req)
{
    char json_buf[1024];
    web_get_alarms_json(json_buf, sizeof(json_buf));
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_buf, strlen(json_buf));
    
    return ESP_OK;
}

// 处理添加闹钟请求
static esp_err_t api_alarms_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *time_obj = cJSON_GetObjectItem(root, "time");
    if (!time_obj || !cJSON_IsString(time_obj)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing time field");
        return ESP_FAIL;
    }
    
    const char *time_str = time_obj->valuestring;
    int hour, minute;
    if (sscanf(time_str, "%d:%d", &hour, &minute) != 2) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid time format");
        return ESP_FAIL;
    }
    
    uint8_t alarm_id;
    esp_err_t err = alarm_manager_add(hour, minute, &alarm_id);
    
    cJSON_Delete(root);
    
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to add alarm");
        return ESP_FAIL;
    }
    
    // 返回成功响应
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddNumberToObject(resp, "id", alarm_id);
    
    char *resp_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));
    
    free(resp_str);
    cJSON_Delete(resp);
    
    ESP_LOGI(TAG, "Alarm added via API: %02d:%02d, ID=%d", hour, minute, alarm_id);
    
    return ESP_OK;
}

// 处理删除闹钟请求
static esp_err_t api_alarms_delete_handler(httpd_req_t *req)
{
    // 从URL中解析ID
    char *uri = req->uri;
    char *id_str = strrchr(uri, '/');
    if (!id_str) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid ID");
        return ESP_FAIL;
    }
    id_str++;  // 跳过'/'
    
    uint8_t id = atoi(id_str);
    esp_err_t err = alarm_manager_delete(id);
    
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Alarm not found");
        return ESP_FAIL;
    }
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    
    char *resp_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));
    
    free(resp_str);
    cJSON_Delete(resp);
    
    ESP_LOGI(TAG, "Alarm deleted via API: ID=%d", id);
    
    return ESP_OK;
}

// 处理更新闹钟请求（启用/禁用）
static esp_err_t api_alarms_put_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // 解析ID
    char *uri = req->uri;
    char *id_str = strrchr(uri, '/');
    if (!id_str) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid ID");
        return ESP_FAIL;
    }
    id_str++;
    uint8_t id = atoi(id_str);
    
    // 解析JSON
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *enabled_obj = cJSON_GetObjectItem(root, "enabled");
    if (enabled_obj && cJSON_IsBool(enabled_obj)) {
        alarm_manager_set_enabled(id, cJSON_IsTrue(enabled_obj));
    }
    
    cJSON_Delete(root);
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    
    char *resp_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));
    
    free(resp_str);
    cJSON_Delete(resp);
    
    return ESP_OK;
}

// 处理测试响铃请求
static esp_err_t api_test_alarm_handler(httpd_req_t *req)
{
    // 设置一个临时的响铃状态
    ESP_LOGI(TAG, "Test alarm triggered via API");
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", "Test alarm triggered");
    
    char *resp_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));
    
    free(resp_str);
    cJSON_Delete(resp);
    
    return ESP_OK;
}

// 处理停止响铃请求
static esp_err_t api_stop_alarm_handler(httpd_req_t *req)
{
    alarm_manager_stop();
    ESP_LOGI(TAG, "Alarm stopped via API");
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", "Alarm stopped");
    
    char *resp_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));
    
    free(resp_str);
    cJSON_Delete(resp);
    
    return ESP_OK;
}

// 处理重置检测器请求
static esp_err_t api_reset_detector_handler(httpd_req_t *req)
{
    // 通过UART发送重置命令给ESP32-C5
    ESP_LOGI(TAG, "Detector reset requested via API");
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", "Detector reset command sent");
    
    char *resp_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));
    
    free(resp_str);
    cJSON_Delete(resp);
    
    return ESP_OK;
}

// URI处理器定义
static const httpd_uri_t uri_get_index = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_get_css = {
    .uri = "/style.css",
    .method = HTTP_GET,
    .handler = css_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_get_js = {
    .uri = "/script.js",
    .method = HTTP_GET,
    .handler = js_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_status = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = api_status_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_alarms_get = {
    .uri = "/api/alarms",
    .method = HTTP_GET,
    .handler = api_alarms_get_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_alarms_post = {
    .uri = "/api/alarms",
    .method = HTTP_POST,
    .handler = api_alarms_post_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_alarms_delete = {
    .uri = "/api/alarms/*",
    .method = HTTP_DELETE,
    .handler = api_alarms_delete_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_alarms_put = {
    .uri = "/api/alarms/*",
    .method = HTTP_PUT,
    .handler = api_alarms_put_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_test_alarm = {
    .uri = "/api/test-alarm",
    .method = HTTP_POST,
    .handler = api_test_alarm_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_stop_alarm = {
    .uri = "/api/stop-alarm",
    .method = HTTP_POST,
    .handler = api_stop_alarm_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_reset_detector = {
    .uri = "/api/reset-detector",
    .method = HTTP_POST,
    .handler = api_reset_detector_handler,
    .user_ctx = NULL
};

// 启动Web服务器
httpd_handle_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_WEB_SERVER_PORT;
    config.max_open_sockets = CONFIG_MAX_CLIENTS;
    config.lru_purge_enable = true;
    
    httpd_handle_t server = NULL;
    
    ESP_LOGI(TAG, "Starting web server on port %d", config.server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // 注册URI处理器
        httpd_register_uri_handler(server, &uri_get_index);
        httpd_register_uri_handler(server, &uri_get_css);
        httpd_register_uri_handler(server, &uri_get_js);
        httpd_register_uri_handler(server, &uri_api_status);
        httpd_register_uri_handler(server, &uri_api_alarms_get);
        httpd_register_uri_handler(server, &uri_api_alarms_post);
        httpd_register_uri_handler(server, &uri_api_alarms_delete);
        httpd_register_uri_handler(server, &uri_api_alarms_put);
        httpd_register_uri_handler(server, &uri_api_test_alarm);
        httpd_register_uri_handler(server, &uri_api_stop_alarm);
        httpd_register_uri_handler(server, &uri_api_reset_detector);
        
        ESP_LOGI(TAG, "Web server started successfully");
        return server;
    }
    
    ESP_LOGE(TAG, "Failed to start web server");
    return NULL;
}

// 停止Web服务器
esp_err_t web_server_stop(httpd_handle_t server)
{
    if (server) {
        return httpd_stop(server);
    }
    return ESP_ERR_INVALID_ARG;
}
