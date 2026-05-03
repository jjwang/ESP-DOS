#ifndef __SHELL_H__
#define __SHELL_H__

#include "terminal.h"
#include "vfs.h"
#include "config.h"

/* Shell 状态 */
typedef struct {
    /* 当前工作目录 */
    char cwd[128];

    /* 命令历史 */
    char history[SHELL_HISTORY][MAX_CMD_LEN];
    int  history_count;
    int  history_pos;

    /* 输入缓冲区 */
    uint16_t input_buf[MAX_CMD_LEN];
    int  input_len;
    int  input_cursor;

    /* 运行状态 */
    int running;

    /* 引用终端 */
    terminal_t *term;

    /* 当前行在终端中的起始位置 */
    int prompt_len; /* 提示符的字符宽度 */

} shell_t;

void shell_init(shell_t *sh, terminal_t *term);
void shell_process_char(shell_t *sh, uint16_t ch);
void shell_print_prompt(shell_t *sh);
void shell_execute(shell_t *sh, const char *cmd);
void shell_show_help(shell_t *sh);

#endif
