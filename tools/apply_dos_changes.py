# -*- coding: utf-8 -*-
import re

with open('kernel/shell.c', 'r', encoding='utf-8') as f:
    s = f.read()

# 1. Remove cmd_pwd declaration
s = s.replace('static void cmd_pwd(shell_t *sh, int argc, char **argv);\n', '')

# 2. Remove cmd_pwd function
s = s.replace('''
static void cmd_pwd(shell_t *sh, int argc, char **argv)
{
    (void)argc; (void)argv;
    shell_puts(sh, sh->cwd);
    shell_puts(sh, "\n");
}
''', '')

# 3. Add path conversion to shell_execute
s = s.replace('void shell_execute(shell_t *sh, const char *cmd)\n{\n    if (!cmd', 
'''void shell_execute(shell_t *sh, const char *cmd_in)
{
    if (!cmd_in || cmd_in[0] == '\\0' || cmd_in[0] == '#') return;
    char cmd_buf[256];
    strncpy(cmd_buf, cmd_in, sizeof(cmd_buf) - 1);
    cmd_buf[sizeof(cmd_buf) - 1] = '\\0';
    for (int i = 0; cmd_buf[i]; i++)
        if (cmd_buf[i] == '\\\\') cmd_buf[i] = '/';
    const char *cmd = cmd_buf;
    if (!cmd''')

# 4. Make cmd_ls use DOS path and current dir
old_ls = '''static void cmd_ls(shell_t *sh, int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : ".";
    int long_format = 0;

    if (argc > 1 && strcmp(argv[1], "-l") == 0) {
        long_format = 1;
        path = (argc > 2) ? argv[2] : ".";
    }

    vfs_dir_t *dir = vfs_opendir(path);
    if (!dir) {
        shell_puts(sh, "无法打开目录: ");
        shell_puts(sh, path);
        shell_puts(sh, "\\n");
        return;
    }

    vfs_dirent_t *entry;
    while ((entry = vfs_readdir(dir)) != NULL) {
        if (!long_format) {
            shell_puts(sh, entry->name);
            shell_puts(sh, "  ");
        } else {
            shell_puts(sh, entry->type == VFS_DIR ? "d" : "-");
            shell_puts(sh, "rw-r--r-- ");
            char size_str[16];
            snprintf(size_str, sizeof(size_str), "%8lu ", (unsigned long)entry->size);
            shell_puts(sh, size_str);
            shell_puts(sh, entry->name);
            shell_puts(sh, "\\n");
        }
    }

    if (!long_format) {
        shell_puts(sh, "\\n");
    }

    vfs_closedir(dir);
}'''

new_ls = '''static void cmd_ls(shell_t *sh, int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : ".";
    char buf[256];

    /* 将路径转换为DOS风格 */
    char dos_path[128];
    strncpy(dos_path, path, sizeof(dos_path) - 1);
    for (int i = 0; dos_path[i]; i++)
        if (dos_path[i] == '/') dos_path[i] = '\\\\';
    snprintf(buf, sizeof(buf), " Directory of %s\\n", dos_path);
    shell_puts(sh, buf);
    shell_puts(sh, "\\n");

    vfs_dir_t *dir = vfs_opendir(path);
    if (!dir) {
        snprintf(buf, sizeof(buf), "File not found - %s\\n", path);
        shell_puts(sh, buf);
        return;
    }

    int total_files = 0;
    vfs_dirent_t *entry;
    while ((entry = vfs_readdir(dir)) != NULL) {
        if (entry->type == VFS_DIR) {
            snprintf(buf, sizeof(buf), "        <DIR>  %s\\n", entry->name);
        } else {
            snprintf(buf, sizeof(buf), "       %8lu  %s\\n", (unsigned long)entry->size, entry->name);
        }
        shell_puts(sh, buf);
        total_files++;
    }

    snprintf(buf, sizeof(buf), "%8d File(s)\\n\\n", total_files);
    shell_puts(sh, buf);

    vfs_closedir(dir);
}'''

s = s.replace(old_ls, new_ls)

# 5. Fix cmd_cd display with DOS path
s = s.replace('''    if (argc < 2) {
        shell_puts(sh, sh->cwd);
        shell_puts(sh, "\\n");
        return;
    }''', 
'''    if (argc < 2) {
        char dos[128];
        strncpy(dos, sh->cwd, sizeof(dos) - 1);
        for (int i = 0; dos[i]; i++)
            if (dos[i] == '/') dos[i] = '\\\\';
        shell_puts(sh, dos);
        shell_puts(sh, "\\n");
        return;
    }''')

# 6. Add DATE and TIME command functions before help
old_help = '''static void cmd_help(shell_t *sh, int argc, char **argv)'''
date_time_cmds = '''static void cmd_date(shell_t *sh, int argc, char **argv)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    const char *dow[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    char buf[64];

    if (argc > 1) {
        int y, m, d;
        if (sscanf(argv[1], "%d-%d-%d", &y, &m, &d) == 3) {
            struct tm tm = {0};
            tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d;
            tm.tm_hour = 12;
            time_t t2 = mktime(&tm);
            struct timeval tv = { .tv_sec = t2, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ds3231_set_time(&tm);
            shell_puts(sh, "Date set.\\n");
        } else {
            shell_puts(sh, "Format: DATE YYYY-MM-DD\\n");
        }
        return;
    }
    snprintf(buf, sizeof(buf), "Current date is: %s %02d-%02d-%04d\\n",
             dow[t->tm_wday], t->tm_mon + 1, t->tm_mday, t->tm_year + 1900);
    shell_puts(sh, buf);
}

static void cmd_time_cmd(shell_t *sh, int argc, char **argv)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[64];
    if (argc > 1) {
        int h, m, s;
        if (sscanf(argv[1], "%d:%d:%d", &h, &m, &s) == 3) {
            now = time(NULL); t = localtime(&now);
            t->tm_hour = h; t->tm_min = m; t->tm_sec = s;
            time_t t2 = mktime(t);
            struct timeval tv = { .tv_sec = t2, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ds3231_set_time(t);
            shell_puts(sh, "Time set.\\n");
        } else {
            shell_puts(sh, "Format: TIME HH:MM:SS\\n");
        }
        return;
    }
    snprintf(buf, sizeof(buf), "Current time is: %02d:%02d:%02d\\n",
             t->tm_hour, t->tm_min, t->tm_sec);
    shell_puts(sh, buf);
}

''' + old_help
s = s.replace(old_help, date_time_cmds)

# 7. Add case-insensitive command matching
old_exec = '''
    /* 查找并执行 */
    int found = 0;
    for (int i = 0; cmd_table[i].name; i++) {
        if (strcmp(argv[0], cmd_table[i].name) == 0) {'''
new_exec = '''
    /* 查找并执行 (大小写不敏感) */
    int found = 0;
    char cmd_lower[32];
    strncpy(cmd_lower, argv[0], sizeof(cmd_lower) - 1);
    cmd_lower[sizeof(cmd_lower) - 1] = '\\0';
    for (int i = 0; cmd_lower[i]; i++) cmd_lower[i] = tolower((unsigned char)cmd_lower[i]);
    for (int i = 0; cmd_table[i].name; i++) {
        if (strcmp(cmd_lower, cmd_table[i].name) == 0) {'''
s = s.replace(old_exec, new_exec)

# 8. Set colors to DOS green
s = s.replace('''static void shell_set_input_color(shell_t *sh)
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
}''', 
'''static void shell_set_input_color(shell_t *sh)
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
}''')

with open('kernel/shell.c', 'w', encoding='utf-8') as f:
    f.write(s)

print('All changes applied')
