/**
 * UART通信模块头文件
 * 用于ESP32-C5与WTDKP4C5-S1之间的通信
 */

#ifndef UART_COMM_H
#define UART_COMM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "person_detector.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_PORT_NUM       UART_NUM_1
#define UART_BAUD_RATE      115200
#define UART_TX_PIN         5
#define UART_RX_PIN         4
#define UART_BUF_SIZE       256
#define UART_CMD_QUEUE_SIZE 5

// 命令类型
typedef enum {
    CMD_GET_STATUS = 0x01,      // 获取检测状态
    CMD_STATUS_RESPONSE = 0x02, // 状态响应
    CMD_RESET_DETECTOR = 0x03,  // 重置检测器
    CMD_SET_THRESHOLD = 0x04,   // 设置阈值
    CMD_ACK = 0x05,             // 确认响应
} uart_cmd_type_t;

// 命令结构
typedef struct {
    uint8_t type;       // 命令类型
    uint8_t seq;        // 序列号
    uint16_t data_len;  // 数据长度
    uint8_t data[128];  // 数据
} uart_command_t;

// 初始化UART通信
esp_err_t uart_comm_init(void);

// 发送检测状态
esp_err_t uart_send_detection_state(const person_detection_state_t *state);

// 接收命令
BaseType_t uart_receive_command(uart_command_t *cmd, TickType_t wait_time);

// 发送确认响应
esp_err_t uart_send_ack(uart_cmd_type_t ack_type);

// 计算CRC校验
uint8_t uart_calculate_crc(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* UART_COMM_H */
