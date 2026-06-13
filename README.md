# OpenCrab — Pocket Operating System for ESP32

一个运行在 ESP32-S3-WROOM-1 N16R8 上的口袋操作系统，类 Unix 终端 + ELF 可执行命令 + ST7789 320×170 显示屏。

## 功能特点

- **类 Unix Shell**: 内置 `ls`, `cat`, `cd`, `pwd`, `mkdir`, `rm`, `clear`, `ps`, `reboot`, `touch`, `write`, `mv`, `cp`, `help`, `exec`, `kill` 共 16 条内核命令
- **ELF 可执行命令**: `echo`, `hello`, `date`, `free`, `uname`, `df` 以独立 ELF 文件运行于 SPIFFS
- **文件系统**: SPIFFS ~12MB 持久化存储
- **ELF 加载器**: 从 SPIFFS 加载 Xtensa ELF → PSRAM → MMU 映射到 ICache → 执行，支持重定位
- **进程管理**: 16 槽 PCB，spawn/kill/wait，系统调用表
- **显示**: ST7789 320×170，RGB565，40MHz SPI，PSRAM 帧缓冲
- **英文字体**: u8g2_font_6x12_tf (Misc-Fixed 6×12 BDF)
- **中文字体**: SimSun FreeType 12×12，覆盖 CJK 全范围 + Emoji（22,049 字符）
- **颜色区分**: 提示符 `$` 和用户输入为青色，系统输出为浅灰色
- **光标闪烁**: 500ms 周期闪烁
- **实时输入回显**: 串口输入逐字符显示
- **启动画面**: OpenCrab 大 Logo（2倍缩放 + 阴影），中文系统信息

## 硬件需求

- **主控**: ESP32-S3-WROOM-1 N16R8（16MB Flash + 8MB PSRAM）
- **屏幕**: ST7789 1.9寸 320×170 SPI 接口
- **接线**: CS=GPIO10, DC=GPIO11, RST=GPIO1, MOSI=GPIO13, SCLK=GPIO12, BL=GPIO14

## 键盘布局

4 行 × 14 键分体键盘，TCA8418 PZ418 克隆芯片通过 I2C (GPIO4/5) 驱动。

```
┌───┬───┬───┬───┬───┬───┬───┐   ┌───┬───┬───┬───┬───┬───┬───┐
│Esc│ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │   │ 7 │ 8 │ 9 │ 0 │ - │ = │Bsp│
├───┼───┼───┼───┼───┼───┼───┤   ├───┼───┼───┼───┼───┼───┼───┤
│Tab│ Q │ W │ E │ R │ T │ Y │   │ U │ I │ O │ P │ [ │ ] │ \ │
├───┼───┼───┼───┼───┼───┼───┤   ├───┼───┼───┼───┼───┼───┼───┤
│Fn │Shi│ A │ S │ D │ F │ G │   │ H │ J │ K │ L │ ; │ ' │ ↵ │
├───┼───┼───┼───┼───┼───┼───┤   ├───┼───┼───┼───┼───┼───┼───┤
│Ctl│Opt│Alt│ Z │ X │ C │ V │   │ B │ N │ M │ , │ . │ / │Spc│
└───┴───┴───┴───┴───┴───┴───┘   └───┴───┴───┴───┴───┴───┴───┘
```

| 物理行 | PCB 左半 (键位) | PCB 右半 (键位) |
|--------|----------------|----------------|
| R1 | ROW7 K001–K007 | ROW6 K008–K014 |
| R2 | ROW5 K015–K021 | ROW4 K022–K028 |
| R3 | ROW3 K029–K035 | ROW2 K036–K042 |
| R4 | ROW1 K043–K049 | ROW0 K050–K056 |

- **主控**: TCA8418 PZ418 (24-pin WQFN), 固定 I2C 地址 `0x34`
- **矩阵**: 8 行 × 7 列 (行 GPIO0–7, 列 GPIO8–14)
- **INT**: GPIO6 (开漏, 10kΩ 上拉), 低电平有效
- **I2C**: I2C_NUM_1, SDA=GPIO4, SCL=GPIO5, 100kHz

## 快速开始

```bash
# 编译固件
python -m platformio run

# 烧录固件
python -m platformio run --target upload --upload-port COM3

# 串口监视器
python -m platformio device monitor --port COM3 --baud 115200
```

详细刷机指南见 [FLASHING.md](FLASHING.md)

## 命令列表

### 内置命令（kernel/ 实现）

| 命令 | 说明 | 用法 |
|------|------|------|
| `help` | 显示帮助信息 | `help [命令]` |
| `ls` | 列出目录内容 | `ls [-l] [路径]` |
| `cat` | 显示文件内容 | `cat <文件>` |
| `cd` | 切换当前目录 | `cd <路径>` |
| `pwd` | 显示当前目录 | `pwd` |
| `mkdir` | 创建目录 | `mkdir <目录名>` |
| `rm` | 删除文件或目录 | `rm [-r] <路径>` |
| `clear` | 清屏 | `clear` |
| `ps` | 显示进程信息 | `ps` |
| `reboot` | 重启系统 | `reboot` |
| `touch` | 创建空文件 | `touch <文件>` |
| `write` | 写入文件 | `write <文件> <内容>` |
| `mv` | 移动/重命名文件 | `mv <源> <目标>` |
| `cp` | 复制文件 | `cp <源> <目标>` |
| `exec` | 运行 ELF 程序 | `exec <ELF文件> [参数...]` |
| `kill` | 终止进程 | `kill <PID>` |

### ELF 应用（apps/ 实现）

| 命令 | 说明 | 用法 |
|------|------|------|
| `echo` | 输出文本 | `echo <文本...>` |
| `date` | 显示版本信息 | `date` |
| `free` | 显示内存信息 | `free` |
| `uname` | 显示系统信息 | `uname [-a]` |
| `df` | 显示磁盘使用 | `df` |
| `hello` | ELF 测试例程 | `hello` |

> 提示：ELF 应用存放于 SPIFFS 的 `/bin/` 目录。输入命令名时 Shell 先查找内置表，未命中则尝试加载 `/bin/<命令>`。

## 项目结构

```
opencrab/
├── kernel/                  # 内核源码（固件主程序）
│   ├── CMakeLists.txt       # ESP-IDF 组件注册
│   ├── main.c               # 入口，UART/显示/任务初始化
│   ├── display_st7789.c     # ST7789 SPI 显示驱动
│   ├── terminal.c           # 终端模拟器（回滚/颜色/滚动/光标）
│   ├── vfs.c                # SPIFFS 虚拟文件系统
│   ├── shell.c              # 命令行 Shell
│   ├── elf_loader.c         # ELF 解析/加载/MMU 映射
│   └── process.c            # 进程管理/系统调用表
├── apps/                    # ELF 应用源码
│   ├── command.ld           # ELF 链接脚本
│   ├── echo.c               # echo 命令
│   ├── hello.c              # hello 测试
│   ├── date.c               # date 命令
│   ├── free.c               # free 命令
│   ├── uname.c              # uname 命令
│   └── df.c                 # df 命令
├── include/                 # 头文件
│   ├── config.h             # 引脚/系统配置
│   ├── app_sdk.h        # ELF 应用 SDK（系统调用表定义）
│   ├── display_st7789.h
│   ├── terminal.h
│   ├── vfs.h
│   ├── shell.h
│   ├── elf.h
│   ├── process.h
│   └── fonts/               # 自动生成的字体数据 + ELF 嵌入头
├── data/                    # SPIFFS 初始数据
│   └── bin/                 # 编译好的 ELF 二进制
├── tools/                   # 构建工具
│   ├── build_apps.py        # 编译 ELF 应用 → data/bin/ + include/fonts/
│   ├── build_all.py         # 一键构建（ELF + SPIFFS + 烧录）
│   └── genfont.py           # 字库生成
├── platformio.ini           # PlatformIO 配置
├── partitions.csv           # Flash 分区表
├── CMakeLists.txt           # ESP-IDF 顶层 CMake
└── sdkconfig.defaults       # Kconfig 默认配置
```

## 构建 ELF 应用

ELF 应用是独立的 Xtensa ELF 文件，编译为 PIC（位置无关代码）后存放于 SPIFFS 的 `/bin/` 目录。Shell 在输入未知命令时自动查找并执行对应的 ELF。

### 编译所有应用

```bash
# 方式一：使用构建脚本
python tools/build_apps.py

# 方式二：一键构建（编译 ELF + 构建固件 + 烧录）
python tools/build_all.py
```

输出：
- `data/bin/<app>` — ELF 二进制文件
- `include/fonts/elf_<app>.h` — 嵌入固件的 C 头文件

### 添加新应用

1. 在 `apps/` 下创建 C 源文件，引用 SDK：
```c
#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    sys->println("Hello from my app!");
    sys->exit(0);
}
```

2. 在 `tools/build_apps.py` 的 `apps` 列表中添加应用名。

3. 重新编译：
```bash
python tools/build_apps.py
# 或者完整构建
python -m platformio run
```

### 应用 SDK 说明

ELF 应用通过系统调用表与内核交互。`syscall_t` 结构体提供以下 API：

| 类别 | 函数 | 说明 |
|------|------|------|
| 输出 | `print(s)` | 输出字符串 |
| 输出 | `println(s)` | 输出字符串 + 换行 |
| 输出 | `printf(fmt, ...)` | 格式化输出 |
| 文件 | `fopen(path, mode)` | 打开文件 |
| 文件 | `fread(buf, size, fp)` | 读取文件 |
| 文件 | `fwrite(buf, size, fp)` | 写入文件 |
| 文件 | `fclose(fp)` | 关闭文件 |
| 文件 | `fexist(path)` | 检查文件存在 |
| 内存 | `malloc(size)` | 分配内存 |
| 内存 | `free(p)` | 释放内存 |
| 系统 | `exit(code)` | 退出进程 |
| 系统 | `reboot()` | 重启系统 |
| 系统 | `sleep(ms)` | 延时 |
| 系统 | `get_free_heap()` | 获取空闲堆大小 |
| 系统 | `get_total_heap()` | 获取总堆大小 |
| 系统 | `get_free_psram()` | 获取空闲 PSRAM |
| 系统 | `get_total_psram()` | 获取总 PSRAM |
| 系统 | `get_chip_desc(buf,max)` | 获取芯片描述 |
| 系统 | `get_fs_info(total,used)` | 获取文件系统信息 |

### ELF 执行流程

```
输入命令 → Shell 查找内置表未命中
         → 检查 /bin/<命令> 存在
         → elf_load(): 从 SPIFFS 读取 ELF
         → heap_caps_malloc(): 分配 PSRAM
         → 加载 PT_LOAD 段到 PSRAM
         → 重定位：调整绝对地址
         → cache_hal_writeback_addr(): 刷新 DCache
         → esp_mmu_map(): PSRAM 物理页 → ICache 虚拟地址
         → cache_hal_invalidate_addr(): 使 ICache 失效
         → proc_spawn_elf(): 创建 FreeRTOS 任务执行
```

## ELF 技术细节

- **编译**: `-fPIC -mno-serialize-volatile -mabi=windowed`，链接为独立 EXEC
- **重定位**: 加载时扫描 literal pool，将 `0x3Fxxxxxx` 范围的绝对地址加上加载偏移
- **PSRAM → ICache**: 利用 `esp_mmu_map` 将 PSRAM 物理页映射到 ICache 空间（0x4280xxxx）
- **缓存一致性**: 写入后 `cache_hal_writeback_addr`，映射后 `cache_hal_invalidate_addr`
- **入口约定**: `_start(int argc, char **argv, syscall_t *sys)`
- **配置要求**: `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`，`CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y`

## 字库

生成工具 `tools/genfont.py`：

```bash
cd tools
pip install Pillow
python genfont.py
```

- **英文**: u8g2_font_6x12_tf (Misc-Fixed 6×12 BDF)，6px 宽
- **中文**: FreeType 渲染 SimSun 12×12，CJK U+4E00-U+9FFF + Emoji，22,049 字符

## 引脚配置

编辑 `include/config.h` 中的 `TFT_*_PIN`。

## 技术栈

```
串口/键盘 → Shell (shell.c)
          → 内置命令 → Terminal (terminal.c) → Display Driver (ST7789)
          → ELF 命令 → ELF Loader (elf_loader.c) → MMU ICache → 执行
          → VFS (vfs.c) → SPIFFS (12MB Flash 分区)
          → Process Manager (process.c) → FreeRTOS 任务
          → ESP-IDF 5.5.3 / FreeRTOS / ESP32-S3 240MHz
          → 8MB PSRAM / 16MB Flash
```

## 许可

MIT License
