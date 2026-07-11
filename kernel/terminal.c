#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "terminal.h"
#include "display_st7789.h"
#include "config.h"

terminal_t g_terminal;

/* RGB565颜色查找表 */
static const uint16_t term_colors[] = {
    0x0000, /* 黑 */  0xF800, /* 红 */  0x07E0, /* 绿 */
    0xFFE0, /* 黄 */  0x001F, /* 蓝 */  0xF81F, /* 紫 */
    0x07FF, /* 青 */  0xFFFF, /* 白 */
};

uint16_t term_color_to_rgb565(uint8_t color)
{
    if (color > 7) return term_colors[7];
    return term_colors[color];
}

uint16_t term_get_fg_rgb(terminal_t *term)
{
    if (term->fg_color == 8) return term->fg_custom;
    return term_color_to_rgb565(term->fg_color);
}

uint16_t term_get_bg_rgb(terminal_t *term)
{
    if (term->bg_color == 8) return term->bg_custom;
    return term_color_to_rgb565(term->bg_color);
}

void term_init(terminal_t *term)
{
    memset(term, 0, sizeof(terminal_t));
    term->fg_color = 8;
    term->fg_custom = 0xC618;
    term->bg_color = TERM_COLOR_BLACK;
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->current_line = 0;
    term->current_line_length = 0;
    term->scrollback_head = 0;
    term->scrollback_count = 0;
    term->visible_start = 0;
    for (int i = 0; i < TERM_SCROLLBACK; i++)
        term->line_fg_color[i] = 0xC618;
    term->dirty = 1;
}

/* 在回滚缓冲区中写入一行 */
static void term_commit_line(terminal_t *term)
{
    int head = term->scrollback_head;
    term->scrollback_len[head] = term->current_line_length;
    term->line_fg_color[head] = term_get_fg_rgb(term);

    for (int i = term->current_line_length; i < TERM_LINE_MAX; i++) {
        term->scrollback[head][i] = 0;
    }

    term->scrollback_head = (head + 1) % TERM_SCROLLBACK;
    if (term->scrollback_count < TERM_SCROLLBACK) {
        term->scrollback_count++;
    }

    term->current_line = (head + 1) % TERM_SCROLLBACK;
    term->current_line_length = 0;
}

/* UTF-8解码 */
static int utf8_decode(const char **str, uint16_t *out)
{
    const unsigned char *s = (const unsigned char *)*str;
    if (s[0] == 0) return 0;

    if (s[0] < 0x80) {
        *out = s[0];
        (*str)++;
        return 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        if ((s[1] & 0xC0) != 0x80) goto replace;
        *out = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *str += 2;
        return 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) goto replace;
        *out = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *str += 3;
        return 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) goto replace;
        *out = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *str += 4;
        return 4;
    }

replace:
    *out = 0xFFFD;
    (*str)++;
    return 1;
}

/* 获取字符显示宽度 */
static int char_display_width(uint16_t ch)
{
    if (ch < 0x80) return 6;
    return 12;
}

static void uart_put_utf8(uint16_t ch)
{
    uint8_t buf[4];
    int len;
    if (ch < 0x80) {
        buf[0] = ch;
        len = 1;
    } else if (ch < 0x800) {
        buf[0] = 0xC0 | (ch >> 6);
        buf[1] = 0x80 | (ch & 0x3F);
        len = 2;
    } else {
        buf[0] = 0xE0 | (ch >> 12);
        buf[1] = 0x80 | ((ch >> 6) & 0x3F);
        buf[2] = 0x80 | (ch & 0x3F);
        len = 3;
    }
    uart_write_bytes(UART_NUM_0, buf, len);
}

void term_putchar(terminal_t *term, uint16_t ch)
{
    if (ch == '\n') {
        uart_put_utf8('\r');
        uart_put_utf8('\n');
        term_newline(term);
        return;
    }
    if (ch == '\r') return;
    if (ch == '\b' || ch == 0x7F) {
        term_backspace(term);
        return;
    }
    if (ch == '\t') {
        for (int i = 0; i < 4; i++) term_putchar(term, ' ');
        return;
    }

    uart_put_utf8(ch);

    int w = char_display_width(ch);

    /* 检查是否需要换行 */
    if (term->cursor_x + w > TFT_WIDTH) {
        term_newline(term);
    }

    /* 写入当前行 */
    int line_len = term->current_line_length;
    if (line_len < TERM_LINE_MAX - 1) {
        term->scrollback[term->current_line][line_len] = ch;
        term->current_line_length = line_len + 1;
    }

    term->cursor_x += w;
    term->dirty = 1;
}

void term_newline(terminal_t *term)
{
    term_commit_line(term);
    term->cursor_x = 0;
    term->cursor_y += TERM_LINE_H;

    /* 滚动 */
    if (term->cursor_y + TERM_LINE_H > TFT_HEIGHT) {
        term->cursor_y = (TERM_ROWS - 1) * TERM_LINE_H;
        /* 向前滚动 */
        term->visible_start = (term->scrollback_head - TERM_ROWS + 1 + TERM_SCROLLBACK) % TERM_SCROLLBACK;
        if (term->visible_start < 0) term->visible_start += TERM_SCROLLBACK;
    }

    term->dirty = 1;
}

void term_backspace(terminal_t *term)
{
    if (term->current_line_length <= 0) return;
    if (term->cursor_x <= 0) return;

    term->current_line_length--;
    uint16_t last = term->scrollback[term->current_line][term->current_line_length];
    term->scrollback[term->current_line][term->current_line_length] = 0;

    int w = char_display_width(last);
    term->cursor_x -= w;
    if (term->cursor_x < 0) term->cursor_x = 0;

    term->dirty = 1;
}

void term_clear(terminal_t *term)
{
    memset(term->scrollback, 0, sizeof(term->scrollback));
    memset(term->scrollback_len, 0, sizeof(term->scrollback_len));
    for (int i = 0; i < TERM_SCROLLBACK; i++)
        term->line_fg_color[i] = 0xC618;
    term->scrollback_head = 0;
    term->scrollback_count = 0;
    term->current_line = 0;
    term->current_line_length = 0;
    term->visible_start = 0;
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->dirty = 1;
}

void term_clear_line(terminal_t *term)
{
    term->current_line_length = 0;
    term->cursor_x = 0;
    memset(term->scrollback[term->current_line], 0, TERM_LINE_MAX * sizeof(uint16_t));
    term->dirty = 1;
}

void term_cursor_left(terminal_t *term)
{
    if (term->current_line_length <= 0) return;
    if (term->cursor_x <= 0) return;

    uint16_t ch = term->scrollback[term->current_line][term->current_line_length - 1];
    int w = char_display_width(ch);
    term->current_line_length--;
    term->cursor_x -= w;
    if (term->cursor_x < 0) term->cursor_x = 0;

    term->dirty = 1;
}

void term_cursor_right(terminal_t *term)
{
    (void)term;
}

void term_cursor_home(terminal_t *term)
{
    term->cursor_x = 0;
    term->current_line_length = 0;
    term->dirty = 1;
}

void term_cursor_set(terminal_t *term, int x, int y)
{
    term->cursor_x = x;
    term->cursor_y = y;
}

void term_set_color(terminal_t *term, uint8_t fg, uint8_t bg)
{
    term->fg_color = fg;
    term->bg_color = bg;
    if (fg != 8)
        term->fg_custom = 0xC618;
    if (bg != 8)
        term->bg_custom = 0;
}

void term_puts(terminal_t *term, const char *str)
{
    while (*str) {
        if (*str == '\n') {
            term_newline(term);
            str++;
        } else if (*str == '\r') {
            str++;
        } else {
            uint16_t ch;
            int bytes = utf8_decode(&str, &ch);
            if (bytes > 0) {
                term_putchar(term, ch);
            } else {
                str++;
            }
        }
    }
    term->dirty = 1;
}

void term_put_utf8(terminal_t *term, const char *utf8_str)
{
    term_puts(term, utf8_str);
}

void term_render(terminal_t *term)
{
    if (!term->dirty) return;
    term->dirty = 0;

    int blink_on = (xTaskGetTickCount() / pdMS_TO_TICKS(500)) & 1;

    uint16_t fg = term_get_fg_rgb(term);
    uint16_t bg = term_get_bg_rgb(term);

    /* 计算可见行范围 */
    int num_visible = TFT_HEIGHT / TERM_LINE_H;
    if (num_visible > TERM_ROWS) num_visible = TERM_ROWS;

    /* 确定可见起始位置 */
    int start_line = term->visible_start;

    /* 清屏 */
    display_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, bg);

    /* 渲染每一行 */
    for (int row = 0; row < num_visible; row++) {
        int line_idx = (start_line + row) % TERM_SCROLLBACK;
        if (line_idx < 0) line_idx += TERM_SCROLLBACK;

        /* 跳过空行 */
        if (row >= term->scrollback_count && line_idx != term->current_line) continue;

        int y = row * TERM_LINE_H;
        int x = 0;

        uint16_t line_fg = term->line_fg_color[line_idx];
        if (line_fg == 0) line_fg = fg;

        int len = (line_idx == term->current_line)
            ? term->current_line_length : term->scrollback_len[line_idx];
        for (int col = 0; col < len && col < TERM_LINE_MAX; col++) {
            uint16_t ch = term->scrollback[line_idx][col];
            if (ch == 0) break;

            int w = display_draw_char_cn(x, y, ch, line_fg, bg);
            x += w;

            if (x >= TFT_WIDTH) break;
        }
    }

    /* 渲染光标 (在当前行末尾闪烁) */
    int cursor_line_idx = term->current_line;
    int vis_cursor_row = -1;

    for (int row = 0; row < num_visible; row++) {
        int line_idx = (start_line + row) % TERM_SCROLLBACK;
        if (line_idx == cursor_line_idx) {
            vis_cursor_row = row;
            break;
        }
    }

    if (vis_cursor_row >= 0 && blink_on) {
        int cy = vis_cursor_row * TERM_LINE_H + TERM_FONT_H - 3;
        int cx = term->cursor_x;
        if (cx < TFT_WIDTH - 2) {
            display_fill_rect(cx, cy + 1, 6, 2, fg);
        }
    }

    /* 刷新到显示 */
    display_flush_all();
}
