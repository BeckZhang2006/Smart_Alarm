/**
 * 闹钟管理模块头文件
 */

#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_ALARMS 10
#define ALARM_ID_INVALID 0xFF

// 闹钟结构
typedef struct {
    uint8_t id;
    uint8_t hour;
    uint8_t minute;
    bool enabled;
    bool is_ringing;
    uint32_t snooze_until;  // 贪睡结束时间（tick count）
} alarm_t;

// 初始化闹钟管理器
esp_err_t alarm_manager_init(void);

// 添加闹钟
esp_err_t alarm_manager_add(uint8_t hour, uint8_t minute, uint8_t *out_id);

// 删除闹钟
esp_err_t alarm_manager_delete(uint8_t id);

// 启用/禁用闹钟
esp_err_t alarm_manager_set_enabled(uint8_t id, bool enabled);

// 获取所有闹钟
esp_err_t alarm_manager_get_all(alarm_t *alarms, uint8_t *count);

// 检查是否有闹钟需要触发
bool alarm_manager_check_trigger(void);

// 贪睡功能（延迟指定分钟数）
esp_err_t alarm_manager_snooze(uint8_t minutes);

// 停止当前响铃
esp_err_t alarm_manager_stop(void);

// 获取下次响铃时间
esp_err_t alarm_manager_get_next(char *time_str, size_t max_len);

// 清除所有闹钟
esp_err_t alarm_manager_clear_all(void);

// 检查指定ID的闹钟是否存在
bool alarm_manager_exists(uint8_t id);

// 获取闹钟状态（是否正在响铃）
bool alarm_manager_is_ringing(void);

#ifdef __cplusplus
}
#endif

#endif /* ALARM_MANAGER_H */
