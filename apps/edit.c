#include "app_sdk.h"

#define MAX_LINES  512
#define LINE_LEN   256

static char *lines[MAX_LINES];
static int line_count;
static int modified;

static int s_len(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

static void s_cpy(char *d, const char *s)
{
    while ((*d++ = *s++));
}

static void s_set(char *d, int c, int n)
{
    while (n-- > 0) *d++ = c;
}

static void editor_help(syscall_t *sys)
{
    sys->println(".l      列出所有行");
    sys->println(".d N    删除第 N 行");
    sys->println(".i N    在第 N 行前插入");
    sys->println(".a      在末尾追加");
    sys->println(".w      保存文件");
    sys->println(".q      退出");
    sys->println("直接输入文字并回车即可追加到文件末尾");
}

static int load_file(syscall_t *sys, const char *path)
{
    void *fp = sys->fopen(path, "r");
    if (!fp) return 0;

    char buf[LINE_LEN];
    while (line_count < MAX_LINES) {
        int len = 0;
        char c;
        int has_data = 0;
        while (sys->fread(&c, 1, fp) == 1) {
            has_data = 1;
            if (c == '\n') break;
            if (c != '\r' && len < LINE_LEN - 1) buf[len++] = c;
        }
        if (!has_data && len == 0) break;
        buf[len] = '\0';
        char *p = sys->malloc(len + 1);
        if (!p) break;
        s_cpy(p, buf);
        lines[line_count++] = p;
    }
    sys->fclose(fp);
    return 1;
}

static int save_file(syscall_t *sys, const char *path)
{
    void *fp = sys->fopen(path, "w");
    if (!fp) { sys->println("写入失败"); return 0; }
    for (int i = 0; i < line_count; i++) {
        sys->fwrite(lines[i], s_len(lines[i]), fp);
        sys->fwrite("\n", 1, fp);
    }
    sys->fclose(fp);
    modified = 0;
    sys->println("已保存");
    return 1;
}

void _start(int argc, char **argv, syscall_t *sys)
{
    if (argc < 2) {
        sys->println("用法: edit <文件名>");
        sys->exit(1);
    }

    s_set((char *)lines, 0, sizeof(lines));
    line_count = 0;
    modified = 0;

    sys->printf("--- 编辑: %s ---\n", argv[1]);
    if (load_file(sys, argv[1]))
        sys->printf("已加载 %d 行 (.h 查看帮助)\n", line_count);
    else
        sys->println("新文件");

    char buf[LINE_LEN];
    while (1) {
        sys->print("> ");
        sys->gets(buf, LINE_LEN);

        if (buf[0] == '\0') {
            sys->println("");
            continue;
        }

        if (buf[0] == '.') {
            if (buf[1] == 'q' && buf[2] == '\0') {
                if (modified) {
                    sys->print("未保存，退出? (y/n): ");
                    char ans[4];
                    sys->gets(ans, 4);
                    if (ans[0] != 'y' && ans[0] != 'Y') continue;
                }
                break;
            }
            if (buf[1] == 'w' && buf[2] == '\0') {
                save_file(sys, argv[1]);
                continue;
            }
            if (buf[1] == 'l' && buf[2] == '\0') {
                for (int i = 0; i < line_count; i++)
                    sys->printf("%3d: %s\n", i + 1, lines[i]);
                continue;
            }
            if (buf[1] == 'h' && buf[2] == '\0') {
                editor_help(sys);
                continue;
            }
            if (buf[1] == 'a' && buf[2] == '\0') {
                sys->println("(输入空行结束):");
                while (1) {
                    sys->print("  ");
                    sys->gets(buf, LINE_LEN);
                    if (buf[0] == '\0') break;
                    if (line_count >= MAX_LINES) {
                        sys->println("行数上限"); break;
                    }
                    int len = s_len(buf);
                    char *p = sys->malloc(len + 1);
                    if (!p) { sys->println("内存不足"); break; }
                    s_cpy(p, buf);
                    lines[line_count++] = p;
                    modified = 1;
                }
                continue;
            }
            if ((buf[1] == 'd' || buf[1] == 'i') && buf[2] == ' ') {
                int num = 0, idx = 0;
                char *s = buf + 3;
                while (*s >= '0' && *s <= '9') {
                    num = num * 10 + (*s - '0');
                    s++;
                }
                if (num < 1 || num > line_count + 1) {
                    sys->println("行号超出范围"); continue;
                }
                idx = num - 1;
                if (buf[1] == 'd') {
                    sys->free(lines[idx]);
                    for (int j = idx; j < line_count - 1; j++)
                        lines[j] = lines[j + 1];
                    line_count--;
                    modified = 1;
                    sys->printf("已删除第 %d 行\n", num);
                } else {
                    if (line_count >= MAX_LINES) {
                        sys->println("行数上限"); continue;
                    }
                    sys->print(": ");
                    sys->gets(buf, LINE_LEN);
                    int len = s_len(buf);
                    char *p = sys->malloc(len + 1);
                    if (!p) { sys->println("内存不足"); continue; }
                    s_cpy(p, buf);
                    for (int j = line_count; j > idx; j--)
                        lines[j] = lines[j - 1];
                    lines[idx] = p;
                    line_count++;
                    modified = 1;
                }
                continue;
            }
            sys->println("未知命令 (.h 查看帮助)");
        } else {
            if (line_count >= MAX_LINES) {
                sys->println("行数上限"); continue;
            }
            int len = s_len(buf);
            char *p = sys->malloc(len + 1);
            if (!p) { sys->println("内存不足"); continue; }
            s_cpy(p, buf);
            lines[line_count++] = p;
            modified = 1;
        }
    }

    for (int i = 0; i < line_count; i++)
        sys->free(lines[i]);
    sys->exit(0);
}
