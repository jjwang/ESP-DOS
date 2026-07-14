# AGENTS.md — ESP-DOS Pocket Operating System 构建备忘录

## 构建命令

```bash
# 编译固件
python -m platformio run

# 烧录固件
python -m platformio run --target upload --upload-port COM3

# 清理并重新编译
python -m platformio run --target clean && python -m platformio run

# 串口监视器
python -m platformio device monitor --port COM3 --baud 115200
```

## 构建 ELF 应用

```bash
# 编译所有 ELF 应用（apps/）到 data/bin/ + include/fonts/
python tools/build_apps.py

# 一键完整构建（ELF + 固件 + 烧录）
python tools/build_all.py
```

## 添加新 ELF 应用

1. 在 `apps/` 下创建 `xxx.c`
2. 在 `tools/build_apps.py` 的 `apps` 列表中添加应用名
3. 在 `platformio.ini` 的 `build_flags` 中添加 `-D ELF_XXX_DATA`
4. 在 `kernel/vfs.c` 中添加 `#include "fonts/elf_xxx.h"` 和 `install_elf("xxx", ...)`
5. `python tools/build_apps.py && python -m platformio run`

## 项目结构

```
esp-dos/
├── platformio.ini        # PlatformIO 配置
├── partitions.csv        # Flash 分区表 (16MB)
├── sdkconfig.defaults    # ESP-IDF Kconfig 默认配置
├── CMakeLists.txt        # ESP-IDF 顶层 CMake
├── kernel/               # 内核源码
│   ├── CMakeLists.txt    # ESP-IDF 组件注册
│   ├── main.c            # 入口
│   ├── display_st7789.c  # ST7789 驱动
│   ├── terminal.c        # 终端模拟器
│   ├── vfs.c             # SPIFFS 文件系统
│   ├── shell.c           # 命令行 Shell
│   ├── elf_loader.c      # ELF 加载器
│   └── process.c         # 进程管理
├── apps/                 # ELF 应用源码
│   ├── command.ld        # ELF 链接脚本
│   ├── echo.c            # echo 命令
│   ├── hello.c           # hello 测试
│   ├── date.c / free.c / uname.c / df.c
│   └── README.md         # 应用开发文档
├── include/              # 头文件
│   ├── config.h          # 引脚和系统配置
│   ├── command_sdk.h     # ELF 应用 SDK
│   ├── *.h               # 各模块头文件
│   └── fonts/            # 字体数据 + ELF 嵌入头
├── flash.bin              # QEMU flash 镜像 (16MB)
├── data/bin/             # 编译好的 ELF 二进制
└── tools/                # 构建工具
    ├── qemu/              # Espressif QEMU (ESP32-S3)
    │   └── bin/qemu-system-xtensa.exe
    ├── build_apps.py     # 编译 ELF 应用
    ├── build_all.py      # 一键构建
    ├── gen_flash_image.py # 生成 QEMU flash.bin + eFuse
    ├── genfont.py        # 字库生成
    └── run_qemu.ps1      # 一键构建 + QEMU 启动
```

## 引脚配置

编辑 `include/config.h` 中的 `TFT_*_PIN`。

## 字库重新生成

```bash
cd tools
pip install Pillow
python genfont.py
```

## QEMU 模拟器

```powershell
# 构建 + 运行 (无图形, 仅串口)
tools/run_qemu.ps1 -NoGraphics

# 仅运行 (跳过构建)
tools/run_qemu.ps1 -NoBuild -NoGraphics

# 图形模式 (SDL 显示)
tools/run_qemu.ps1

# 调试模式 (等待 GDB 连接)
tools/run_qemu.ps1 -Gdb
```

QEMU 通过 `tools/gen_flash_image.py` 自动合并以下文件到 `flash.bin`:
- bootloader.bin @ 0x00000
- partitions.bin @ 0x08000
- ota_data_initial.bin @ 0x0D000
- firmware.bin   @ 0x10000
- spiffs.bin     @ 0x410000 (可选)

eFuse 配置: ESP32-S3 rev 0.3, 32MB RAM, Octal PSRAM, 16MB SPI Flash.

注意: QEMU 内 ST7789 显示未模拟, 只能通过串口交互。

## ELF 技术要点

- 编译选项：`-fPIC -mno-serialize-volatile -mabi=windowed`
- 入口：`_start(int argc, char **argv, syscall_t *sys)`
- PSRAM → ICache：`esp_mmu_map()` + 缓存一致性
- 重定位：加载时扫描 literal pool 调整绝对地址
- 系统调用：通过 `syscall_t` 结构体指针访问内核 API
- 详细说明见 `apps/README.md`
