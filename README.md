# 智能CSI闹铃系统 (Smart Alarm with WiFi CSI)

基于启明云端 WTDKP4C5-S1 开发板和 ESP32-C5 的 WiFi CSI 人体检测智能闹铃系统。

## 项目概述

本项目实现了一个基于 WiFi CSI (Channel State Information) 技术的智能闹铃系统，能够：

- 📡 **使用 WiFi CSI 检测床上是否有人**
- ⏰ **设置多个闹钟**
- 😴 **智能贪睡功能**：当检测到床上仍有人时，自动延迟5分钟后再次响铃
- 🌐 **Web 页面交互**：通过浏览器配置闹钟和查看状态

## 硬件需求

| 组件 | 型号 | 功能 |
|------|------|------|
| 主控板 | WTDKP4C5-S1 | 运行主控程序、Web服务器 |
| WiFi CSI采集 | ESP32-C5 开发板 | CSI数据采集和人体检测 |
| 蜂鸣器 | 有源蜂鸣器 | 闹钟提醒 |
| 按钮 | 2个 | 贪睡/停止按钮 |
| 电源 | DC 12V 或 USB Type-C | 供电 |

### WTDKP4C5-S1 规格

- **芯片**: ESP32-P4 (主控) + ESP32-C5 (WiFi)
- **处理器**: 双核 RISC-V @ 360MHz
- **WiFi**: 2.4GHz & 5GHz 双频 Wi-Fi 6
- **接口**: USB 2.0, MIPI-CSI, MIPI-DSI, UART

## 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                    智能CSI闹铃系统                           │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────────┐          ┌──────────────────┐        │
│  │  WTDKP4C5-S1     │          │  ESP32-C5 开发板 │        │
│  │  (ESP32-P4)      │          │  (CSI采集器)     │        │
│  │                  │          │                  │        │
│  │  ┌────────────┐  │  UART    │  ┌────────────┐  │        │
│  │  │  闹钟管理   │  │◄────────►│  │  CSI采集   │  │        │
│  │  └────────────┘  │          │  └────────────┘  │        │
│  │  ┌────────────┐  │          │  ┌────────────┐  │        │
│  │  │  Web服务器 │  │           │  │ 人体检测   │  │        │
│  │  └────────────┘  │          │  └────────────┘  │        │
│  │  ┌────────────┐  │          │                  │        │
│  │  │  蜂鸣器控制 │  │          │                  │        │
│  │  └────────────┘  │          └──────────────────┘        │
│  └──────────────────┘                                      │
│                                                            │
│  连接方式: UART (TX=GPIO11, RX=GPIO10)                      │
└─────────────────────────────────────────────────────────────┘
```

## 软件架构

### 固件结构

```
smart_alarm/
├── firmware/
│   ├── esp32_c5_csi/           # ESP32-C5 CSI采集固件
│   │   ├── main/
│   │   │   ├── main.c          # 主程序
│   │   │   ├── csi_data.c/h    # CSI数据处理
│   │   │   ├── person_detector.c/h  # 人体检测算法
│   │   │   └── uart_comm.c/h   # UART通信
│   │   ├── CMakeLists.txt
│   │   └── sdkconfig.defaults
│   └──
│   └── wtdkp4c5_main/          # WTDKP4C5-S1 主控固件
│       ├── main/
│       │   ├── main.c          # 主程序
│       │   ├── alarm_manager.c/h  # 闹钟管理
│       │   ├── web_server.c/h  # Web服务器
│       │   ├── uart_comm.c/h   # UART通信
│       │   ├── storage.c/h     # 存储管理
│       │   └── web/            # Web页面文件
│       │       ├── index.html
│       │       ├── style.css
│       │       └── script.js
│       ├── CMakeLists.txt
│       └── sdkconfig.defaults
├── web/                        # Web页面（开发用）
└── docs/                       # 文档
```

## 人体检测原理

WiFi CSI (Channel State Information) 技术利用 WiFi 信号在传播过程中受到环境影响的变化来检测人体存在：

1. **多径效应**: 人体会对 WiFi 信号产生反射、散射和衍射
2. **幅度变化**: 人体移动导致信号幅度变化
3. **相位变化**: 人体存在改变信号相位
4. **子载波相关性**: 人体影响不同子载波之间的相关性

### 检测算法

```
┌──────────────────────────────────────────┐
│  原始CSI数据                              │
│  (128个子载波 × 2字节)                    │
└─────────────────┬────────────────────────┘
                  ▼
┌──────────────────────────────────────────┐
│  预处理                                   │
│  - 提取幅度和相位                         │
│  - 计算平均值                             │
└─────────────────┬────────────────────────┘
                  ▼
┌──────────────────────────────────────────┐
│  特征提取                                 │
│  - 幅度方差                               │
│  - 相位方差                               │
│  - 子载波相关性                           │
└─────────────────┬────────────────────────┘
                  ▼
┌──────────────────────────────────────────┐
│  人体检测判断                             │
│  综合评分 > 阈值 → 有人                   │
│  综合评分 ≤ 阈值 → 无人                   │
└──────────────────────────────────────────┘
```

## 快速开始

### 1. 硬件连接

#### WTDKP4C5-S1 与 ESP32-C5 连接

| WTDKP4C5-S1 | ESP32-C5 | 功能 |
|-------------|----------|------|
| GPIO 11 (TX)| GPIO 4 (RX) | UART TX |
| GPIO 10 (RX)| GPIO 5 (TX) | UART RX |
| GND         | GND       | 地线 |
| 3.3V        | 3.3V      | 电源 |

#### 外设连接 (WTDKP4C5-S1)

| 外设 | GPIO | 说明 |
|------|------|------|
| 蜂鸣器 | GPIO 8 | PWM控制 |
| 状态LED | GPIO 3 | 状态指示 |
| 贪睡按钮 | GPIO 2 | 中断触发 |
| 停止按钮 | GPIO 1 | 中断触发 |

### 2. 编译固件

#### ESP32-C5 固件编译

```bash
cd firmware/esp32_c5_csi
idf.py set-target esp32c5
idf.py build
idf.py flash
```

#### WTDKP4C5-S1 固件编译

```bash
cd firmware/wtdkp4c5_main
idf.py set-target esp32p4
idf.py build
idf.py flash
```

### 3. 配置WiFi

WTDKP4C5-S1 启动后会创建一个 WiFi AP:
- **SSID**: `SmartAlarm_AP`
- **密码**: `12345678`

连接后可访问 `http://192.168.4.1` 进行配置。

### 4. 使用Web界面

打开浏览器访问设备IP地址：

1. **设置闹钟**: 选择时间和分钟，点击"设置闹钟"
2. **查看状态**: 实时显示人体检测状态和置信度
3. **测试响铃**: 点击"测试响铃"按钮测试
4. **调整阈值**: 滑块调整检测灵敏度

## API接口

### 获取状态
```http
GET /api/status
```

响应：
```json
{
  "is_person_present": true,
  "confidence": 0.85,
  "next_alarm": "07:30",
  "wifi_signal": -45
}
```

### 获取闹钟列表
```http
GET /api/alarms
```

### 添加闹钟
```http
POST /api/alarms
Content-Type: application/json

{
  "time": "07:30"
}
```

### 删除闹钟
```http
DELETE /api/alarms/{id}
```

### 停止响铃
```http
POST /api/stop-alarm
```

## 配置参数

### 检测阈值

在Web界面或通过API调整：
- **范围**: 10 - 100
- **默认值**: 30
- **说明**: 数值越小越灵敏，越大越不容易误报

### 贪睡时间

- **默认值**: 5分钟
- **可调**: 3/5/10/15分钟

## 调优建议

### 最佳实践

1. **CSI采集器放置**: 将ESP32-C5开发板放置在床边，距离床1-2米
2. **避免干扰**: 远离其他WiFi设备和金属物体
3. **阈值调整**: 根据实际环境调整检测阈值
4. **校准**: 首次使用时进行环境校准

### 常见问题

| 问题 | 可能原因 | 解决方案 |
|------|----------|----------|
| 误报率高 | 阈值过低 | 增加阈值数值 |
| 漏检 | 阈值过高 | 降低阈值数值 |
| 检测不稳定 | CSI采集器位置 | 调整位置和角度 |
| 无法连接WiFi | AP配置错误 | 检查SSID和密码 |

## 技术参考

### WiFi CSI 技术

- [ESP-CSI 项目](https://github.com/espressif/esp-csi)
- [乐鑫 WiFi CSI 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-channel-state-information)

### 相关硬件

- [WTDKP4C5-S1 规格书](WTDKP4C5-S1_UserGuide_V1.1.pdf)
- [ESP32-C5 技术规格](https://www.espressif.com/en/products/socs/esp32-c5)

## 许可

本项目基于 MIT 许可证开源。

## 贡献

欢迎提交 Issue 和 Pull Request！

## 致谢

- 启明云端 (Wireless-Tag) 提供的 WTDKP4C5-S1 开发板
- 乐鑫科技 (Espressif) 的 ESP-IDF 开发框架
- ESP-CSI 开源项目
