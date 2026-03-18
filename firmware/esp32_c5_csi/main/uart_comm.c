/**
 * UART通信模块实现
 * 协议格式： [HEAD][TYPE][SEQ][LEN_L][LEN_H][DATA...][CRC][TAIL]
 *           0xAA  1B    1B   1B     1B     NB      1B   0x55
 */

#include <stdio.h>
#include <string.h>
#include "uart_comm.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "UART_COMM";

#define FRAME_HEAD  0xAA
#define FRAME_TAIL  0x55
#define FRAME_MIN_LEN 6

static QueueHandle_t uart_queue = NULL;
static uint8_t seq_number = 0;

// 计算CRC8校验
uint8_t uart_calculate_crc(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0xFF;
    
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

// 发送数据帧
static esp_err_t send_frame(uart_cmd_type_t type, const uint8_t *data, uint16_t data_len)
{
    if (data_len > 128) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t frame[256];
    uint16_t frame_len = 0;
    
    // 帧头
    frame[frame_len++] = FRAME_HEAD;
    
    // 命令类型
    frame[frame_len++] = (uint8_t)type;
    
    // 序列号
    frame[frame_len++] = seq_number++;
    
    // 数据长度（小端）
    frame[frame_len++] = (uint8_t)(data_len & 0xFF);
    frame[frame_len++] = (uint8_t)((data_len >> 8) & 0xFF);
    
    // 数据
    if (data && data_len > 0) {
        memcpy(&frame[frame_len], data, data_len);
        frame_len += data_len;
    }
    
    // CRC校验（不包含帧头和帧尾）
    uint8_t crc = uart_calculate_crc(&frame[1], frame_len - 1);
    frame[frame_len++] = crc;
    
    // 帧尾
    frame[frame_len++] = FRAME_TAIL;
    
    // 发送
    int ret = uart_write_bytes(UART_PORT_NUM, (const char *)frame, frame_len);
    if (ret != frame_len) {
        ESP_LOGE(TAG, "Failed to send frame, ret=%d", ret);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// 解析接收到的数据
static int parse_frame(const uint8_t *raw_data, int raw_len, uart_command_t *cmd)
{
    if (raw_len < FRAME_MIN_LEN) {
        return -1;
    }
    
    // 查找帧头
    int head_pos = -1;
    for (int i = 0; i < raw_len; i++) {
        if (raw_data[i] == FRAME_HEAD) {
            head_pos = i;
            break;
        }
    }
    
    if (head_pos < 0 || head_pos + FRAME_MIN_LEN > raw_len) {
        return -1;
    }
    
    // 解析帧
    int pos = head_pos + 1;
    cmd->type = raw_data[pos++];
    cmd->seq = raw_data[pos++];
    
    // 数据长度
    cmd->data_len = raw_data[pos] | (raw_data[pos + 1] << 8);
    pos += 2;
    
    // 检查完整帧长度
    if (head_pos + FRAME_MIN_LEN + cmd->data_len > raw_len) {
        return -1;
    }
    
    // 复制数据
    if (cmd->data_len > 0) {
        memcpy(cmd->data, &raw_data[pos], cmd->data_len);
        pos += cmd->data_len;
    }
    
    // 验证CRC
    uint8_t recv_crc = raw_data[pos++];
    uint8_t calc_crc = uart_calculate_crc(&raw_data[head_pos + 1], 
                                          pos - head_pos - 2);
    if (recv_crc != calc_crc) {
        ESP_LOGW(TAG, "CRC mismatch: recv=%02X, calc=%02X", recv_crc, calc_crc);
        return -1;
    }
    
    // 验证帧尾
    if (raw_data[pos] != FRAME_TAIL) {
        ESP_LOGW(TAG, "Invalid frame tail: %02X", raw_data[pos]);
        return -1;
    }
    
    return pos + 1;
}

esp_err_t uart_comm_init(void)
{
    ESP_LOGI(TAG, "Initializing UART...");
    
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    // 安装UART驱动
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 
                                         UART_BUF_SIZE * 2, 10, &uart_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, 
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    ESP_LOGI(TAG, "UART initialized on TX=%d, RX=%d, Baud=%d", 
             UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);
    
    return ESP_OK;
}

esp_err_t uart_send_detection_state(const person_detection_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t data[6];
    data[0] = state->is_person_present ? 0x01 : 0x00;
    
    // 置信度转换为0-255
    data[1] = (uint8_t)(state->confidence * 255.0f);
    
    // 时间戳（秒，低4字节）
    uint32_t timestamp_sec = state->last_update_time / configTICK_RATE_HZ;
    memcpy(&data[2], &timestamp_sec, 4);
    
    return send_frame(CMD_STATUS_RESPONSE, data, sizeof(data));
}

BaseType_t uart_receive_command(uart_command_t *cmd, TickType_t wait_time)
{
    if (!cmd) {
        return pdFALSE;
    }
    
    uint8_t rx_buf[256];
    int rx_len = uart_read_bytes(UART_PORT_NUM, rx_buf, sizeof(rx_buf), 
                                  wait_time);
    
    if (rx_len > 0) {
        int parsed = parse_frame(rx_buf, rx_len, cmd);
        if (parsed > 0) {
            ESP_LOGD(TAG, "Received command: type=%02X, seq=%d, len=%d",
                     cmd->type, cmd->seq, cmd->data_len);
            return pdTRUE;
        }
    }
    
    return pdFALSE;
}

esp_err_t uart_send_ack(uart_cmd_type_t ack_type)
{
    uint8_t data = (uint8_t)ack_type;
    return send_frame(CMD_ACK, &data, 1);
}
