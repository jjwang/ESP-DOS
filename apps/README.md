# ESP-DOS: Pocket Operating System for ESP32
## ELF 应用开发指南

ELF 应用是独立的 Xtensa ELF 可执行文件，存放于 SPIFFS 的 `/bin/` 目录，由 Shell 在输入未知命令时自动查找并加载执行。

## 架构

```
输入命令 → Shell 查找内置表未命中
         → 检查 /bin/<命令> 存在
         → elf_load(): 从 SPIFFS 读取 ELF → PSRAM
         → 重定位：调整 literal pool 中的绝对地址
         → cache_hal_writeback_addr(): 刷新 DCache
         → esp_mmu_map(): PSRAM 物理页 → ICache (0x4280xxxx)
         → cache_hal_invalidate_addr(): 使 ICache 失效
         → proc_spawn_elf(): 创建 FreeRTOS 任务执行
         → _start(argc, argv, syscall_t*)
```

## 当前应用列表

| 应用 | 源文件 | 说明 | 所需系统调用 |
|------|--------|------|-------------|
| `hello` | `hello.c` | ELF 测试例程 | println, exit |
| `echo` | `echo.c` | 输出文本 | print, println, exit |
| `date` | `date.c` | 显示版本信息 | println, exit |
| `free` | `free.c` | 显示内存信息 | printf, get_free_heap, get_total_heap, get_free_psram, get_total_psram, exit |
| `uname` | `uname.c` | 显示系统信息 | printf, get_chip_desc, exit |
| `df` | `df.c` | 显示磁盘使用 | printf, get_fs_info, exit |

## 构建应用

### 前置条件

- PlatformIO 工具链（已随项目安装）
- Xtensa ESP32-S3 GCC 编译器（`toolchain-xtensa-esp-elf`）

### 编译所有应用

```bash
# 方式一：使用构建脚本
python tools/build_apps.py

# 方式二：一键完整构建
python tools/build_all.py
```

输出：
- `data/bin/<应用名>` — ELF 二进制文件，用于 SPIFFS 镜像
- `include/fonts/elf_<应用名>.h` — 嵌入固件的 C 头文件
- ELF 文件同时复制到 SPIFFS（固件首次启动时写入 `/bin/`）

### 编译单个应用

```bash
# 使用 Xtensa 工具链直接编译
xtensa-esp32s3-elf-gcc \
    -nostdlib -ffreestanding -Os \
    -mno-serialize-volatile -mabi=windowed \
    -fPIC \
    -I include \
    apps/<应用名>.c \
    -o data/bin/<应用名> \
    -nostartfiles -nodefaultlibs \
    -Wl,-N,--gc-sections \
    -Wl,-Ttext-segment=0x3F000000 \
    -Wl,--section-start=.text=0x3F000000 \
    -e _start

# 去除符号表减小体积
xtensa-esp32s3-elf-strip --strip-all data/bin/<应用名>
```

### 添加新应用

#### 步骤

1. **创建源文件** `apps/<应用名>.c`：

```c
#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    /* 应用逻辑 */
    sys->println("Hello from my app!");
    sys->exit(0);
}
```

2. **注册构建**：在 `tools/build_apps.py` 的 `apps` 列表中添加应用名：

```python
apps = ['echo', 'hello', 'date', 'free', 'uname', 'df', '你新加的应用名']
```

3. **注册固件**：在 `platformio.ini` 的 `build_flags` 中添加：

```ini
build_flags =
    ...
    -D ELF_你新加的应用名大写_DATA
```

4. **注册 VFS**：在 `kernel/vfs.c` 中添加头文件和安装调用：

```c
#ifdef ELF_新应用_DATA
#include "fonts/elf_新应用.h"
#endif

#ifdef ELF_新应用_DATA
    install_elf("新应用名", elf_新应用_data, ELF_新应用_SIZE);
#endif
```

5. **编译**：

```bash
# 重新生成 ELF 和嵌入头
python tools/build_apps.py

# 完整构建固件
python -m platformio run
```

## 应用 SDK 参考

ELF 应用通过 `syscall_t` 结构体与内核交互。结构体定义在 `include/app_sdk.h`。

### 输出函数

```c
void (*print)(const char *s);        // 输出字符串（UART + 显示）
void (*println)(const char *s);      // 输出字符串并换行
void (*print_char)(char c);          // 输出单个字符
void (*printf)(const char *fmt, ...);// 格式化输出
```

所有输出同时发送到串口和 ST7789 显示屏。系统输出使用浅灰色（0xC618），用户输入使用青色（0x5DFF）。

### 输入函数

```c
int  (*getchar)(void);               // 读取一个字符
void (*gets)(char *buf, int max);    // 读取一行字符串
```

### 文件系统

```c
void *(*fopen)(const char *path, const char *mode); // 打开文件
int   (*fread)(void *buf, int size, void *fp);      // 读文件
int   (*fwrite)(const void *buf, int size, void *fp);// 写文件
void  (*fclose)(void *fp);          // 关闭文件
int   (*fexist)(const char *path);  // 检查文件存在
int   (*ls)(const char *path, void *callback, void *arg); // 列出目录
```

`fopen` 的 mode 参数：
- `"r"` — 只读
- `"w"` — 写入（创建/截断）
- `"a"` — 追加

```c
// 文件读取示例
void *fp = sys->fopen("/home/data.txt", "r");
if (fp) {
    char buf[128];
    int n = sys->fread(buf, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    sys->printf("读取 %d 字节: %s\n", n, buf);
    sys->fclose(fp);
}
```

### 内存管理

```c
void *(*malloc)(int size);   // 分配堆内存
void  (*free)(void *p);      // 释放内存
```

注意：应用退出时不会自动释放内存，请在 `exit` 前手动 `free`。

### 系统函数

```c
void  (*exit)(int code);     // 退出进程（设置退出码，清理内存，删除任务）
void  (*reboot)(void);       // 重启 ESP32
void  (*sleep)(int ms);      // 延时（毫秒）
void  (*getcwd)(char *buf, int max);  // 获取当前工作目录
int   (*chdir)(const char *path);     // 切换工作目录
```

### 系统信息

```c
int   (*get_free_heap)(void);     // 获取空闲堆大小（字节）
int   (*get_total_heap)(void);    // 获取总堆大小（字节）
int   (*get_free_psram)(void);    // 获取空闲 PSRAM 大小（字节）
int   (*get_total_psram)(void);   // 获取总 PSRAM 大小（字节）
void  (*get_chip_desc)(char *buf, int max);  // 获取芯片描述字符串
void  (*get_fs_info)(int *total, int *used); // 获取文件系统统计
```

## 编译技术细节

### 编译选项

| 选项 | 说明 |
|------|------|
| `-nostdlib` | 不使用标准库 |
| `-ffreestanding` | 独立环境编译 |
| `-Os` | 优化体积 |
| `-mno-serialize-volatile` | Xtensa 不序列化 volatile 访问 |
| `-mabi=windowed` | Xtensa 窗口调用 ABI |
| `-fPIC` | 位置无关代码 |
| `-Wl,-N` | 设置文本段可读写（消除 RWX 警告） |
| `-Wl,--gc-sections` | 删除未使用段 |
| `-Wl,--section-start=.text=0x3F000000` | .text 段链接基址 |
| `-e _start` | 入口符号 |

### 重定位

ELF 编译时使用 `-fPIC` 生成位置无关代码，但 literal pool 中仍包含绝对地址（如字符串指针）。加载器扫描整个段：

```c
uint32_t delta = (uint32_t)base - min_vaddr;
for (uint32_t *p = base; p < end; p++) {
    if ((*p & 0xFFF00000) == 0x3F000000)  // 匹配 0x3Fxxxxxx 范围
        *p += delta;                       // 调整为实际地址
}
```

### MMU 映射

由于 ESP32-S3 的 PSRAM 默认不可执行，ELF 代码无法直接从 PSRAM 运行。需要：

1. `esp_mmu_vaddr_to_paddr()` 获取 PSRAM 物理地址
2. `esp_mmu_map()` 将物理页映射到 ICache 空间（0x4280xxxx）
3. 缓存一致性：写入后刷新 DCache，映射后使 ICache 失效

### Kconfig 要求

`sdkconfig.defaults` 中必须启用：

```ini
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_XIP_FROM_PSRAM=y
CONFIG_SPIRAM_RODATA=y
```

## 调试技巧

- ELF 加载日志（串口输出）：以 `I (xxxxx) ELF:` 开头
- 重定位日志：`I (6281) ELF: 重定位: delta=0x...`
- MMU 映射日志：`I (6281) ELF: 映射到ICache: data=0x... exec=0x...`
- 进程日志：`I (6282) PROC: exec '/bin/hello' PID=1`
- 常见错误：`InstructionFetchError` = PSRAM 未正确映射到 ICache
- 常见错误：`IllegalInstruction` = 代码已映射但数据错误（缓存一致性）
- 常见错误：`LoadStoreError` = 绝对地址未重定位

## 示例：free 应用

```c
#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    (void)argc; (void)argv;
    int total = sys->get_total_heap();
    int free_h = sys->get_free_heap();
    int total_p = sys->get_total_psram();
    int free_p = sys->get_free_psram();

    sys->printf("Heap:  %dK / %dK\n", (total - free_h) / 1024, total / 1024);
    if (total_p > 0)
        sys->printf("PSRAM: %dK / %dK\n", (total_p - free_p) / 1024, total_p / 1024);

    sys->exit(0);
}
```

## 示例：带参数的应用

```c
#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    sys->printf("argc=%d\n", argc);
    for (int i = 0; i < argc; i++)
        sys->printf("argv[%d]=%s\n", i, argv[i]);
    sys->exit(0);
}
```
