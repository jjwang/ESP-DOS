#ifndef __DISPLAY_ST7789_H__
#define __DISPLAY_ST7789_H__

#include <stdint.h>
#include "config.h"

void display_init(void);
void display_fill(uint16_t color);
void display_draw_pixel(int x, int y, uint16_t color);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_draw_char_ascii(int x, int y, int ch, uint16_t fg, uint16_t bg);
int  display_draw_char_cn(int x, int y, uint16_t unicode, uint16_t fg, uint16_t bg);
void display_flush(int x, int y, int w, int h);
void display_flush_all(void);
void display_set_backlight(uint8_t brightness);
int  display_get_width(void);
int  display_get_height(void);
void display_draw_large_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale);
void display_draw_text(int x, int y, const char *text, uint16_t fg, uint16_t bg);

#endif
