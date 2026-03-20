/**
 * 存储模块头文件
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化存储
esp_err_t storage_init(void);

// 写入数据
esp_err_t storage_write(const char *key, const void *data, size_t len);

// 读取数据
esp_err_t storage_read(const char *key, void *data, size_t *len);

// 删除数据
esp_err_t storage_delete(const char *key);

// 清空所有数据
esp_err_t storage_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_H */
