/**
 * UART通信模块头文件 (WTDKP4C5-S1与ESP32-C5开发板通信)
 */

#ifndef UART_COMM_H
#define UART_COMM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_PORT_NUM       UART_NUM_1
#define UART_BAUD_RATE      115200
#define UART_TX_PIN         11
#define UART_RX_PIN         10

// 命令类型
typedef enum {
    CMD_GET_STATUS = 0x01,      // 获取检测状态
    CMD_RESET_DETECTOR = 0x03,  // 重置检测器
    CMD_SET_THRESHOLD = 0x04,   // 设置阈值
} uart_cmd_type_t;

// 响应类型
typedef enum {
    RESP_STATUS = 0x02,         // 状态响应
    RESP_ACK = 0x05,            // 确认响应
} uart_resp_type_t;

// 命令结构
typedef struct {
    uint8_t type;
    uint8_t seq;
    uint16_t data_len;
    uint8_t data[128];
} uart_command_t;

// 响应结构
typedef struct {
    uint8_t type;
    uint8_t seq;
    uint16_t data_len;
    uint8_t data[128];
} uart_response_t;

// 初始化UART通信
esp_err_t uart_comm_init(void);

// 发送命令
esp_err_t uart_send_command(uart_cmd_type_t type, const uint8_t *data, uint16_t data_len);

// 接收响应
BaseType_t uart_receive_response(uart_response_t *resp, TickType_t wait_time);

// 计算CRC校验
uint8_t uart_calculate_crc(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* UART_COMM_H */
