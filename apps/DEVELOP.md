# ELF 应用开发指南

本文介绍如何从零开始编写、编译、调试一个 ELF 应用。

## 目录

1. [环境准备](#1-环境准备)
2. [创建源文件](#2-创建源文件)
3. [SDK 参考](#3-sdk-参考)
4. [编译](#4-编译)
5. [注册到固件](#5-注册到固件)
6. [调试](#6-调试)
7. [完整示例](#7-完整示例)
8. [注意事项](#8-注意事项)

## 1. 环境准备

- PlatformIO 已安装
- Xtensa ESP32-S3 GCC 工具链（随 PlatformIO 自动安装）
- Python 3

验证工具链：

```bash
# 检查编译器
xtensa-esp32s3-elf-gcc --version

# 检查构建脚本
python tools/build_apps.py
```

## 2. 创建源文件

在 `apps/` 目录下创建 `.c` 文件，包含 `app_sdk.h` 并实现 `_start` 入口函数。

### 最小模板

```c
#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    sys->println("Hello from ELF!");
    sys->exit(0);
}
```

### 入口约定

`_start` 是 ELF 的唯一入口，由内核在加载后调用：

| 参数 | 类型 | 说明 |
|------|------|------|
| `argc` | `int` | 命令行参数个数 |
| `argv` | `char **` | 命令行参数数组 |
| `sys` | `syscall_t *` | 系统调用表指针 |

`argv[0]` 为命令名（如 `myapp`），`argv[1]` 起为实际参数。

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

### 返回值

ELF 不通过 `return` 返回值，应调用 `sys->exit(code)` 退出。`exit` 会清理内存、设置退出码并删除任务。

## 3. SDK 参考

所有系统调用通过 `syscall_t` 结构体访问，定义在 `include/app_sdk.h`。

### 3.1 输出

```c
void (*print)(const char *s);           // 输出字符串
void (*println)(const char *s);         // 输出字符串 + 换行
void (*print_char)(char c);             // 输出单个字符
void (*printf)(const char *fmt, ...);   // 格式化输出
```

所有输出同时发送到串口和 ST7789 显示屏。

```c
sys->print("Hello");
sys->println("World");
sys->print_char('!');
sys->printf("count = %d\n", 42);
```

### 3.2 输入

```c
int  (*getchar)(void);              // 读取一个字符（阻塞，-1=无输入）
void (*gets)(char *buf, int max);   // 读取一行（阻塞）
```

### 3.3 文件系统

```c
void *(*fopen)(const char *path, const char *mode);  // 打开文件
int   (*fread)(void *buf, int size, void *fp);       // 读文件
int   (*fwrite)(const void *buf, int size, void *fp); // 写文件
void  (*fclose)(void *fp);                           // 关闭文件
int   (*fexist)(const char *path);                    // 检查文件存在
```

`mode` 参数：
| 模式 | 说明 |
|------|------|
| `"r"` | 只读（文件必须存在） |
| `"w"` | 写入（创建/覆盖） |
| `"a"` | 追加（创建/追加） |

```c
// 读文件
void *fp = sys->fopen("/home/data.txt", "r");
if (fp) {
    char buf[256];
    int n = sys->fread(buf, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    sys->printf("read: %s\n", buf);
    sys->fclose(fp);
}

// 写文件
void *fp = sys->fopen("/home/out.txt", "w");
if (fp) {
    sys->fwrite("Hello\n", 6, fp);
    sys->fclose(fp);
}
```

### 3.4 内存

```c
void *(*malloc)(int size);   // 分配堆内存
void  (*free)(void *p);      // 释放内存
```

注意：ELF 退出时不会自动释放通过 `malloc` 分配的内存，请在 `exit` 前手动释放。

### 3.5 系统

```c
void  (*exit)(int code);     // 退出进程
void  (*reboot)(void);       // 重启系统
void  (*sleep)(int ms);      // 延时（毫秒）
void  (*getcwd)(char *buf, int max);   // 获取当前目录
int   (*chdir)(const char *path);      // 切换目录
```

### 3.6 系统信息

```c
int   (*get_free_heap)(void);      // 空闲堆大小（字节）
int   (*get_total_heap)(void);     // 总堆大小（字节）
int   (*get_free_psram)(void);     // 空闲 PSRAM（字节）
int   (*get_total_psram)(void);    // 总 PSRAM（字节）
void  (*get_chip_desc)(char *buf, int max);  // 芯片描述
void  (*get_fs_info)(int *total, int *used); // 文件系统统计
```

```c
// 显示内存信息
int total = sys->get_total_heap();
int free_h = sys->get_free_heap();
sys->printf("Heap: %dK / %dK\n", (total - free_h) / 1024, total / 1024);

// 显示芯片信息
char desc[64];
sys->get_chip_desc(desc, sizeof(desc));
sys->printf("Chip: %s\n", desc);

// 显示文件系统
int fs_total, fs_used;
sys->get_fs_info(&fs_total, &fs_used);
sys->printf("FS: %dK / %dK\n", fs_used / 1024, fs_total / 1024);
```

## 4. 编译

### 4.1 注册到构建列表

编辑 `tools/build_apps.py`，在 `apps` 列表中添加应用名：

```python
apps = ['echo', 'hello', 'date', 'free', 'uname', 'df', '你的应用名']
```

### 4.2 执行编译

```bash
# 仅编译 ELF
python tools/build_apps.py

# 完整构建（ELF + 固件 + 烧录）
python tools/build_all.py
```

输出文件：
- `data/bin/<app>` — ELF 二进制文件，部署到 SPIFFS `/bin/`
- `include/fonts/elf_<app>.h` — 嵌入固件的 C 头文件

### 4.3 手动编译

```bash
xtensa-esp32s3-elf-gcc \
    -nostdlib -ffreestanding -Os \
    -mno-serialize-volatile -mabi=windowed \
    -fPIC \
    -I include \
    apps/myapp.c \
    -o data/bin/myapp \
    -nostartfiles -nodefaultlibs \
    -Wl,-N,--gc-sections \
    -Wl,-Ttext-segment=0x3F000000 \
    -Wl,--section-start=.text=0x3F000000 \
    -e _start

xtensa-esp32s3-elf-strip --strip-all data/bin/myapp
```

## 5. 注册到固件

### 5.1 添加头文件引用

编辑 `kernel/vfs.c`，添加：

```c
#ifdef ELF_MYAPP_DATA
#include "fonts/elf_myapp.h"
#endif
```

### 5.2 添加安装调用

```c
#ifdef ELF_MYAPP_DATA
    install_elf("myapp", elf_myapp_data, ELF_MYAPP_SIZE);
#endif
```

### 5.3 添加构建标志

编辑 `platformio.ini`：

```ini
build_flags =
    ...
    -D ELF_MYAPP_DATA
```

### 5.4 完整构建固件

```bash
python -m platformio run --target upload --upload-port COM3
```

重启后输入 `myapp` 即可运行。

## 6. 调试

### 6.1 串口日志

ELF 加载和执行的关键日志：

| 日志 | 说明 |
|------|------|
| `ELF: 加载 '...' 完成: entry=0x... size=N` | ELF 加载成功 |
| `ELF: 重定位: delta=0x...` | 绝对地址调整值 |
| `ELF: 映射到ICache: data=0x... exec=0x...` | PSRAM → ICache 映射 |
| `PROC: exec '...' PID=N` | 进程已创建 |

### 6.2 常见错误

**加载失败**
```
ELF: 内存不足, 需要 N bytes
```
ELF 文件过大或 PSRAM 不足。单个 ELF 最大 2MB。

**InstructionFetchError**
```
Guru Meditation Error: Core 0 panic'ed (InstructionFetchError)
```
PSRAM 未正确映射到 ICache。检查 `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`。

**IllegalInstruction**
```
Guru Meditation Error: Core 0 panic'ed (IllegalInstruction)
```
代码已映射但数据错误（缓存一致性问题）。内核会自动处理，一般不会出现此问题。

**LoadStoreError**
```
Guru Meditation Error: Core 0 panic'ed (LoadStoreError)
```
代码访问了非法内存地址，通常是绝对地址未重定位。检查字符串等常量是否正确引用。

### 6.3 最大限制

| 项目 | 限制 |
|------|------|
| 单个 ELF 最大 | 2MB |
| 固件中 ELF 数量 | 不限（受 SPIFFS 空间限制，~12MB） |
| 任务栈 | 4KB（FreeRTOS 任务包装器，非 ELF 栈） |
| ELF 自身栈 | 由 ELF 链接脚本控制，默认 256KB |

## 7. 完整示例

### 7.1 文件读写示例

```c
#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    /* 写文件 */
    void *fp = sys->fopen("/tmp/test.txt", "w");
    if (!fp) {
        sys->println("写打开失败");
        sys->exit(1);
    }
    sys->fwrite("Hello from ELF!\n", 16, fp);
    sys->fclose(fp);

    /* 读文件 */
    fp = sys->fopen("/tmp/test.txt", "r");
    if (!fp) {
        sys->println("读打开失败");
        sys->exit(1);
    }
    char buf[64];
    int n = sys->fread(buf, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    sys->printf("读取 %d 字节: %s", n, buf);
    sys->fclose(fp);

    sys->exit(0);
}
```

### 7.2 带参数的命令

```c
#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    if (argc < 2) {
        sys->println("用法: hello <名字>");
        sys->exit(1);
    }
    sys->printf("你好, %s!\n", argv[1]);
    sys->exit(0);
}
```

### 7.3 使用内存

```c
#include "../include/app_sdk.h"
#include <string.h>  // 需要实现 memcpy 等

void _start(int argc, char **argv, syscall_t *sys)
{
    char *buf = sys->malloc(1024);
    if (!buf) {
        sys->println("内存不足");
        sys->exit(1);
    }
    /* 使用 buf ... */
    sys->free(buf);
    sys->exit(0);
}
```

> 注意：标准库函数（`memcpy`, `memset`, `strlen` 等）需要自行实现或链接 libc。推荐自行实现简单的版本，避免增大 ELF 体积。

## 8. 注意事项

### 8.1 编译选项

| 选项 | 必须 | 说明 |
|------|------|------|
| `-nostdlib` | 是 | 不使用标准库 |
| `-ffreestanding` | 是 | 独立环境 |
| `-fPIC` | 是 | 位置无关代码 |
| `-mno-serialize-volatile` | 是 | Xtensa 特定 |
| `-mabi=windowed` | 是 | Xtensa 窗口 ABI |
| `-e _start` | 是 | 指定入口 |

### 8.2 不支持的 C 特性

- ❌ `printf` 系列（用 `sys->printf` 代替）
- ❌ `malloc`/`free`（用 `sys->malloc`/`sys->free` 代替）
- ❌ `fopen`/`fread`/`fwrite`/`fclose`（用 `sys->` 版本代替）
- ❌ 全局变量（可能被链接器放在意外位置）
- ✅ 局部变量
- ✅ 函数调用
- ✅ 指针运算

### 8.3 避免使用

- `static` 局部变量（可能被放在 `.bss` 段，需确认链接脚本支持）
- 全局 `const` 字符串（会自动放在 `.rodata`，会正确加载和重定位）
- 浮点运算（ESP32-S3 浮点支持有限）

### 8.4 代码风格

```c
#include "../include/app_sdk.h"

void _start(int argc, char **argv, syscall_t *sys)
{
    /* 处理参数 */
    if (argc < 2) {
        sys->println("用法: myapp <参数>");
        sys->exit(1);
    }

    /* 主要逻辑 */
    sys->printf("处理: %s\n", argv[1]);

    /* 清理并退出 */
    sys->exit(0);
}
```
