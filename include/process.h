#ifndef __PROCESS_H__
#define __PROCESS_H__

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAX_PROC 16
#define PROC_NAME_LEN 32

typedef enum {
    PROC_FREE = 0,
    PROC_RUNNING,
    PROC_ZOMBIE
} proc_state_t;

typedef struct {
    uint16_t pid;
    proc_state_t state;
    char name[PROC_NAME_LEN];
    TaskHandle_t task;
    int exit_code;
    void *elf_base;   /* PSRAM中加载的ELF基址, 退出时需释放 */
} pcb_t;

/* 进程入口类型 (传给ELF的main) */
typedef void (*proc_entry_t)(void *arg);

/* 系统调用表 (兼容 command_sdk.h) */
typedef struct syscall_table_s {
    void (*print)(const char *s);
    void (*println)(const char *s);
    void (*print_char)(char c);
    void (*printf)(const char *fmt, ...);
    int  (*getchar)(void);
    void (*gets)(char *buf, int max);
    void *(*fopen)(const char *path, const char *mode);
    int   (*fread)(void *buf, int size, void *fp);
    int   (*fwrite)(const void *buf, int size, void *fp);
    void  (*fclose)(void *fp);
    int   (*fexist)(const char *path);
    int   (*ls)(const char *path, void *callback, void *arg);
    void *(*malloc)(int size);
    void  (*free)(void *p);
    void  (*exit)(int code);
    void  (*reboot)(void);
    void  (*sleep)(int ms);
    void  (*getcwd)(char *buf, int max);
    int   (*chdir)(const char *path);

    /* 系统信息 (v2) */
    int   (*get_free_heap)(void);
    int   (*get_total_heap)(void);
    int   (*get_free_psram)(void);
    int   (*get_total_psram)(void);
    void  (*get_chip_desc)(char *buf, int max);
    void  (*get_fs_info)(int *total, int *used);
} syscall_table_t;

int  proc_init(void);
int  proc_spawn(const char *name, proc_entry_t entry, void *arg, int prio, int stack_size);
int  proc_spawn_elf(const char *path, int argc, char **argv);
int  proc_kill(uint16_t pid);
uint16_t proc_wait_any(int *exit_code);
pcb_t *proc_get(uint16_t pid);
int  proc_list(pcb_t *buf, int max);

#endif
