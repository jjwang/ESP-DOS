#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include <ctype.h>
#include "shell.h"
#include "display_st7789.h"
#include "process.h"
#include "vfs.h"
#include "ds3231.h"
#include "tca8418.h"
#include "editor.h"
#include "dino.h"

/* 分页输出 */
extern QueueHandle_t g_input_queue;

void shell_puts(shell_t *sh, const char *s)
{
    term_puts(sh->term, s);
}

/* ---- 命令�?---- */
typedef struct {
    const char *name;
    const char *desc;
    const char *usage;
    void (*handler)(shell_t *sh, int argc, char **argv);
} cmd_entry_t;

static void cmd_help(shell_t *sh, int argc, char **argv);
static void cmd_ls(shell_t *sh, int argc, char **argv);
static void cmd_cat(shell_t *sh, int argc, char **argv);
static void cmd_cd(shell_t *sh, int argc, char **argv);
static void cmd_mkdir(shell_t *sh, int argc, char **argv);
static void cmd_rm(shell_t *sh, int argc, char **argv);
static void cmd_clear(shell_t *sh, int argc, char **argv);
static void cmd_ps(shell_t *sh, int argc, char **argv);
static void cmd_reboot(shell_t *sh, int argc, char **argv);
static void cmd_touch(shell_t *sh, int argc, char **argv);
static void cmd_write(shell_t *sh, int argc, char **argv);
static void cmd_mv(shell_t *sh, int argc, char **argv);
static void cmd_cp(shell_t *sh, int argc, char **argv);
static void cmd_exec(shell_t *sh, int argc, char **argv);
static void cmd_kill(shell_t *sh, int argc, char **argv);
static void cmd_wifi(shell_t *sh, int argc, char **argv);
static void cmd_sysinfo(shell_t *sh, int argc, char **argv);
static void cmd_date(shell_t *sh, int argc, char **argv);
static void cmd_time_cmd(shell_t *sh, int argc, char **argv);
static void cmd_edit(shell_t *sh, int argc, char **argv);
static void cmd_dino(shell_t *sh, int argc, char **argv);
static void shell_set_input_color(shell_t *sh);

static const cmd_entry_t cmd_table[] = {
    {"help",   "显示帮助信息",        "HELP [命令]", cmd_help},
    {"ls",     "列出目录内容",        "LS [路径]", cmd_ls},
    {"dir",    "列出目录内容",        "DIR [路径]", cmd_ls},
    {"type",   "显示文件内容",        "TYPE <文件>", cmd_cat},
    {"cd",     "显示/切换目录",      "CD [路径]", cmd_cd},
    {"cls",    "清屏",               "CLS", cmd_clear},
    {"md",     "创建目录",           "MD <目录名>", cmd_mkdir},
    {"del",    "删除文件或目录",      "DEL [-R] <路径>", cmd_rm},
    {"ren",    "重命名文件",          "REN <旧名> <新名>", cmd_mv},
    {"copy",   "复制文件",            "COPY <源> <目标>", cmd_cp},
    {"ps",     "显示进程信息",        "PS", cmd_ps},
    {"reboot", "重启系统",            "REBOOT", cmd_reboot},
    {"touch",  "创建空文件",          "TOUCH <文件>", cmd_touch},
    {"write",  "写入文件",            "WRITE <文件> <内容>", cmd_write},
    {"exec",   "运行ELF程序",         "EXEC <ELF文件>", cmd_exec},
    {"kill",   "终止进程",            "KILL <PID>", cmd_kill},
    {"wifi",   "连接WiFi网络",        "WIFI <SSID> <PASSWORD>", cmd_wifi},
    {"sysinfo","显示系统信息",        "SYSINFO", cmd_sysinfo},
    {"date",   "显示/设置日期",       "DATE [YYYY-MM-DD]", cmd_date},
    {"time",   "显示/设置时间",       "TIME [HH:MM:SS]", cmd_time_cmd},
    {"edit",   "全屏文本编辑器",      "EDIT [文件]", cmd_edit},
    {"dino",   "小恐龙游戏",         "DINO", cmd_dino},
    {NULL, NULL, NULL, NULL}
};

/* ---- ELF/进程命令 ---- */
static void cmd_exec(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) { shell_puts(sh, "用法: exec <ELF文件>\n"); return; }
    shell_puts(sh, "正在加载: ");
    shell_puts(sh, argv[1]);
    shell_puts(sh, "\n");
    int pid = proc_spawn_elf(argv[1], argc - 1, argv + 1);
    if (pid < 0) {
        shell_puts(sh, "加载失败\n");
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "PID=%d running, wait...\n", pid);
    shell_puts(sh, buf);
    int code;
    uint16_t done = proc_wait_any(&code);
    snprintf(buf, sizeof(buf), "PID=%d exit (code=%d)\n", done, code);
    shell_puts(sh, buf);
}

static void cmd_kill(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) { shell_puts(sh, "用法: kill <PID>\n"); return; }
    int pid = atoi(argv[1]);
    if (proc_kill(pid) == 0)
        shell_puts(sh, "已终止\n");
    else
        shell_puts(sh, "进程不存在\n");
}

/* ---- 命令实现 ---- */

static void cmd_help(shell_t *sh, int argc, char **argv)
{
    shell_puts(sh, "\nESP-DOS - 可用命令:\n");
    shell_puts(sh, "================================\n");

    if (argc > 1) {
        char cmd[32];
        strncpy(cmd, argv[1], sizeof(cmd) - 1);
        cmd[sizeof(cmd) - 1] = '\0';
        for (int i = 0; cmd[i]; i++) cmd[i] = tolower((unsigned char)cmd[i]);
        for (int i = 0; cmd_table[i].name; i++) {
            if (strcmp(cmd, cmd_table[i].name) == 0) {
                char up[32];
                strcpy(up, cmd_table[i].name);
                for (int j = 0; up[j]; j++) up[j] = toupper((unsigned char)up[j]);
                shell_puts(sh, up);
                shell_puts(sh, " - ");
                shell_puts(sh, cmd_table[i].desc);
                shell_puts(sh, "\n  用法: ");
                shell_puts(sh, cmd_table[i].usage);
                shell_puts(sh, "\n");
                return;
            }
        }
        shell_puts(sh, "未知命令: ");
        shell_puts(sh, argv[1]);
        shell_puts(sh, "\n");
        return;
    }

    int lines = 2;
    for (int i = 0; cmd_table[i].name; i++) {
        char line[64];
        char up[16];
        strcpy(up, cmd_table[i].name);
        for (int j = 0; up[j]; j++) up[j] = toupper((unsigned char)up[j]);
        int pad = 10 - strlen(cmd_table[i].name);
        if (pad < 1) pad = 1;
        snprintf(line, sizeof(line), "  %s%*s%s\n", up, pad, "", cmd_table[i].desc);
        shell_puts(sh, line);
        lines++;

        if (lines >= TERM_ROWS - 1 && cmd_table[i + 1].name) {
            shell_set_input_color(sh);
            shell_puts(sh, "--- 按任意键继续 ---");
            term_render(sh->term);
            uint16_t ch;
            xQueueReceive(g_input_queue, &ch, portMAX_DELAY);
            term_clear_line(sh->term);
            term_render(sh->term);
            lines = 0;
        }
    }
}

static void cmd_ls(shell_t *sh, int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : ".";
    char buf[256];

    vfs_dir_t *dir = vfs_opendir(path);
    if (!dir) {
        snprintf(buf, sizeof(buf), "File not found - %s\n", path);
        shell_puts(sh, buf);
        return;
    }

    /* 将路径转换为DOS风格 */
    char dos_path[128];
    strncpy(dos_path, path, sizeof(dos_path) - 1);
    for (int i = 0; dos_path[i]; i++)
        if (dos_path[i] == '/') dos_path[i] = '\\';
    snprintf(buf, sizeof(buf), " Directory of %s\n", dos_path);
    shell_puts(sh, buf);
    shell_puts(sh, "\n");

    int total_files = 0;
    vfs_dirent_t *entry;
    while ((entry = vfs_readdir(dir)) != NULL) {
        char date[32] = "";
        if (entry->mtime) {
            time_t t = (time_t)entry->mtime;
            struct tm *tm = localtime(&t);
            strftime(date, sizeof(date), "%Y/%m/%d  %H:%M", tm);
        }
        if (entry->type == VFS_DIR) {
            snprintf(buf, sizeof(buf), "%s    <DIR>  %s\n", date, entry->name);
        } else if (entry->type == VFS_EXEC) {
            snprintf(buf, sizeof(buf), "%s    <EXEC> %s\n", date, entry->name);
        } else {
            snprintf(buf, sizeof(buf), "%s  %8lu  %s\n", date, (unsigned long)entry->size, entry->name);
        }
        shell_puts(sh, buf);
        total_files++;
    }

    snprintf(buf, sizeof(buf), "%8d File(s)\n\n", total_files);
    shell_puts(sh, buf);

    vfs_closedir(dir);
}

static void cmd_cat(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) {
        shell_puts(sh, "用法: cat <文件>\n");
        return;
    }

    vfs_file_t *file = vfs_open(argv[1], VFS_O_RDONLY);
    if (!file) {
        shell_puts(sh, "无法打开文件: ");
        shell_puts(sh, argv[1]);
        shell_puts(sh, "\n");
        return;
    }

    char buf[128];
    int bytes;
    while ((bytes = vfs_read(file, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = '\0';
        shell_puts(sh, buf);
    }

    if (bytes < 0) {
        shell_puts(sh, "\n读取错误\n");
    }

    vfs_close(file);
}

static void cmd_cd(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) {
        char dos[128];
        strncpy(dos, sh->cwd, sizeof(dos) - 1);
        for (int i = 0; dos[i]; i++)
            if (dos[i] == '/') dos[i] = '\\';
        shell_puts(sh, dos);
        shell_puts(sh, "\n");
        return;
    }
    const char *path = argv[1];
    if (vfs_chdir(path) != 0) {
        shell_puts(sh, "目录不存在");
        shell_puts(sh, path);
        shell_puts(sh, "\n");
        return;
    }
    vfs_getcwd(sh->cwd, sizeof(sh->cwd));
}

static void cmd_mkdir(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) {
        shell_puts(sh, "用法: mkdir <目录�?\n");
        return;
    }
    if (vfs_mkdir(argv[1]) != 0) {
        shell_puts(sh, "创建目录失败: ");
        shell_puts(sh, argv[1]);
        shell_puts(sh, "\n");
    }
}

static void cmd_rm(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) {
        shell_puts(sh, "用法: rm [-r] <路径>\n");
        return;
    }

    int recursive = 0;
    const char *path = argv[1];
    if (strcmp(argv[1], "-r") == 0) {
        recursive = 1;
        path = (argc > 2) ? argv[2] : NULL;
        if (!path) {
            shell_puts(sh, "用法: rm [-r] <路径>\n");
            return;
        }
    }

    if (!recursive) {
        vfs_stat_t st;
        if (vfs_stat(path, &st) == 0 && st.type == VFS_DIR) {
            shell_puts(sh, "是目�? 使用 rm -r 删除\n");
            return;
        }
    }

    if (vfs_remove(path) != 0) {
        shell_puts(sh, "删除失败: ");
        shell_puts(sh, path);
        shell_puts(sh, "\n");
    }
}

static void cmd_clear(shell_t *sh, int argc, char **argv)
{
    (void)argc; (void)argv;
    term_clear(sh->term);
}

static void cmd_ps(shell_t *sh, int argc, char **argv)
{
    (void)argc; (void)argv;
    shell_puts(sh, "PID  NAME          STATE\n");
    shell_puts(sh, "---  ------------  -----\n");

    TaskStatus_t *tasks = NULL;
    uint32_t total = uxTaskGetNumberOfTasks();
    tasks = malloc(sizeof(TaskStatus_t) * total);
    if (tasks) {
        total = uxTaskGetSystemState(tasks, total, NULL);
        for (uint32_t i = 0; i < total; i++) {
            char line[64];
            const char *state;
            switch (tasks[i].eCurrentState) {
                case eRunning:   state = "运行"; break;
                case eReady:     state = "就绪"; break;
                case eBlocked:   state = "阻塞"; break;
                case eSuspended: state = "暂停"; break;
                case eDeleted:   state = "删除"; break;
                default:         state = "未知"; break;
            }
            snprintf(line, sizeof(line), "%-4d %-14s %s\n",
                     (int)tasks[i].xTaskNumber,
                     tasks[i].pcTaskName,
                     state);
            shell_puts(sh, line);
        }
        free(tasks);
    }
}

static void cmd_reboot(shell_t *sh, int argc, char **argv)
{
    (void)argc; (void)argv;
    shell_puts(sh, "系统将在3秒后重启...\n");
    term_render(sh->term);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

static void cmd_touch(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) {
        shell_puts(sh, "用法: touch <文件>\n");
        return;
    }
    vfs_file_t *f = vfs_open(argv[1], VFS_O_WRONLY | VFS_O_CREAT);
    if (f) {
        vfs_close(f);
    } else {
        shell_puts(sh, "创建文件失败\n");
    }
}

static void cmd_write(shell_t *sh, int argc, char **argv)
{
    if (argc < 3) {
        shell_puts(sh, "用法: write <文件> <内容>\n");
        return;
    }
    vfs_file_t *f = vfs_open(argv[1], VFS_O_WRONLY | VFS_O_CREAT);
    if (!f) {
        shell_puts(sh, "打开文件失败\n");
        return;
    }
    vfs_write(f, argv[2], strlen(argv[2]));
    vfs_close(f);
}


static void cmd_mv(shell_t *sh, int argc, char **argv)
{
    if (argc < 3) {
        shell_puts(sh, "用法: mv <�? <目标>\n");
        return;
    }
    if (vfs_rename(argv[1], argv[2]) != 0) {
        shell_puts(sh, "移动/重命名失败\n");
    }
}

static void cmd_cp(shell_t *sh, int argc, char **argv)
{
    if (argc < 3) {
        shell_puts(sh, "用法: cp <�? <目标>\n");
        return;
    }

    vfs_file_t *src = vfs_open(argv[1], VFS_O_RDONLY);
    if (!src) {
        shell_puts(sh, "无法打开源文件\n");
        return;
    }

    vfs_file_t *dst = vfs_open(argv[2], VFS_O_WRONLY | VFS_O_CREAT);
    if (!dst) {
        shell_puts(sh, "无法创建目标文件\n");
        vfs_close(src);
        return;
    }

    char buf[256];
    int bytes;
    while ((bytes = vfs_read(src, buf, sizeof(buf))) > 0) {
        vfs_write(dst, buf, bytes);
    }

    vfs_close(src);
    vfs_close(dst);
    shell_puts(sh, "复制完成\n");
}

/* ---- WiFi命令 ---- */
static void cmd_wifi(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) {
        /* 显示当前状�?*/
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            char line[128];
            snprintf(line, sizeof(line), "已连�? %s (rssi=%d)\n", (char *)ap.ssid, ap.rssi);
            shell_puts(sh, line);
        } else {
            shell_puts(sh, "未连�? 用法: wifi <ssid> <password>\n");
        }
        return;
    }
    if (argc < 3) {
        shell_puts(sh, "用法: wifi <ssid> <password>\n");
        return;
    }

    shell_puts(sh, "正在连接WiFi...\n");
    term_render(sh->term);

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, argv[1], sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, argv[2], sizeof(wifi_config.sta.password) - 1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();

    /* 连接期间降低WiFi日志级别, 避免刷屏 */
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    for (int i = 0; i < 50; i++) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            char buf[64];
            snprintf(buf, sizeof(buf), "已连�? %s (rssi=%d)\n", (char *)ap.ssid, ap.rssi);
            shell_puts(sh, buf);
            term_render(sh->term);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    shell_puts(sh, "连接超时\n");
    term_render(sh->term);
}

/* ---- 系统资源命令 ---- */
static void cmd_sysinfo(shell_t *sh, int argc, char **argv)
{
    (void)argc; (void)argv;
    char line[128];

    /* CPU */
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_sz = 0;
    esp_flash_get_size(NULL, &flash_sz);

    shell_puts(sh, "-- CPU --\n");
    snprintf(line, sizeof(line), "ESP32-S3 rev%d  240MHz  %d cores\n", chip.revision, chip.cores);
    shell_puts(sh, line);

    size_t total_int = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    shell_puts(sh, "Internal SRAM:\n");
    snprintf(line, sizeof(line), "  free %dK / total %dK (%d%%)\n",
             (int)(free_int / 1024), (int)(total_int / 1024),
             (int)(free_int * 100 / total_int));
    shell_puts(sh, line);
    snprintf(line, sizeof(line), "  cache+code: %dK\n", (int)((512 - (int)total_int) / 1024));
    shell_puts(sh, line);

    /* 文件系统 */
    uint32_t fs_total = 0, fs_used = 0;
    vfs_info(&fs_total, &fs_used);
    uint32_t fs_free = fs_total - fs_used;
    int pct = fs_total > 0 ? (int)(fs_used * 100 / fs_total) : 0;

    shell_puts(sh, "-- Storage --\n");
    snprintf(line, sizeof(line), "Flash: %dMB   SPIFFS: %dKB\n",
             (int)(flash_sz / 1024 / 1024), (int)(fs_total / 1024));
    shell_puts(sh, line);
    snprintf(line, sizeof(line), "PSRAM: %dMB\n", (int)(total_psram / 1024 / 1024));
    shell_puts(sh, line);
    snprintf(line, sizeof(line), "SPIFFS free: %dK/%dK (%d%%)\n",
             (int)(fs_free / 1024), (int)(fs_total / 1024), pct);
    shell_puts(sh, line);
}

static void cmd_date(shell_t *sh, int argc, char **argv)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    const char *dow[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    char buf[64];

    if (argc > 1) {
        int y, m, d;
        if (sscanf(argv[1], "%d-%d-%d", &y, &m, &d) == 3) {
            struct tm tm = {0};
            tm.tm_year = y - 1900;
            tm.tm_mon = m - 1;
            tm.tm_mday = d;
            tm.tm_hour = 12;
            time_t t = mktime(&tm);
            struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ds3231_set_time(&tm);
            shell_puts(sh, "Date set.\n");
        } else {
            shell_puts(sh, "Format: DATE YYYY-MM-DD\n");
        }
        return;
    }

    snprintf(buf, sizeof(buf), "Current date is: %s %02d-%02d-%04d\n",
             dow[t->tm_wday], t->tm_mon + 1, t->tm_mday, t->tm_year + 1900);
    shell_puts(sh, buf);
    shell_set_input_color(sh);
    shell_puts(sh, "Enter new date (YYYY-MM-DD): ");
    term_render(sh->term);
    uint16_t input[32];
    int pos = 0;
    while (1) {
        uint16_t ch;
        if (xQueueReceive(g_input_queue, &ch, pdMS_TO_TICKS(250)) == pdTRUE) {
            if (ch == '\r' || ch == '\n') break;
            if ((ch == '\b' || ch == 0x7F) && pos > 0) {
                pos--;
                term_cursor_left(sh->term);
                term_putchar(sh->term, ' ');
                term_cursor_left(sh->term);
                shell_set_input_color(sh);
                term_render(sh->term);
                continue;
            }
            if (pos < 30) {
                input[pos++] = ch;
                term_putchar(sh->term, ch);
                shell_set_input_color(sh);
                term_render(sh->term);
            }
        } else {
            sh->term->dirty = 1;
            term_render(sh->term);
        }
    }
    input[pos] = 0;
    char line[32];
    for (int i = 0; i < pos; i++) line[i] = (char)input[i];
    line[pos] = '\0';
    shell_puts(sh, "\n");

    int y, m, d;
    if (pos > 0 && sscanf(line, "%d-%d-%d", &y, &m, &d) == 3) {
        struct tm tm = {0};
        tm.tm_year = y - 1900;
        tm.tm_mon = m - 1;
        tm.tm_mday = d;
        tm.tm_hour = 12;
        time_t t = mktime(&tm);
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ds3231_set_time(&tm);
        shell_puts(sh, "Date set.\n");
    } else if (pos > 0) {
        shell_puts(sh, "Invalid format.\n");
    }
}

static void cmd_time_cmd(shell_t *sh, int argc, char **argv)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[64];

    if (argc > 1) {
        int h, m, s;
        if (sscanf(argv[1], "%d:%d:%d", &h, &m, &s) == 3) {
            now = time(NULL);
            t = localtime(&now);
            t->tm_hour = h;
            t->tm_min = m;
            t->tm_sec = s;
            time_t t2 = mktime(t);
            struct timeval tv = { .tv_sec = t2, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ds3231_set_time(t);
            shell_puts(sh, "Time set.\n");
        } else {
            shell_puts(sh, "Format: TIME HH:MM:SS\n");
        }
        return;
    }

    snprintf(buf, sizeof(buf), "Current time is: %02d:%02d:%02d\n",
             t->tm_hour, t->tm_min, t->tm_sec);
    shell_puts(sh, buf);
    shell_set_input_color(sh);
    shell_puts(sh, "Enter new time (HH:MM:SS): ");
    term_render(sh->term);
    uint16_t input[32];
    int pos = 0;
    while (1) {
        uint16_t ch;
        if (xQueueReceive(g_input_queue, &ch, pdMS_TO_TICKS(250)) == pdTRUE) {
            if (ch == '\r' || ch == '\n') break;
            if ((ch == '\b' || ch == 0x7F) && pos > 0) {
                pos--;
                term_cursor_left(sh->term);
                term_putchar(sh->term, ' ');
                term_cursor_left(sh->term);
                shell_set_input_color(sh);
                term_render(sh->term);
                continue;
            }
            if (pos < 30) {
                input[pos++] = ch;
                term_putchar(sh->term, ch);
                shell_set_input_color(sh);
                term_render(sh->term);
            }
        } else {
            sh->term->dirty = 1;
            term_render(sh->term);
        }
    }
    input[pos] = 0;
    char line[32];
    for (int i = 0; i < pos; i++) line[i] = (char)input[i];
    line[pos] = '\0';
    shell_puts(sh, "\n");

    int h, m, s;
    if (pos > 0 && sscanf(line, "%d:%d:%d", &h, &m, &s) == 3) {
        now = time(NULL);
        t = localtime(&now);
        t->tm_hour = h;
        t->tm_min = m;
        t->tm_sec = s;
        time_t t2 = mktime(t);
        struct timeval tv = { .tv_sec = t2, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ds3231_set_time(t);
        shell_puts(sh, "Time set.\n");
    } else if (pos > 0) {
        shell_puts(sh, "Invalid format.\n");
    }
}

static void cmd_edit(shell_t *sh, int argc, char **argv)
{
    const char *file = NULL;
    if (argc > 1) file = argv[1];
    editor_run(file);
    g_terminal.dirty = 1;
    term_render(&g_terminal);
}

static void cmd_dino(shell_t *sh, int argc, char **argv)
{
    (void)sh; (void)argc; (void)argv;
    dino_run();
}

/* ---- 命令解析与执行 ---- */

static int split_command(const char *cmd, char **argv, int max_args)
{
    int argc = 0;
    const char *p = cmd;

    while (*p && argc < max_args - 1) {
        while (*p == ' ') p++;
        if (*p == '\0') break;

        if (*p == '"') {
            p++;
            int pos = 0;
            char *arg = malloc(MAX_CMD_LEN);
            if (!arg) break;
            while (*p && *p != '"' && pos < MAX_CMD_LEN - 1) {
                arg[pos++] = *p++;
            }
            arg[pos] = '\0';
            if (*p == '"') p++;
            argv[argc++] = arg;
        } else {
            int pos = 0;
            char *arg = malloc(MAX_CMD_LEN);
            if (!arg) break;
            while (*p && *p != ' ' && pos < MAX_CMD_LEN - 1) {
                arg[pos++] = *p++;
            }
            arg[pos] = '\0';
            argv[argc++] = arg;
        }
    }
    argv[argc] = NULL;
    return argc;
}

static void free_argv(char **argv, int argc)
{
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
}

/* 颜色辅助函数 (定义在后�? */
static void shell_set_output_color(shell_t *sh);

void shell_execute(shell_t *sh, const char *cmd_in)
{
    if (!cmd_in || cmd_in[0] == '\0' || cmd_in[0] == '#') return;

    /* DOS路径转Unix: \ → / */
    char cmd_buf[MAX_CMD_LEN];
    strncpy(cmd_buf, cmd_in, sizeof(cmd_buf) - 1);
    cmd_buf[sizeof(cmd_buf) - 1] = '\0';
    for (int i = 0; cmd_buf[i]; i++)
        if (cmd_buf[i] == '\\') cmd_buf[i] = '/';
    const char *cmd = cmd_buf;

    /* 添加到历�?*/
    if (sh->history_count == 0 ||
        strcmp(sh->history[(sh->history_count - 1) % SHELL_HISTORY], cmd) != 0) {
        int idx = sh->history_count % SHELL_HISTORY;
        strncpy(sh->history[idx], cmd, MAX_CMD_LEN - 1);
        sh->history_count++;
    }
    sh->history_pos = sh->history_count;

    /* 先换�?提交带提示符颜色的行), 再切换为输出颜色 */
    shell_puts(sh, "\n");
    shell_set_output_color(sh);

    /* 解析命令 */
    char *argv[MAX_ARGS];
    int argc = split_command(cmd, argv, MAX_ARGS);
    if (argc == 0) return;

    /* 查找并执�?*/
    int found = 0;
    for (int i = 0; cmd_table[i].name; i++) {
        if (strcmp(argv[0], cmd_table[i].name) == 0) {
            cmd_table[i].handler(sh, argc, argv);
            found = 1;
            break;
        }
    }

    if (!found) {
        /* 尝试�?/bin/ 加载 ELF 命令 */
        char elf_path[128];
        snprintf(elf_path, sizeof(elf_path), "/bin/%s", argv[0]);
        vfs_stat_t st;
        if (vfs_stat(elf_path, &st) == 0 && (st.type == VFS_FILE || st.type == VFS_EXEC)) {
            int pid = proc_spawn_elf(elf_path, argc, argv);
            if (pid > 0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "PID=%d\n", pid);
                shell_puts(sh, buf);
                int code;
                proc_wait_any(&code);
            } else {
                shell_puts(sh, "加载失败\n");
            }
        } else {
            shell_puts(sh, "命令未找到 ");
            shell_puts(sh, argv[0]);
            shell_puts(sh, "\n");
        }
    }

    free_argv(argv, argc);
}

/* 设置提示�?输入颜色 */
static void shell_set_input_color(shell_t *sh)
{
    sh->term->fg_color = 8;
    sh->term->fg_custom = COLOR_DOS_GREEN;
    sh->term->line_fg_color[sh->term->current_line] = COLOR_DOS_GREEN;
}

static void shell_set_output_color(shell_t *sh)
{
    sh->term->fg_color = 8;
    sh->term->fg_custom = COLOR_DOS_GREEN;
    sh->term->line_fg_color[sh->term->current_line] = COLOR_DOS_GREEN;
}

/* ---- 行编辑 ---- */

void shell_print_prompt(shell_t *sh)
{
    shell_set_input_color(sh);
    char prompt[128];
    snprintf(prompt, sizeof(prompt), "A:");
    /* 将当前目录转为DOS风格并追加 */
    char dos[64];
    strncpy(dos, sh->cwd, sizeof(dos) - 1);
    for (int i = 0; dos[i]; i++)
        if (dos[i] == '/') dos[i] = '\\';
    if (strcmp(sh->cwd, "/") == 0)
        strcat(prompt, "\\>");
    else
        snprintf(prompt + 2, sizeof(prompt) - 2, "%s\\>", dos);
    shell_puts(sh, prompt);
    sh->prompt_len = strlen(prompt);
    sh->prompt_len *= 6;
}

static void shell_redraw_input(shell_t *sh)
{
    term_clear_line(sh->term);

    shell_set_input_color(sh);
    char prompt[128];
    snprintf(prompt, sizeof(prompt), "A:");
    char dos[64];
    strncpy(dos, sh->cwd, sizeof(dos) - 1);
    for (int i = 0; dos[i]; i++)
        if (dos[i] == '/') dos[i] = '\\';
    if (strcmp(sh->cwd, "/") == 0)
        strcat(prompt, "\\>");
    else
        snprintf(prompt + 2, sizeof(prompt) - 2, "%s\\>", dos);
    shell_puts(sh, prompt);
    sh->prompt_len = strlen(prompt) * 6;

    for (int i = 0; i < sh->input_len; i++) {
        term_putchar(sh->term, sh->input_buf[i]);
    }
}

void shell_init(shell_t *sh, terminal_t *term)
{
    memset(sh, 0, sizeof(shell_t));
    sh->term = term;
    sh->running = 1;
    sh->history_pos = 0;
    strcpy(sh->cwd, "/");
    vfs_getcwd(sh->cwd, sizeof(sh->cwd));
}

void shell_process_char(shell_t *sh, uint16_t ch)
{
    if (ch == '\r' || ch == '\n') {
        /* 空行：输出换行，继续显示提示�?*/
        if (sh->input_len == 0) {
            term_putchar(sh->term, '\n');
            term_render(sh->term);
            shell_print_prompt(sh);
            return;
        }

        sh->input_buf[sh->input_len] = '\0';
        /* 先刷新再执行（避免界面卡住不动） */
        term_render(sh->term);

        /* 转为UTF-8字符�?*/
        char utf8_cmd[MAX_CMD_LEN];
        int pos = 0;
        for (int i = 0; i < sh->input_len && pos < MAX_CMD_LEN - 4; i++) {
            uint16_t c = sh->input_buf[i];
            if (c < 0x80) {
                utf8_cmd[pos++] = (char)c;
            } else if (c < 0x800) {
                utf8_cmd[pos++] = 0xC0 | (c >> 6);
                utf8_cmd[pos++] = 0x80 | (c & 0x3F);
            } else {
                utf8_cmd[pos++] = 0xE0 | (c >> 12);
                utf8_cmd[pos++] = 0x80 | ((c >> 6) & 0x3F);
                utf8_cmd[pos++] = 0x80 | (c & 0x3F);
            }
        }
        utf8_cmd[pos] = '\0';

        shell_execute(sh, utf8_cmd);

        /* 重置输入 */
        sh->input_len = 0;
        sh->input_cursor = 0;

        /* 打印提示符 */
        shell_print_prompt(sh);
        term_render(sh->term);
        return;
    }

    if (ch == '\b' || ch == 0x7F) {
        if (sh->input_len > 0) {
            sh->input_len--;
            sh->input_cursor = sh->input_len;
            shell_redraw_input(sh);
            term_render(sh->term);
        }
        return;
    }

    /* 处理特殊�?(通过转义序列) */
    if (ch == 0x1B) {
        /* ESC序列 - 暂不处理箭头键等 */
        return;
    }

    /* 箭头键 */
    if (ch == KEY_UP || ch == KEY_DOWN) {
        if (sh->history_count == 0) return;
        if (ch == KEY_UP) {
            sh->history_pos--;
            if (sh->history_pos < 0) sh->history_pos = sh->history_count - 1;
        } else {
            sh->history_pos++;
            if (sh->history_pos >= sh->history_count) sh->history_pos = 0;
        }
        int idx = sh->history_pos % SHELL_HISTORY;
        sh->input_len = 0;
        for (char *p = sh->history[idx]; *p && sh->input_len < MAX_CMD_LEN - 1; p++)
            sh->input_buf[sh->input_len++] = (uint8_t)*p;
        sh->input_cursor = sh->input_len;
        shell_redraw_input(sh);
        term_render(sh->term);
        return;
    }

    if (ch == KEY_LEFT || ch == KEY_RIGHT) {
        if (ch == KEY_LEFT && sh->input_cursor > 0)
            sh->input_cursor--;
        else if (ch == KEY_RIGHT && sh->input_cursor < sh->input_len)
            sh->input_cursor++;
        return;
    }

    /* 输入字符显示为蓝色并回显 */
    if (sh->input_len < MAX_CMD_LEN - 1) {
        shell_set_input_color(sh);
        term_putchar(sh->term, ch);
        sh->input_buf[sh->input_len++] = ch;
        sh->input_cursor = sh->input_len;
    }

    term_render(sh->term);
}
