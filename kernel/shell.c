#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "shell.h"
#include "display_st7789.h"
#include "process.h"
#include "vfs.h"

/* ---- 命令表 ---- */
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
static void cmd_pwd(shell_t *sh, int argc, char **argv);
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

static const cmd_entry_t cmd_table[] = {
    {"help",   "显示帮助信息",        "help [命令]", cmd_help},
    {"ls",     "列出目录内容",        "ls [-l] [路径]", cmd_ls},
    {"cat",    "显示文件内容",        "cat <文件>", cmd_cat},
    {"cd",     "切换当前目录",        "cd <路径>", cmd_cd},
    {"pwd",    "显示当前目录",        "pwd", cmd_pwd},
    {"mkdir",  "创建目录",           "mkdir <目录名>", cmd_mkdir},
    {"rm",     "删除文件或目录",      "rm [-r] <路径>", cmd_rm},
    {"clear",  "清屏",              "clear", cmd_clear},
    {"ps",     "显示进程信息",        "ps", cmd_ps},
    {"reboot", "重启系统",           "reboot", cmd_reboot},
    {"touch",  "创建空文件",         "touch <文件>", cmd_touch},
    {"write",  "写入文件",           "write <文件> <内容>", cmd_write},
    {"exec",   "运行ELF程序",        "exec <ELF文件>", cmd_exec},
    {"mv",     "移动/重命名文件",     "mv <源> <目标>", cmd_mv},
    {"cp",     "复制文件",           "cp <源> <目标>", cmd_cp},
    {"kill",   "终止进程",           "kill <PID>", cmd_kill},
    {NULL, NULL, NULL, NULL}
};

/* ---- ELF/进程命令 ---- */
static void cmd_exec(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) { term_puts(sh->term, "用法: exec <ELF文件>\n"); return; }
    term_puts(sh->term, "正在加载: ");
    term_puts(sh->term, argv[1]);
    term_puts(sh->term, "\n");
    int pid = proc_spawn_elf(argv[1], argc - 1, argv + 1);
    if (pid < 0) {
        term_puts(sh->term, "加载失败\n");
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "PID=%d running, wait...\n", pid);
    term_puts(sh->term, buf);
    int code;
    uint16_t done = proc_wait_any(&code);
    snprintf(buf, sizeof(buf), "PID=%d exit (code=%d)\n", done, code);
    term_puts(sh->term, buf);
}

static void cmd_kill(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) { term_puts(sh->term, "用法: kill <PID>\n"); return; }
    int pid = atoi(argv[1]);
    if (proc_kill(pid) == 0)
        term_puts(sh->term, "已终止\n");
    else
        term_puts(sh->term, "进程不存在\n");
}

/* ---- 命令实现 ---- */

static void cmd_help(shell_t *sh, int argc, char **argv)
{
    term_puts(sh->term, "\nOpenCrab - 可用命令:\n");
    term_puts(sh->term, "================================\n");

    if (argc > 1) {
        for (int i = 0; cmd_table[i].name; i++) {
            if (strcmp(argv[1], cmd_table[i].name) == 0) {
                term_puts(sh->term, cmd_table[i].name);
                term_puts(sh->term, " - ");
                term_puts(sh->term, cmd_table[i].desc);
                term_puts(sh->term, "\n  用法: ");
                term_puts(sh->term, cmd_table[i].usage);
                term_puts(sh->term, "\n");
                return;
            }
        }
        term_puts(sh->term, "未知命令: ");
        term_puts(sh->term, argv[1]);
        term_puts(sh->term, "\n");
        return;
    }

    for (int i = 0; cmd_table[i].name; i++) {
        term_puts(sh->term, "  ");
        term_puts(sh->term, cmd_table[i].name);
        int pad = 10 - strlen(cmd_table[i].name);
        if (pad < 1) pad = 1;
        char buf[16];
        memset(buf, ' ', pad);
        buf[pad] = '\0';
        term_puts(sh->term, buf);
        term_puts(sh->term, cmd_table[i].desc);
        term_puts(sh->term, "\n");
    }
}

static void cmd_ls(shell_t *sh, int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : ".";
    int long_format = 0;

    if (argc > 1 && strcmp(argv[1], "-l") == 0) {
        long_format = 1;
        path = (argc > 2) ? argv[2] : ".";
    }

    vfs_dir_t *dir = vfs_opendir(path);
    if (!dir) {
        term_puts(sh->term, "无法打开目录: ");
        term_puts(sh->term, path);
        term_puts(sh->term, "\n");
        return;
    }

    vfs_dirent_t *entry;
    while ((entry = vfs_readdir(dir)) != NULL) {
        if (!long_format) {
            term_puts(sh->term, entry->name);
            term_puts(sh->term, "  ");
        } else {
            term_puts(sh->term, entry->type == VFS_DIR ? "d" : "-");
            term_puts(sh->term, "rw-r--r-- ");
            char size_str[16];
            snprintf(size_str, sizeof(size_str), "%8lu ", (unsigned long)entry->size);
            term_puts(sh->term, size_str);
            term_puts(sh->term, entry->name);
            term_puts(sh->term, "\n");
        }
    }

    if (!long_format) {
        term_puts(sh->term, "\n");
    }

    vfs_closedir(dir);
}

static void cmd_cat(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) {
        term_puts(sh->term, "用法: cat <文件>\n");
        return;
    }

    vfs_file_t *file = vfs_open(argv[1], VFS_O_RDONLY);
    if (!file) {
        term_puts(sh->term, "无法打开文件: ");
        term_puts(sh->term, argv[1]);
        term_puts(sh->term, "\n");
        return;
    }

    char buf[128];
    int bytes;
    while ((bytes = vfs_read(file, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = '\0';
        term_puts(sh->term, buf);
    }

    if (bytes < 0) {
        term_puts(sh->term, "\n读取错误\n");
    }

    vfs_close(file);
}

static void cmd_cd(shell_t *sh, int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/";
    if (vfs_chdir(path) != 0) {
        term_puts(sh->term, "目录不存在: ");
        term_puts(sh->term, path);
        term_puts(sh->term, "\n");
        return;
    }
    vfs_getcwd(sh->cwd, sizeof(sh->cwd));
}

static void cmd_pwd(shell_t *sh, int argc, char **argv)
{
    (void)argc; (void)argv;
    term_puts(sh->term, sh->cwd);
    term_puts(sh->term, "\n");
}

static void cmd_mkdir(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) {
        term_puts(sh->term, "用法: mkdir <目录名>\n");
        return;
    }
    if (vfs_mkdir(argv[1]) != 0) {
        term_puts(sh->term, "创建目录失败: ");
        term_puts(sh->term, argv[1]);
        term_puts(sh->term, "\n");
    }
}

static void cmd_rm(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) {
        term_puts(sh->term, "用法: rm [-r] <路径>\n");
        return;
    }

    int recursive = 0;
    const char *path = argv[1];
    if (strcmp(argv[1], "-r") == 0) {
        recursive = 1;
        path = (argc > 2) ? argv[2] : NULL;
        if (!path) {
            term_puts(sh->term, "用法: rm [-r] <路径>\n");
            return;
        }
    }

    if (!recursive) {
        vfs_stat_t st;
        if (vfs_stat(path, &st) == 0 && st.type == VFS_DIR) {
            term_puts(sh->term, "是目录, 使用 rm -r 删除\n");
            return;
        }
    }

    if (vfs_remove(path) != 0) {
        term_puts(sh->term, "删除失败: ");
        term_puts(sh->term, path);
        term_puts(sh->term, "\n");
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
    term_puts(sh->term, "PID  NAME          STATE\n");
    term_puts(sh->term, "---  ------------  -----\n");

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
            term_puts(sh->term, line);
        }
        free(tasks);
    }
}

static void cmd_reboot(shell_t *sh, int argc, char **argv)
{
    (void)argc; (void)argv;
    term_puts(sh->term, "系统将在3秒后重启...\n");
    term_render(sh->term);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

static void cmd_touch(shell_t *sh, int argc, char **argv)
{
    if (argc < 2) {
        term_puts(sh->term, "用法: touch <文件>\n");
        return;
    }
    vfs_file_t *f = vfs_open(argv[1], VFS_O_WRONLY | VFS_O_CREAT);
    if (f) {
        vfs_close(f);
    } else {
        term_puts(sh->term, "创建文件失败\n");
    }
}

static void cmd_write(shell_t *sh, int argc, char **argv)
{
    if (argc < 3) {
        term_puts(sh->term, "用法: write <文件> <内容>\n");
        return;
    }
    vfs_file_t *f = vfs_open(argv[1], VFS_O_WRONLY | VFS_O_CREAT);
    if (!f) {
        term_puts(sh->term, "打开文件失败\n");
        return;
    }
    vfs_write(f, argv[2], strlen(argv[2]));
    vfs_close(f);
}


static void cmd_mv(shell_t *sh, int argc, char **argv)
{
    if (argc < 3) {
        term_puts(sh->term, "用法: mv <源> <目标>\n");
        return;
    }
    if (vfs_rename(argv[1], argv[2]) != 0) {
        term_puts(sh->term, "移动/重命名失败\n");
    }
}

static void cmd_cp(shell_t *sh, int argc, char **argv)
{
    if (argc < 3) {
        term_puts(sh->term, "用法: cp <源> <目标>\n");
        return;
    }

    vfs_file_t *src = vfs_open(argv[1], VFS_O_RDONLY);
    if (!src) {
        term_puts(sh->term, "无法打开源文件\n");
        return;
    }

    vfs_file_t *dst = vfs_open(argv[2], VFS_O_WRONLY | VFS_O_CREAT);
    if (!dst) {
        term_puts(sh->term, "无法创建目标文件\n");
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
    term_puts(sh->term, "复制完成\n");
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

/* 颜色辅助函数 (定义在后面) */
static void shell_set_input_color(shell_t *sh);
static void shell_set_output_color(shell_t *sh);

void shell_execute(shell_t *sh, const char *cmd)
{
    if (!cmd || cmd[0] == '\0' || cmd[0] == '#') return;

    /* 添加到历史 */
    if (sh->history_count == 0 ||
        strcmp(sh->history[(sh->history_count - 1) % SHELL_HISTORY], cmd) != 0) {
        int idx = sh->history_count % SHELL_HISTORY;
        strncpy(sh->history[idx], cmd, MAX_CMD_LEN - 1);
        sh->history_count++;
    }
    sh->history_pos = sh->history_count;

    /* 先换行(提交带提示符颜色的行), 再切换为输出颜色 */
    term_puts(sh->term, "\n");
    shell_set_output_color(sh);

    /* 解析命令 */
    char *argv[MAX_ARGS];
    int argc = split_command(cmd, argv, MAX_ARGS);
    if (argc == 0) return;

    /* 查找并执行 */
    int found = 0;
    for (int i = 0; cmd_table[i].name; i++) {
        if (strcmp(argv[0], cmd_table[i].name) == 0) {
            cmd_table[i].handler(sh, argc, argv);
            found = 1;
            break;
        }
    }

    if (!found) {
        /* 尝试从 /bin/ 加载 ELF 命令 */
        char elf_path[128];
        snprintf(elf_path, sizeof(elf_path), "/bin/%s", argv[0]);
        vfs_stat_t st;
        if (vfs_stat(elf_path, &st) == 0 && st.type == VFS_FILE) {
            int pid = proc_spawn_elf(elf_path, argc, argv);
            if (pid > 0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "PID=%d\n", pid);
                term_puts(sh->term, buf);
                int code;
                proc_wait_any(&code);
            } else {
                term_puts(sh->term, "加载失败\n");
            }
        } else {
            term_puts(sh->term, "命令未找到: ");
            term_puts(sh->term, argv[0]);
            term_puts(sh->term, "\n");
        }
    }

    free_argv(argv, argc);
}

/* 设置提示符/输入颜色 (浅蓝) */
static void shell_set_input_color(shell_t *sh)
{
    sh->term->fg_color = 8;
    sh->term->fg_custom = COLOR_LIGHT_BLUE;
    sh->term->line_fg_color[sh->term->current_line] = COLOR_LIGHT_BLUE;
}

/* 设置输出颜色 (浅灰) */
static void shell_set_output_color(shell_t *sh)
{
    sh->term->fg_color = 8;
    sh->term->fg_custom = 0xC618;
    sh->term->line_fg_color[sh->term->current_line] = 0xC618;
}

/* ---- 行编辑 ---- */

void shell_print_prompt(shell_t *sh)
{
    shell_set_input_color(sh);
    term_puts(sh->term, SHELL_PROMPT);
    sh->prompt_len = strlen(SHELL_PROMPT);
    sh->prompt_len *= 6;
}

static void shell_redraw_input(shell_t *sh)
{
    term_clear_line(sh->term);

    shell_set_input_color(sh);
    term_puts(sh->term, SHELL_PROMPT);
    sh->prompt_len = strlen(SHELL_PROMPT) * 6;

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
        /* 执行命令 */
        sh->input_buf[sh->input_len] = '\0';

        /* 转为UTF-8字符串 */
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

    /* 处理特殊键 (通过转义序列) */
    if (ch == 0x1B) {
        /* ESC序列 - 暂不处理箭头键等 */
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
