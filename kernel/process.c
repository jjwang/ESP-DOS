#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
/* ets_printf 输出到硬件UART, 绕过VFS控制台 */
int ets_printf(const char *fmt, ...);
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "process.h"
#include "elf.h"
#include "vfs.h"
#include "terminal.h"
extern terminal_t g_terminal;

static const char *TAG = "PROC";

static pcb_t g_proc_table[MAX_PROC];
static uint16_t g_next_pid = 1;

int proc_init(void)
{
    memset(g_proc_table, 0, sizeof(g_proc_table));
    ESP_LOGI(TAG, "进程系统初始化");
    return 0;
}

static int find_free_slot(void)
{
    for (int i = 0; i < MAX_PROC; i++)
        if (g_proc_table[i].state == PROC_FREE) return i;
    return -1;
}

int proc_spawn(const char *name, proc_entry_t entry, void *arg, int prio, int stack_size)
{
    int slot = find_free_slot();
    if (slot < 0) return -1;

    uint16_t pid = g_next_pid++;
    g_proc_table[slot].pid = pid;
    g_proc_table[slot].state = PROC_RUNNING;
    g_proc_table[slot].exit_code = 0;
    g_proc_table[slot].elf_base = NULL;
    strncpy(g_proc_table[slot].name, name, PROC_NAME_LEN - 1);

    char task_name[PROC_NAME_LEN];
    snprintf(task_name, sizeof(task_name), "proc_%d", pid);

    BaseType_t ret = xTaskCreatePinnedToCore(entry, task_name, stack_size, arg, prio,
                                              &g_proc_table[slot].task, 0);
    if (ret != pdPASS) {
        g_proc_table[slot].state = PROC_FREE;
        return -1;
    }

    ESP_LOGI(TAG, "spawn %s PID=%d", name, pid);
    return pid;
}

/* ELF输出 — 同时输出到UART和显示终端 */
static void elf_print(const char *s)
{
    if (s) {
        ets_printf("%s", s);
        term_puts(&g_terminal, s);
    }
}
static void elf_println(const char *s)
{
    if (s) {
        ets_printf("%s\n", s);
        term_puts(&g_terminal, s);
    }
    term_putchar(&g_terminal, '\n');
}
static void elf_putchar(char c)
{
    char buf[2] = {c, 0};
    ets_printf("%s", buf);
    term_putchar(&g_terminal, c);
}
static void elf_printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        ets_printf("%s", buf);
        term_puts(&g_terminal, buf);
    }
}

/* VFS 桥接函数 */
static void *elf_fopen(const char *path, const char *mode)
{
    int flags = 0;
    if (mode[0] == 'r') flags = VFS_O_RDONLY;
    else if (mode[0] == 'w') flags = VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC;
    else if (mode[0] == 'a') flags = VFS_O_WRONLY | VFS_O_CREAT | VFS_O_APPEND;
    return vfs_open(path, flags);
}
static int elf_fread(void *buf, int size, void *fp)
{
    return vfs_read((vfs_file_t *)fp, buf, size);
}
static int elf_fwrite(const void *buf, int size, void *fp)
{
    return vfs_write((vfs_file_t *)fp, buf, size);
}
static void elf_fclose(void *fp)
{
    vfs_close((vfs_file_t *)fp);
}
static int elf_fexist(const char *path)
{
    return vfs_exists(path);
}
static int elf_get_free_heap(void) { return heap_caps_get_free_size(MALLOC_CAP_DEFAULT); }
static int elf_get_total_heap(void) { return heap_caps_get_total_size(MALLOC_CAP_DEFAULT); }
static int elf_get_free_psram(void) { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
static int elf_get_total_psram(void) { return heap_caps_get_total_size(MALLOC_CAP_SPIRAM); }
static void elf_get_chip_desc(char *buf, int max)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    snprintf(buf, max, "ESP32-S3 rev%d %d-core", info.revision, info.cores);
}
static void elf_get_fs_info(int *total, int *used)
{
    uint32_t t = 0, u = 0;
    vfs_info(&t, &u);
    if (total) *total = (int)t;
    if (used) *used = (int)u;
}

/* ELF包装器: 加载ELF后创建任务执行 */
typedef struct {
    void *entry_point;
    void *elf_base;
    uint16_t pid_slot;
    syscall_table_t sys;
    int argc;
    char **argv;
} elf_wrapper_arg_t;

/* 退出处理 — 通过task句柄查找PCB */
static void sys_exit_handler(int code)
{
    TaskHandle_t cur = xTaskGetCurrentTaskHandle();
    for (int i = 0; i < MAX_PROC; i++) {
        if (g_proc_table[i].task == cur && g_proc_table[i].state == PROC_RUNNING) {
            g_proc_table[i].exit_code = code;
            g_proc_table[i].state = PROC_ZOMBIE;
            if (g_proc_table[i].elf_base) {
                free(g_proc_table[i].elf_base);
                g_proc_table[i].elf_base = NULL;
            }
            break;
        }
    }
    vTaskDelete(NULL);
}

static void elf_entry_wrapper(void *arg)
{
    elf_wrapper_arg_t *w = (elf_wrapper_arg_t *)arg;
    void (*entry)(int, char **, syscall_table_t *) =
        (void (*)(int, char **, syscall_table_t *))w->entry_point;

    entry(w->argc, w->argv, &w->sys);

    /* 若entry返回（ELF未调用exit），在此清理 */
    int slot = w->pid_slot;
    g_proc_table[slot].state = PROC_ZOMBIE;
    if (g_proc_table[slot].elf_base) {
        free(g_proc_table[slot].elf_base);
        g_proc_table[slot].elf_base = NULL;
    }
    vTaskDelete(NULL);
}

int proc_spawn_elf(const char *path, int argc, char **argv)
{
    elf_load_result_t elf;
    if (elf_load(path, &elf) != 0) return -1;

    int slot = find_free_slot();
    if (slot < 0) { elf_free(&elf); return -1; }

    uint16_t pid = g_next_pid++;
    g_proc_table[slot].pid = pid;
    g_proc_table[slot].state = PROC_RUNNING;
    g_proc_table[slot].exit_code = 0;
    g_proc_table[slot].elf_base = elf.base_addr;
    snprintf(g_proc_table[slot].name, PROC_NAME_LEN - 1, "%s", strrchr(path, '/') ? strrchr(path, '/') + 1 : path);

    /* 恢复argv[0]为ELF文件名（而非完整路径） */
    char *cmd_name = strrchr(path, '/') ? strrchr(path, '/') + 1 : (char *)path;

    /* 准备包装参数 (堆分配, 避免任务启动前释放) */
    elf_wrapper_arg_t *w = malloc(sizeof(elf_wrapper_arg_t));
    if (!w) { elf_free(&elf); return -1; }
    w->entry_point = elf.entry_point;
    w->elf_base = elf.base_addr;
    w->pid_slot = slot;

    /* 构造传递给ELF的argv */
    w->argc = argc;
    w->argv = NULL;
    if (argc > 0 && argv) {
        w->argv = malloc(sizeof(char *) * (argc + 1));
        if (!w->argv) { free(w); elf_free(&elf); return -1; }
        w->argv[0] = cmd_name; /* argv[0] = 命令名 */
        for (int i = 1; i < argc; i++)
            w->argv[i] = argv[i];
        w->argv[argc] = NULL;
    }

    w->sys.print = elf_print;
    w->sys.println = elf_println;
    w->sys.print_char = elf_putchar;
    w->sys.printf = elf_printf;
    w->sys.getchar = NULL;
    w->sys.gets = NULL;
    w->sys.fopen = elf_fopen;
    w->sys.fread = elf_fread;
    w->sys.fwrite = elf_fwrite;
    w->sys.fclose = elf_fclose;
    w->sys.fexist = elf_fexist;
    w->sys.ls = NULL;
    w->sys.malloc = (void *(*)(int))malloc;
    w->sys.free = (void (*)(void *))free;
    w->sys.exit = sys_exit_handler;
    w->sys.reboot = (void (*)(void))esp_restart;
    w->sys.sleep = (void (*)(int))vTaskDelay;
    w->sys.getcwd = NULL;
    w->sys.chdir = NULL;
    w->sys.get_free_heap = elf_get_free_heap;
    w->sys.get_total_heap = elf_get_total_heap;
    w->sys.get_free_psram = elf_get_free_psram;
    w->sys.get_total_psram = elf_get_total_psram;
    w->sys.get_chip_desc = elf_get_chip_desc;
    w->sys.get_fs_info = elf_get_fs_info;

    char task_name[PROC_NAME_LEN];
    snprintf(task_name, sizeof(task_name), "elf_%d", pid);

    if (xTaskCreatePinnedToCore(elf_entry_wrapper, task_name, 4096, w, 5,
                                 &g_proc_table[slot].task, 0) != pdPASS) {
        g_proc_table[slot].state = PROC_FREE;
        free(w->argv); free(w); elf_free(&elf);
        return -1;
    }

    ESP_LOGI(TAG, "exec '%s' PID=%d", path, pid);
    return pid;
}

int proc_kill(uint16_t pid)
{
    for (int i = 0; i < MAX_PROC; i++) {
        if (g_proc_table[i].pid == pid && g_proc_table[i].state == PROC_RUNNING) {
            vTaskDelete(g_proc_table[i].task);
            g_proc_table[i].state = PROC_ZOMBIE;
            if (g_proc_table[i].elf_base) {
                free(g_proc_table[i].elf_base);
                g_proc_table[i].elf_base = NULL;
            }
            return 0;
        }
    }
    return -1;
}

uint16_t proc_wait_any(int *exit_code)
{
    while (1) {
        for (int i = 0; i < MAX_PROC; i++) {
            if (g_proc_table[i].state == PROC_ZOMBIE) {
                uint16_t pid = g_proc_table[i].pid;
                if (exit_code) *exit_code = g_proc_table[i].exit_code;
                g_proc_table[i].state = PROC_FREE;
                return pid;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

pcb_t *proc_get(uint16_t pid)
{
    for (int i = 0; i < MAX_PROC; i++)
        if (g_proc_table[i].pid == pid) return &g_proc_table[i];
    return NULL;
}

int proc_list(pcb_t *buf, int max)
{
    int count = 0;
    for (int i = 0; i < MAX_PROC && count < max; i++) {
        if (g_proc_table[i].state != PROC_FREE) {
            memcpy(&buf[count], &g_proc_table[i], sizeof(pcb_t));
            count++;
        }
    }
    return count;
}
