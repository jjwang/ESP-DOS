# ESP-DOS - 刷机指南

## 项目简介

ESP-DOS 是一个运行在 ESP32-S3 上的类 Unix 终端系统。它提供：
- 命令行终端（通过 ST7789 显示屏输出）
- 文件系统（SPIFFS）
- 类 Unix 命令（ls, cat, cd, pwd, mkdir, rm, echo 等）
- 中文显示支持

## 硬件要求

| 组件 | 规格 |
|------|------|
| 主控 | ESP32-S3-WROOM-1 N16R8（16MB Flash + 8MB PSRAM） |
| 屏幕 | ST7789 320×170 1.9寸 SPI 接口 |
| 连接线 | USB Type-C 数据线（用于烧录和供电） |

## 引脚连接

根据你的 ST7789 模块，按以下方式连接：

| ST7789 引脚 | ESP32-S3 引脚 |
|-------------|---------------|
| CS (片选)   | GPIO4         |
| DC (数据/命令) | GPIO5      |
| RST (复位)  | GPIO6         |
| MOSI (数据) | GPIO7         |
| SCLK (时钟) | GPIO15        |
| BL (背光)   | GPIO16        |
| VCC         | 3.3V          |
| GND         | GND           |

> **注意**: 不同厂商的模块引脚定义可能不同。请根据你的模块实际丝印调整。
> 如需修改引脚，编辑 `include/config.h` 中的 `TFT_*_PIN` 宏。

## 开发环境搭建

### 方法一：使用 PlatformIO（推荐）

1. **安装 VSCode**
   下载并安装 Visual Studio Code: https://code.visualstudio.com/

2. **安装 PlatformIO 插件**
   - 打开 VSCode
   - 点击左侧扩展图标（或按 `Ctrl+Shift+X`）
   - 搜索 "PlatformIO IDE"
   - 点击安装

3. **安装驱动**
   - ESP32-S3 使用 USB Serial/JTAG 控制器，Windows 10/11 通常自动识别
   - 如无法识别，安装 CP210x 或 CH340 驱动（取决于你的开发板）

### 方法二：使用 ESP-IDF 命令行

1. **安装 ESP-IDF**
   参照官方文档: https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/

2. **设置环境变量**
   ```bash
   # Windows (PowerShell)
   $env:IDF_PATH = "C:\esp-idf"
   . $env:IDF_PATH\export.ps1
   ```

## 获取源码

```bash
git clone <项目地址> esp-dos
cd esp-dos
```

## 编译

### PlatformIO 编译

```bash
# 在项目目录下执行
pio run
```

或使用 VSCode PlatformIO 插件：
1. 在 VSCode 中打开 esp-dos 目录
2. 点击底部状态栏的 →（向右箭头）按钮
3. 选择 "esp32-s3-dev" 环境

### ESP-IDF 命令行编译

```bash
cd esp-dos
idf.py build
```

## 烧录（刷机）

### 连接开发板

1. 使用 USB 数据线将 ESP32-S3 开发板连接到电脑
2. 确认驱动已安装，设备管理器中可以看到串口设备

### PlatformIO 烧录

```bash
# 自动检测并烧录
pio run --target upload

# 指定串口烧录（如果自动检测失败）
pio run --target upload --upload-port COM3
```

> 将 `COM3` 替换为实际端口号。在 Windows 下可通过设备管理器查看。

### ESP-IDF 命令行烧录

```bash
# 烧录固件
idf.py -p COM3 flash

# 同时烧录 LittleFS 文件系统（如果有数据文件）
idf.py -p COM3 flash
```

### 进入下载模式

大多数 ESP32-S3 开发板支持自动复位进入下载模式。如果遇到问题：

1. 按住 **BOOT/IO0** 按钮
2. 短按 **EN/RST** 按钮
3. 松开 **BOOT** 按钮
4. 开始烧录

## 首次启动

1. 烧录完成后，开发板会自动重启
2. **显示屏**上会显示启动画面：
   ```
   ESP-DOS v0.1.0
   ESP32-S3 rev.X  X核
   PSRAM: 8MB  Flash: 16MB
   显示: ST7789 320x170
   ```
3. 启动完成后显示 Shell 提示符：
   ```
   openterm$
   ```
4. 打开串口监视器（波特率 115200）与系统交互

### 打开串口监视器

#### PlatformIO
```bash
python -m platformio device monitor --port COM3 --baud 115200
```
退出按 `Ctrl+C`。

#### ESP-IDF
```bash
idf.py -p COM3 monitor
```

#### 使用 PuTTY
1. 打开 PuTTY
2. 选择 "Serial" 连接类型
3. 串口: COM3（根据实际情况修改）
4. 波特率: 115200
5. 点击 "Open"

#### 串口调试助手
1. 设置波特率 115200
2. 勾选"发送新行"或手动在消息末尾加回车
3. 输入命令后点击发送

## 常用命令

| 命令 | 说明 | 示例 |
|------|------|------|
| `help` | 显示帮助信息 | `help ls` |
| `ls` | 列出目录 | `ls /home` |
| `cat` | 显示文件内容 | `cat /home/welcome.txt` |
| `cd` | 切换目录 | `cd /home` |
| `pwd` | 显示当前目录 | `pwd` |
| `mkdir` | 创建目录 | `mkdir mydir` |
| `rm` | 删除文件 | `rm file.txt` |
| `rm -r` | 删除目录 | `rm -r mydir` |
| `echo` | 输出文本 | `echo Hello` |
| `touch` | 创建空文件 | `touch test.txt` |
| `write` | 写入文件 | `write test.txt Hello` |
| `clear` | 清屏 | `clear` |
| `ps` | 查看进程 | `ps` |
| `free` | 查看内存 | `free` |
| `df` | 查看磁盘 | `df` |
| `date` | 查看时间 | `date` |
| `uname` | 查看系统信息 | `uname -a` |
| `reboot` | 重启系统 | `reboot` |

## 文件系统说明

系统使用 SPIFFS 文件系统，划分了约 12MB 空间。

预置目录：
- `/home` - 用户目录
- `/tmp` - 临时文件
- `/etc` - 配置文件
- `/bin` - 二进制文件
- `/dev` - 设备文件
- `/mnt` - 挂载点

## 常见问题

### Q: 开机闪白屏？
A: 这是 ST7789 面板从睡眠模式唤醒时的正常现象。消除方法：
1. 背光引脚在初始化全程保持未配置（高阻态），最后才设为输出点亮
2. 初始化时序：先发 Display OFF (0x28) → Sleep Out (0x11) → 写入全黑帧 → Display ON (0x29) → 背光 ON
3. 对应代码在 `src/display_st7789.c` 的 `display_init()` 函数中

### Q: 屏幕不显示？
A: 检查引脚连接是否正确。可能是初始化序列不匹配，尝试调整 `config.h` 中的 `TFT_MADCTL`。

### Q: 烧录失败？
A: 尝试手动进入下载模式（按住 BOOT → 按 RST → 松开 BOOT）。

### Q: 串口没有输出？
A: 检查串口是否正确连接，确认使用了正确的波特率 (115200)。

### Q: 如何重刷文件系统？
A: 格式化 SPIFFS:
```
# 在 shell 中执行（需要先实现 format 命令）
# 或重新烧录整个固件
```

## 重新生成字库

如有自定义字体需求：

```bash
cd tools
pip install Pillow
python genfont.py
```

## 技术栈

- **框架**: ESP-IDF (FreeRTOS)
- **显示**: ST7789 (SPI, 80MHz)
- **文件系统**: SPIFFS (12MB)
- **终端**: 自定义文本终端，支持中文
- **字库**: 12×12 点阵中文 + 12×6 点阵英文
