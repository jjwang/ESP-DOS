#ifndef __COMMAND_SDK_H__
#define __COMMAND_SDK_H__

#include <stdint.h>

/* 系统调用表 — 传递给 ELF 命令的完整 API */
typedef struct {
    /* 终端输出 */
    void (*print)(const char *s);
    void (*println)(const char *s);
    void (*print_char)(char c);
    void (*printf)(const char *fmt, ...);
    
    /* 输入 */
    int  (*getchar)(void);
    void (*gets)(char *buf, int max);

    /* 文件系统 */
    void *(*fopen)(const char *path, const char *mode);
    int   (*fread)(void *buf, int size, void *fp);
    int   (*fwrite)(const void *buf, int size, void *fp);
    void  (*fclose)(void *fp);
    int   (*fexist)(const char *path);

    /* 目录 */
    int   (*ls)(const char *path, void *callback, void *arg);

    /* 内存 */
    void *(*malloc)(int size);
    void  (*free)(void *p);

    /* 系统 */
    void  (*exit)(int code);
    void  (*reboot)(void);
    void  (*sleep)(int ms);

    /* 路径 */
    void  (*getcwd)(char *buf, int max);
    int   (*chdir)(const char *path);

    /* 系统信息 (v2) */
    int   (*get_free_heap)(void);
    int   (*get_total_heap)(void);
    int   (*get_free_psram)(void);
    int   (*get_total_psram)(void);
    void  (*get_chip_desc)(char *buf, int max);
    void  (*get_fs_info)(int *total, int *used);
} syscall_t;

/* 命令入口 */
typedef void (*command_entry_t)(int argc, char **argv, syscall_t *sys);

#endif
