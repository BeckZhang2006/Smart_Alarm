/**
 * 人体检测模块头文件
 * 基于WiFi CSI数据检测人体存在
 */

#ifndef PERSON_DETECTOR_H
#define PERSON_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "csi_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DETECTION_BUFFER_SIZE 20
#define DEFAULT_DETECTION_THRESHOLD 30.0f
#define DEFAULT_CONFIDENCE_THRESHOLD 0.6f

// 检测状态结构
typedef struct {
    bool is_person_present;     // 是否检测到人体
    float confidence;           // 置信度 (0.0 - 1.0)
    uint32_t last_update_time;  // 最后更新时间
} person_detection_state_t;

// 初始化人体检测器
esp_err_t person_detector_init(void);

// 更新检测状态
esp_err_t person_detector_update(const csi_processed_data_t *csi_data, 
                                  person_detection_state_t *state);

// 设置检测阈值
void person_detector_set_threshold(float threshold);

// 获取当前检测阈值
float person_detector_get_threshold(void);

// 重置检测器
void person_detector_reset(void);

// 获取历史变化方差（用于判断运动）
float person_detector_get_variance(void);

#ifdef __cplusplus
}
#endif

#endif /* PERSON_DETECTOR_H */
