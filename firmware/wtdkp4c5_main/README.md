# 智能CSI闹钟 - WTDKP4C5-S1

基于启明云端 WTDKP4C5-S1 (ESP32-P4 + ESP32-C5) 的智能闹钟系统。

## 📁 项目文件

```
wtdkp4c5_main/
├── main/                      # 主程序源码
│   ├── main.c                 # 主程序入口
│   ├── csi_processor.c/h      # CSI人体检测处理
│   ├── sdio_host_csi.c/h      # SDIO通信（与C5）
│   ├── alarm_manager.c/h      # 闹钟管理
│   ├── web_server.c/h         # Web服务器
│   ├── storage.c/h            # NVS存储
│   ├── display.c              # 显示驱动
│   ├── uart_comm.c/h          # UART通信
│   └── web/                   # Web页面资源
│       ├── index.html
│       ├── style.css
│       └── script.js
├── build/                     # 编译输出
│   ├── smart_alarm_main.bin   # 主固件
│   ├── bootloader/bootloader.bin
│   └── partition_table/partition-table.bin
├── build_project.bat          # 编译脚本
├── flash.bat                  # 完整烧录脚本
├── flash_without_erase.bat    # 快速烧录脚本
├── monitor.bat                # 串口监视器
├── sdkconfig                  # SDK配置
└── CMakeLists.txt             # CMake配置
```

## 🚀 快速开始

### 1. 编译固件

双击 `build_project.bat` 进行编译。

### 2. 烧录固件

**首次烧录：**
1. 双击 `flash.bat`
2. 按住 BOOT 键 → 按 RST 键 → 松开 BOOT 键
3. 按任意键开始烧录

**后续更新（保留设置）：**
- 双击 `flash_without_erase.bat`

### 3. 查看日志（可选）

双击 `monitor.bat` 查看串口输出。

### 4. 配置闹钟

1. 连接 WiFi 热点 `MyESP32P4_AP`，密码 `12345678`
2. 浏览器访问 `http://192.168.4.1`
3. 设置闹钟时间和检测灵敏度

## 🛠️ 开发环境

- **芯片**: ESP32-P4 (主控) + ESP32-C5 (WiFi/CSI)
- **SDK**: ESP-IDF v5.5.2
- **开发板**: 启明云端 WTDKP4C5-S1

## 📋 功能特性

- ✅ WiFi CSI 人体检测
- ✅ 多闹钟管理（最多10个）
- ✅ 智能贪睡（检测到人在床上自动延迟）
- ✅ Web 配置界面
- ✅ 物理按键控制（贪睡/停止）
- ✅ 蜂鸣器提醒
- ✅ 数据断电保存

## 📚 详细文档

- [烧录说明](烧录说明.md) - 详细的烧录步骤和故障排除

## ⚠️ 注意事项

1. 需要配合板载 ESP32-C5 使用（C5 负责 CSI 数据采集）
2. 首次使用建议先进行环境校准
3. WiFi AP 配置可在 `sdkconfig` 中修改

## 📄 License

MIT License
