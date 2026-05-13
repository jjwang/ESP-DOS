# 修订记录

## 2026-05-13

### 修复：ESP_LOGI 日志串口输出

**问题**：`CONFIG_ESP_CONSOLE_UART_NONE=y` 导致控制台禁用，`ESP_LOGI` 输出被丢弃，
且 `ESP_CONSOLE_ROM_SERIAL_PORT_NUM = -1` 导致 PSRAM/cache 初始化失败，固件卡死无法启动。

**修复**：`sdkconfig.defaults`
- `CONFIG_ESP_CONSOLE_UART_NONE=y` → `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`
- 移除了 `CONFIG_LOG_DEFAULT_LEVEL_INFO` / `CONFIG_LOG_MAXIMUM_LEVEL_INFO`（非必要）

### 修复：多核日志交织

**问题**：双核同时调用 `ets_printf` / `uart_write_bytes` 写 UART 导致日志行交织。

**修复**：`kernel/main.c`
- 添加 `log_vprintf` 回调，使用 `vsnprintf` + `uart_write_bytes` + 原子锁 (`__sync_lock_test_and_set`)
- `uart_init()` 后设置 `esp_log_set_vprintf(log_vprintf)`
- `esp_log_set_vprintf` 后添加 `vTaskDelay(250)` 等待前一行输出完成

### 其他变更

- `platformio.ini`: 移除了 `-DCONFIG_ESP_CONSOLE_UART0=y`（不影响 Kconfig）
- `sdkconfig.defaults`: 精简冗余配置
- `partitions.csv`: SPIFFS 从 0xBF0000(~12MB) 缩为 0x200000(2MB)
