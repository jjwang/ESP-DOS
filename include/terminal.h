#ifndef __TERMINAL_H__
#define __TERMINAL_H__

#include <stdint.h>
#include "config.h"

/* 终端颜色 */
#define TERM_COLOR_BLACK   0
#define TERM_COLOR_RED     1
#define TERM_COLOR_GREEN   2
#define TERM_COLOR_YELLOW  3
#define TERM_COLOR_BLUE    4
#define TERM_COLOR_MAGENTA 5
#define TERM_COLOR_CYAN    6
#define TERM_COLOR_WHITE   7
#define TERM_COLOR_DEFAULT 9

/* 一行最大字符数 (按ASCII半角计) */
#define TERM_LINE_MAX 128

/* 终端状态结构 */
typedef struct {
    /* 回滚缓冲区 (环形数组) */
    uint16_t scrollback[TERM_SCROLLBACK][TERM_LINE_MAX];
    uint8_t  scrollback_len[TERM_SCROLLBACK];   /* 每行字符数 */
	uint16_t line_fg_color[TERM_SCROLLBACK];    /* 每行前景色 */
    int      scrollback_head;                    /* 写入位置 */
    int      scrollback_count;                   /* 总行数 */

    /* 可见区域 */
    int visible_start;  /* 回滚缓冲区中第一行可见行的偏移 */

    /* 光标位置 (像素坐标) */
    int cursor_x;
    int cursor_y;

    /* 当前行 */
    int current_line;           /* 在回滚缓冲区中的索引 */
    int current_line_length;    /* 当前行已写入的字符数 */

    /* 颜色 (0-7标准色, 8=自定义) */
    uint8_t fg_color;
    uint8_t bg_color;
    uint16_t fg_custom;   /* 当fg_color=8时使用 */
    uint16_t bg_custom;   /* 当bg_color=8时使用 */

    /* 脏标记 - 需要重绘的区域 */
    int dirty;
    int dirty_x, dirty_y, dirty_w, dirty_h;

    /* 输入缓冲区 (用于行编辑) */
    uint16_t input_buf[TERM_LINE_MAX];
    int input_len;
    int input_cursor;

} terminal_t;

void term_init(terminal_t *term);
void term_set_color(terminal_t *term, uint8_t fg, uint8_t bg);
void term_putchar(terminal_t *term, uint16_t ch);
void term_puts(terminal_t *term, const char *str);
void term_put_utf8(terminal_t *term, const char *utf8_str);
void term_newline(terminal_t *term);
void term_backspace(terminal_t *term);
void term_clear(terminal_t *term);
void term_clear_line(terminal_t *term);
void term_cursor_left(terminal_t *term);
void term_cursor_right(terminal_t *term);
void term_cursor_home(terminal_t *term);
void term_cursor_set(terminal_t *term, int x, int y);
void term_render(terminal_t *term);

/* 将终端颜色转换为RGB565 */
uint16_t term_color_to_rgb565(uint8_t color);
uint16_t term_get_fg_rgb(terminal_t *term);
uint16_t term_get_bg_rgb(terminal_t *term);

/* 全局终端实例 */
extern terminal_t g_terminal;

#endif
