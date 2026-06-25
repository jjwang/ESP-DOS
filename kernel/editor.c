#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "display_st7789.h"
#include "config.h"
#include "tca8418.h"
#include "vfs.h"

#define EDIT_MAX_LINES   1024
#define EDIT_LINE_LEN    256
#define EDIT_VIS_ROWS    10
#define EDIT_VIS_COLS    53

typedef struct {
    char **lines;
    int line_count;
    int capacity;
    int cx, cy;
    int top_line;
    int modified;
    char filename[128];
} editor_t;

typedef struct {
    const char *name;
    const char *items[8];
    int count;
} menu_t;

static QueueHandle_t *g_edit_queue;

static menu_t menus[2] = {
    {"File", {"Save", "Quit"}, 2},
    {"Help", {"About ESP-DOS Editor 1.0"}, 1}
};
#define MENU_COUNT 2

static void editor_free(editor_t *ed)
{
    for (int i = 0; i < ed->line_count; i++)
        free(ed->lines[i]);
    free(ed->lines);
}

static int editor_load(editor_t *ed, const char *path)
{
    vfs_file_t *fp = vfs_open(path, VFS_O_RDONLY);
    if (!fp) return 0;

    char buf[EDIT_LINE_LEN];
    while (ed->line_count < EDIT_MAX_LINES) {
        int len = 0;
        char c;
        int has_data = 0;
        while (vfs_read(fp, &c, 1) == 1) {
            has_data = 1;
            if (c == '\n') break;
            if (c != '\r' && len < EDIT_LINE_LEN - 1) buf[len++] = c;
        }
        if (!has_data && len == 0) break;
        buf[len] = '\0';
        if (ed->line_count >= ed->capacity) {
            ed->capacity = ed->capacity ? ed->capacity * 2 : 64;
            ed->lines = realloc(ed->lines, ed->capacity * sizeof(char *));
        }
        ed->lines[ed->line_count] = strdup(buf);
        ed->line_count++;
    }
    vfs_close(fp);
    strncpy(ed->filename, path, sizeof(ed->filename) - 1);
    return 1;
}

static int editor_save(editor_t *ed)
{
    if (!ed->filename[0]) {
        snprintf(ed->filename, sizeof(ed->filename), "untitled.txt");
    }
    vfs_file_t *fp = vfs_open(ed->filename, VFS_O_WRONLY | VFS_O_CREAT);
    if (!fp) return -1;
    for (int i = 0; i < ed->line_count; i++) {
        vfs_write(fp, ed->lines[i], strlen(ed->lines[i]));
        vfs_write(fp, "\n", 1);
    }
    vfs_close(fp);
    ed->modified = 0;
    return 0;
}

static void editor_draw_char(int x, int y, uint16_t ch, uint16_t fg, uint16_t bg)
{
    display_fill_rect(x, y, 6, 12, bg);
    if (ch >= 0x20 && ch <= 0x7E)
        display_draw_char_cn(x, y, ch, fg, bg);
}

static void editor_draw_line_text(int row, const char *text, uint16_t fg, uint16_t bg)
{
    int y = (row + 1) * 14;
    display_fill_rect(0, y, 320, 12, bg);
    int x = 0;
    for (int i = 0; text[i] && i < EDIT_VIS_COLS; i++) {
        editor_draw_char(x, y, (uint16_t)(unsigned char)text[i], fg, bg);
        x += 6;
    }
}

static void editor_render(editor_t *ed, int menu_mode, int menu_idx, int dropdown_open, int dropdown_sel)
{
    display_fill(COLOR_BLACK);

    uint16_t bar_bg = COLOR_BLUE;
    display_fill_rect(0, 0, 320, 14, bar_bg);
    int mx = 6;
    for (int m = 0; m < MENU_COUNT; m++) {
        uint16_t mg_fg = COLOR_WHITE;
        uint16_t mg_bg = bar_bg;
        if (menu_mode && menu_idx == m) {
            if (dropdown_open) {
                mg_bg = COLOR_YELLOW;
                mg_fg = COLOR_BLACK;
            } else {
                mg_bg = COLOR_GRAY;
                mg_fg = COLOR_BLACK;
            }
        }
        for (int i = 0; menus[m].name[i]; i++) {
            if (menu_mode && menu_idx == m) {
                display_fill_rect(mx - 1, 0, 7, 14, mg_bg);
            } else {
                display_fill_rect(mx, 0, 6, 14, mg_bg);
            }
            editor_draw_char(mx, 1, (uint16_t)(unsigned char)menus[m].name[i], mg_fg, mg_bg);
            mx += 6;
        }
        mx += 18;
    }

    for (int r = 0; r < EDIT_VIS_ROWS; r++) {
        int line_idx = ed->top_line + r;
        if (line_idx < ed->line_count) {
            editor_draw_line_text(r, ed->lines[line_idx], COLOR_GREEN, COLOR_BLACK);
        } else {
            display_fill_rect(0, (r + 1) * 14, 320, 12, COLOR_BLACK);
        }
    }

    int cursor_line = ed->top_line + ed->cy;
    int col_pos = ed->cx < EDIT_LINE_LEN ? ed->cx : 0;
    char status[180];
    snprintf(status, sizeof(status), "%s%s  Ln %d, Col %d",
             ed->filename[0] ? ed->filename : "Untitled",
             ed->modified ? " *" : "",
             cursor_line + 1, col_pos + 1);
    display_fill_rect(0, 11 * 14, 320, 14, COLOR_BLUE);
    int sx = 0;
    for (int i = 0; status[i] && i < 50; i++) {
        editor_draw_char(sx, 11 * 14 + 1, (uint16_t)(unsigned char)status[i], COLOR_WHITE, COLOR_BLUE);
        sx += 6;
    }

    if (!dropdown_open && !menu_mode) {
        int cursor_y = (ed->cy + 1) * 14;
        int cursor_x = ed->cx * 6;
        display_fill_rect(cursor_x, cursor_y, 6, 12, COLOR_YELLOW);
        if (ed->top_line + ed->cy < ed->line_count &&
            ed->cx < (int)strlen(ed->lines[ed->top_line + ed->cy])) {
            editor_draw_char(cursor_x, cursor_y,
                (uint16_t)(unsigned char)ed->lines[ed->top_line + ed->cy][ed->cx],
                COLOR_BLACK, COLOR_YELLOW);
        }
    }

    if (dropdown_open && menu_idx >= 0 && menu_idx < MENU_COUNT) {
        int bx = menu_idx * 6 * 6 + 6;
        if (menu_idx > 0) bx += 18;
        int dd_h = menus[menu_idx].count * 14;
        int dd_y = 14;
        int dd_w = 0;
        for (int i = 0; i < menus[menu_idx].count; i++) {
            int l = strlen(menus[menu_idx].items[i]);
            if (l * 6 > dd_w) dd_w = l * 6;
        }
        dd_w += 12;
        if (bx + dd_w > 320) bx = 320 - dd_w;
        display_fill_rect(bx, dd_y, dd_w, dd_h, COLOR_GRAY);
        for (int i = 0; i < menus[menu_idx].count; i++) {
            int iy = dd_y + i * 14;
            uint16_t ifg = COLOR_WHITE;
            uint16_t ibg = COLOR_GRAY;
            if (i == dropdown_sel) {
                ifg = COLOR_BLACK;
                ibg = COLOR_YELLOW;
                display_fill_rect(bx - 1, iy, dd_w + 1, 14, ibg);
            } else {
                display_fill_rect(bx, iy, dd_w, 14, ibg);
            }
            int ix = bx + 6;
            for (int j = 0; menus[menu_idx].items[i][j]; j++) {
                editor_draw_char(ix, iy + 1, (uint16_t)(unsigned char)menus[menu_idx].items[i][j], ifg, ibg);
                ix += 6;
            }
        }
    }

    display_flush_all();
}

static uint16_t editor_readkey(void)
{
    uint16_t ch;
    if (xQueueReceive(*g_edit_queue, &ch, portMAX_DELAY) == pdTRUE)
        return ch;
    return 0;
}

static int editor_show_confirm(editor_t *ed, const char *msg)
{
    display_fill_rect(0, 11 * 14, 320, 14, COLOR_BLACK);
    int x = 0;
    for (int i = 0; msg[i]; i++) {
        editor_draw_char(x, 11 * 14 + 1, (uint16_t)(unsigned char)msg[i], COLOR_RED, COLOR_BLACK);
        x += 6;
    }
    display_flush_all();
    uint16_t ch = editor_readkey();
    return (ch == 'y' || ch == 'Y');
}

static void editor_insert_char(editor_t *ed, int line, int col, char c)
{
    if (line >= ed->line_count) {
        if (line >= ed->capacity) {
            ed->capacity = ed->capacity ? ed->capacity * 2 : 64;
            ed->lines = realloc(ed->lines, ed->capacity * sizeof(char *));
        }
        ed->lines[line] = calloc(EDIT_LINE_LEN, 1);
        ed->line_count = line + 1;
    }
    char *s = ed->lines[line];
    int len = strlen(s);
    if (len >= EDIT_LINE_LEN - 1) return;
    if (col < len) {
        memmove(s + col + 1, s + col, len - col + 1);
    }
    s[col] = c;
    ed->modified = 1;
}

static void editor_delete_char(editor_t *ed, int line, int col)
{
    if (line >= ed->line_count) return;
    char *s = ed->lines[line];
    int len = strlen(s);
    if (col >= len) return;
    memmove(s + col, s + col + 1, len - col);
    ed->modified = 1;
}

static void editor_newline(editor_t *ed)
{
    int line = ed->top_line + ed->cy;
    if (line >= ed->line_count) return;
    char *s = ed->lines[line];
    int len = strlen(s);
    int split = ed->cx < len ? ed->cx : len;

    if (ed->line_count >= EDIT_MAX_LINES) return;
    if (ed->line_count >= ed->capacity) {
        ed->capacity = ed->capacity ? ed->capacity * 2 : 64;
        ed->lines = realloc(ed->lines, ed->capacity * sizeof(char *));
    }
    for (int i = ed->line_count; i > line + 1; i--)
        ed->lines[i] = ed->lines[i - 1];
    ed->lines[line + 1] = strdup(s + split);
    s[split] = '\0';
    ed->line_count++;
    ed->modified = 1;
    ed->cx = 0;
    if (ed->cy < EDIT_VIS_ROWS - 1) {
        ed->cy++;
    } else {
        ed->top_line++;
    }
}

static void editor_backspace(editor_t *ed)
{
    int line = ed->top_line + ed->cy;
    if (ed->cx > 0) {
        editor_delete_char(ed, line, ed->cx - 1);
        ed->cx--;
    } else if (ed->cy > 0 || ed->top_line > 0) {
        int prev_line = line - 1;
        int prev_len = prev_line >= 0 ? strlen(ed->lines[prev_line]) : 0;
        if (prev_line >= 0 && line < ed->line_count) {
            char *s = ed->lines[line];
            if (prev_len + strlen(s) < EDIT_LINE_LEN - 1) {
                strcat(ed->lines[prev_line], s);
                free(ed->lines[line]);
                for (int i = line; i < ed->line_count - 1; i++)
                    ed->lines[i] = ed->lines[i + 1];
                ed->line_count--;
                ed->modified = 1;
                ed->cx = prev_len;
                if (ed->cy > 0) ed->cy--;
                else ed->top_line--;
            }
        }
    }
}

void editor_run(const char *filename)
{
    extern QueueHandle_t g_input_queue;
    g_edit_queue = &g_input_queue;

    editor_t ed;
    memset(&ed, 0, sizeof(ed));
    ed.cx = 0;

    if (filename && filename[0]) {
        strncpy(ed.filename, filename, sizeof(ed.filename) - 1);
        editor_load(&ed, filename);
    }
    if (ed.line_count == 0) {
        ed.capacity = 64;
        ed.lines = calloc(ed.capacity, sizeof(char *));
        ed.lines[0] = calloc(EDIT_LINE_LEN, 1);
        ed.line_count = 1;
    }

    int menu_mode = 0;
    int menu_idx = 0;
    int dropdown_open = 0;
    int dropdown_sel = 0;

    editor_render(&ed, 0, 0, 0, 0);

    while (1) {
        uint16_t ch = editor_readkey();

        if (dropdown_open) {
            if (ch == KEY_UP) {
                if (dropdown_sel > 0) dropdown_sel--;
                editor_render(&ed, menu_mode, menu_idx, dropdown_open, dropdown_sel);
            } else if (ch == KEY_DOWN) {
                int mc = menus[menu_idx].count;
                if (dropdown_sel < mc - 1) dropdown_sel++;
                editor_render(&ed, menu_mode, menu_idx, dropdown_open, dropdown_sel);
            } else if (ch == KEY_LEFT) {
                if (menu_idx > 0) { menu_idx--; dropdown_sel = 0; }
                editor_render(&ed, menu_mode, menu_idx, dropdown_open, dropdown_sel);
            } else if (ch == KEY_RIGHT) {
                if (menu_idx < MENU_COUNT - 1) { menu_idx++; dropdown_sel = 0; }
                editor_render(&ed, menu_mode, menu_idx, dropdown_open, dropdown_sel);
            } else if (ch == '\r' || ch == '\n') {
                if (menu_idx == 0) {
                    if (dropdown_sel == 0) {
                        editor_save(&ed);
                    } else if (dropdown_sel == 1) {
                        if (ed.modified) {
                            if (editor_show_confirm(&ed, "Unsaved! Save? (y/n)"))
                                editor_save(&ed);
                        }
                        editor_free(&ed);
                        return;
                    }
                }
                menu_mode = 0;
                dropdown_open = 0;
                editor_render(&ed, 0, 0, 0, 0);
            } else if (ch == 0x1B) {
                dropdown_open = 0;
                editor_render(&ed, 1, menu_idx, 0, 0);
            }
            continue;
        }

        if (menu_mode) {
            if (ch == KEY_LEFT) {
                if (menu_idx > 0) menu_idx--;
                editor_render(&ed, 1, menu_idx, 0, 0);
            } else if (ch == KEY_RIGHT) {
                if (menu_idx < MENU_COUNT - 1) menu_idx++;
                editor_render(&ed, 1, menu_idx, 0, 0);
            } else if (ch == KEY_DOWN || ch == '\r' || ch == '\n') {
                if (menu_idx == 1) {
                    dropdown_open = 1;
                    dropdown_sel = 0;
                    editor_render(&ed, 1, menu_idx, 1, 0);
                    uint16_t k = editor_readkey();
                    dropdown_open = 0;
                    menu_mode = 0;
                    editor_render(&ed, 0, 0, 0, 0);
                } else {
                    dropdown_open = 1;
                    dropdown_sel = 0;
                    editor_render(&ed, 1, menu_idx, 1, 0);
                }
            } else if (ch == 0x1B) {
                menu_mode = 0;
                editor_render(&ed, 0, 0, 0, 0);
            }
            continue;
        }

        if (ch == 0x1B) {
            menu_mode = 1;
            menu_idx = 0;
            dropdown_open = 0;
            editor_render(&ed, 1, 0, 0, 0);
            continue;
        }

        int line = ed.top_line + ed.cy;
        int len = line < ed.line_count ? strlen(ed.lines[line]) : 0;

        if (ch == KEY_UP) {
            if (ed.cy > 0) ed.cy--;
            else if (ed.top_line > 0) ed.top_line--;
            len = (ed.top_line + ed.cy < ed.line_count) ?
                  strlen(ed.lines[ed.top_line + ed.cy]) : 0;
            if (ed.cx > len) ed.cx = len;
        } else if (ch == KEY_DOWN) {
            if (ed.cy < EDIT_VIS_ROWS - 1 && ed.top_line + ed.cy + 1 < ed.line_count) ed.cy++;
            else if (ed.top_line + EDIT_VIS_ROWS < ed.line_count) ed.top_line++;
            len = (ed.top_line + ed.cy < ed.line_count) ?
                  strlen(ed.lines[ed.top_line + ed.cy]) : 0;
            if (ed.cx > len) ed.cx = len;
        } else if (ch == KEY_LEFT) {
            if (ed.cx > 0) ed.cx--;
            else if (ed.cy > 0) { ed.cy--; ed.cx = strlen(ed.lines[ed.top_line + ed.cy]); }
            else if (ed.top_line > 0) { ed.top_line--; ed.cy = 0; ed.cx = strlen(ed.lines[ed.top_line]); }
        } else if (ch == KEY_RIGHT) {
            if (ed.cx < len) ed.cx++;
            else if (ed.cy < EDIT_VIS_ROWS - 1 && ed.top_line + ed.cy + 1 < ed.line_count) {
                ed.cy++; ed.cx = 0;
            } else if (ed.top_line + EDIT_VIS_ROWS < ed.line_count) {
                ed.top_line++; ed.cy = EDIT_VIS_ROWS - 1; ed.cx = 0;
            }
        } else if (ch == '\r' || ch == '\n') {
            editor_newline(&ed);
        } else if (ch == '\b' || ch == 0x7F) {
            editor_backspace(&ed);
        } else if (ch >= 0x20 && ch <= 0x7E) {
            editor_insert_char(&ed, line, ed.cx, (char)ch);
            ed.cx++;
        }

        editor_render(&ed, 0, 0, 0, 0);
    }
}
