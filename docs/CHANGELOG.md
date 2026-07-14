# 修订记录

## 2026-05-15

### 修复：CD 命令、提示符、SPIFFS readdir、中文字符损坏

- `kernel/vfs.c`：用文件注册表替代 SPIFFS readdir 遍历目录，根除卡死
- `vfs_chdir` 改用注册表查找路径，修复根目录路径拼接 `//` 问题
- `kernel/shell.c`：提示符 `A:\>` 随当前目录变化（`CD \bin` → `A:\BIN\>`）
- 还原被 PowerShell 损坏的中文字符串
- `kernel/display_st7789.c`：冒号字符右移 1 像素
- `kernel/main.c`：首屏信息改为 DOS 绿色
- DIR 命令 DOS 格式输出，区分 `<DIR>` 和文件大小

## 2026-05-14

### 新增：DATE/TIME 命令及 DOS 欢迎界面

- `kernel/shell.c`：内置 DATE/TIME 命令，支持 DOS 风格交互设置
- 启动时终端显示 `ESP-DOS-DOS Version 1.0` + 版权信息
- `kernel/main.c`：首屏 ESP-DOS logo 改为 DOS 经典绿
- `kernel/terminal.c`：光标改为 DOS 下划线风格 (6px宽)
- 所有输出配色改为 DOS 经典绿

### 重构：ESP-DOS → ESP-DOS-DOS

- `include/config.h`：系统改名为 ESP-DOS-DOS，提示符改为 `A:\\>`
- `kernel/shell.c`：命令表 DOS 化 — DIR/TYPE/CLS/MD/DEL/REN/COPY/HELP
- 命令解析支持大小写不敏感
- 增加分页输出（按任意键继续）
- 全终端配色改为 DOS 经典绿 (0x07E0)
- ELF 命令改名：DATE/MEM/VER/CHKDSK，输出格式 DOS 化
- `kernel/vfs.c`：ELF 安装名同步更新，欢迎消息改 ESP-DOS-DOS

## 2026-05-13

### 新增：TCA8418 键盘驱动 (TI TCA8418)

- `kernel/tca8418.c` `include/tca8418.h`：基于 Linux 内核驱动重写，修正为 TI TCA8418 寄存器映射
- 修正了寄存器地址（原为 NXP TCA8418E 映射，实际芯片为 TI TCA8418）
- 添加 `REG_KP_GPIO`、`REG_GPIO_DIR`、`REG_GPI_EM` 等关键寄存器配置
- `CFG.KE_IEN` 为 BIT(0)（非 BIT(6)），`INT_STAT.K_INT` 为 BIT(0)
- 键事件寄存器在 `REG_KEY_EVENT_A`(0x04)，键码 = row × 10 + col + 1
- 行上拉通过 `REG_GPIO_PULL1`(0x2C) 2-bit 编码控制
- `include/config.h`：I2C 引脚改为 GPIO 4(SDA)/5(SCL)，移除 INT 引脚
- 4×4 数字键盘映射（物理接线 R0↔TCA R3, R1↔TCA R2, R2↔TCA R1, R3↔TCA R0）

### 新增：DS3231 RTC 驱动

- `kernel/ds3231.c` `include/ds3231.h`：DS3231 实时时钟驱动
- 独立 I2C 总线（I2C_NUM_0, SDA=GPIO15, SCL=GPIO16）
- 系统启动时自动读取 RTC 时间同步到 ESP32 系统时钟
- `kernel/i2c_bus.c` `include/i2c_bus.h`：抽出共享 I2C 总线初始化
- 更新 `kernel/CMakeLists.txt` 添加新源文件
- `docs/keyboard.md`：更新 TCA8418 + DS3231 完整接线文档

### 修复：ESP_LOGI 日志串口输出

**问题**：`CONFIG_ESP_CONSOLE_UART_NONE=y` 导致控制台禁用，`ESP_LOGI` 输出被丢弃，
且 `ESP_CONSOLE_ROM_SERIAL_PORT_NUM = -1` 导致 PSRAM/cache 初始化失败，固件卡死无法启动。

**修复**：`sdkconfig.defaults`
- `CONFIG_ESP_CONSOLE_UART_NONE=y` → `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`
- 移除了 `CONFIG_LOG_DEFAULT_LEVEL_INFO` / `CONFIG_LOG_MAXIMUM_LEVEL_INFO`（非必要）

### 修复：多核日志交织

**问题**：双核同时调用 `ets_printf` / `uart_write_bytes` 写 UART 导致日志行交织。

**修复**：`kernel/main.c`
- 添加 `log_vprintf` 回调，使用 `esp_rom_vprintf` + 原子锁 (`__sync_lock_test_and_set`)
- `esp_log_set_vprintf` 移至 `app_main` 第一行，尽早接管所有日志输出
- 回调后添加 `vTaskDelay(20ms)`，等待另一核的启动日志传输完毕
- 原子锁串行化双核同时调用 `esp_rom_vprintf` 的冲突

### 其他变更

- `platformio.ini`: 移除了 `-DCONFIG_ESP_CONSOLE_UART0=y`（不影响 Kconfig）
- `sdkconfig.defaults`: 精简冗余配置
- `partitions.csv`: SPIFFS 从 0xBF0000(~12MB) 缩为 0x200000(2MB)
