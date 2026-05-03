#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "display_st7789.h"
#include "fonts/font_6x12.h"
#include "fonts/font_cn_12x12.h"

static const char *TAG = "ST7789";

static spi_device_handle_t g_spi = NULL;
static uint16_t *g_fb = NULL;       /* 帧缓冲 (PSRAM) */
static int g_fb_initialized = 0;

/* ---- SPI 底层操作 ---- */
static void st7789_write_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    gpio_set_level(TFT_DC_PIN, 0);
    spi_device_transmit(g_spi, &t);
}

static void st7789_write_data(const uint8_t *data, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    gpio_set_level(TFT_DC_PIN, 1);
    spi_device_transmit(g_spi, &t);
}

static void st7789_write_data_byte(uint8_t data)
{
    st7789_write_data(&data, 1);
}

/* ---- 设置绘图窗口 ---- */
static void st7789_set_window(int x0, int y0, int x1, int y1)
{
    /* 列地址 (CASET: 2 bytes start + 2 bytes end) */
    st7789_write_cmd(0x2A);
    st7789_write_data_byte(x0 >> 8);
    st7789_write_data_byte(x0 & 0xFF);
    st7789_write_data_byte(x1 >> 8);
    st7789_write_data_byte(x1 & 0xFF);

    /* 行地址 (RASET: 2 bytes start + 2 bytes end, 加列偏移使170像素居中) */
    int ry0 = y0 + TFT_COL_OFFSET;
    int ry1 = y1 + TFT_COL_OFFSET;
    st7789_write_cmd(0x2B);
    st7789_write_data_byte(ry0 >> 8);
    st7789_write_data_byte(ry0 & 0xFF);
    st7789_write_data_byte(ry1 >> 8);
    st7789_write_data_byte(ry1 & 0xFF);

    st7789_write_cmd(0x2C);  /* 开始写内存 */
}

/* ---- 帧缓冲操作 ---- */
static void fb_init(void)
{
    if (g_fb == NULL) {
        g_fb = (uint16_t *)heap_caps_malloc(TFT_WIDTH * TFT_HEIGHT * 2, MALLOC_CAP_SPIRAM);
        if (g_fb == NULL) {
            ESP_LOGE(TAG, "无法分配帧缓冲!");
            g_fb = (uint16_t *)malloc(TFT_WIDTH * TFT_HEIGHT * 2);
            if (g_fb == NULL) {
                ESP_LOGE(TAG, "致命: 无法分配内存!");
                abort();
            }
        }
        memset(g_fb, 0, TFT_WIDTH * TFT_HEIGHT * 2);
        g_fb_initialized = 1;
    }
}

/* ---- 公开API ---- */

void display_init(void)
{
    ESP_LOGI(TAG, "初始化 ST7789 %dx%d...", TFT_WIDTH, TFT_HEIGHT);

    /* 配置GPIO (不含背光, 背光最后才初始化) */
    uint64_t pin_mask = (1ULL << TFT_CS_PIN) | (1ULL << TFT_DC_PIN) | (1ULL << TFT_RST_PIN);
    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    gpio_set_level(TFT_CS_PIN, 1);
    gpio_set_level(TFT_DC_PIN, 0);
    gpio_set_level(TFT_RST_PIN, 1);

    /* SPI总线 */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = TFT_MOSI_PIN,
        .miso_io_num = -1,
        .sclk_io_num = TFT_SCLK_PIN,
        .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TFT_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 40000000,
        .mode = 0,
        .spics_io_num = TFT_CS_PIN,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(TFT_HOST, &dev_cfg, &g_spi));

    /* 硬件复位 */
    gpio_set_level(TFT_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TFT_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* 初始化序列 */
    st7789_write_cmd(0x01);   vTaskDelay(pdMS_TO_TICKS(150)); /* 软复位 */

    st7789_write_cmd(0x3A);   st7789_write_data_byte(0x05);   /* RGB565 */
    st7789_write_cmd(0x36);   st7789_write_data_byte(TFT_MADCTL); /* 方向 */
    st7789_write_cmd(0x21);   /* 反色开 (IPS屏需要) */
    st7789_write_cmd(0x13);   /* 正常显示 */

    st7789_write_cmd(0x28);   /* Display OFF (确保退出Sleep后不闪) */
    st7789_write_cmd(0x11);   vTaskDelay(pdMS_TO_TICKS(200)); /* Sleep Out */

    /* Sleep Out后立即写入全黑帧 */
    fb_init();
    memset(g_fb, 0, TFT_WIDTH * TFT_HEIGHT * 2);
    display_flush_all();

    st7789_write_cmd(0x29);   /* 显示开 */

    /* 最后才配置背光引脚 */
    gpio_config_t bl_conf = {
        .pin_bit_mask = (1ULL << TFT_BL_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl_conf);
#if TFT_BL_ACTIVE_LOW
    gpio_set_level(TFT_BL_PIN, 0);
#else
    gpio_set_level(TFT_BL_PIN, 1);
#endif

    ESP_LOGI(TAG, "ST7789 初始化完成");
}

void display_fill(uint16_t color)
{
    if (!g_fb) return;
    for (int i = 0; i < TFT_WIDTH * TFT_HEIGHT; i++) {
        g_fb[i] = color;
    }
}

void display_draw_pixel(int x, int y, uint16_t color)
{
    if (!g_fb) return;
    if (x < 0 || x >= TFT_WIDTH || y < 0 || y >= TFT_HEIGHT) return;
    g_fb[y * TFT_WIDTH + x] = color;
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!g_fb) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TFT_WIDTH)  w = TFT_WIDTH - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    for (int row = 0; row < h; row++) {
        int offset = (y + row) * TFT_WIDTH + x;
        for (int col = 0; col < w; col++) {
            g_fb[offset + col] = color;
        }
    }
}

void display_flush(int x, int y, int w, int h)
{
    if (!g_fb) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TFT_WIDTH)  w = TFT_WIDTH - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    st7789_set_window(x, y, x + w - 1, y + h - 1);

    gpio_set_level(TFT_DC_PIN, 1);

    /* 直接发送帧缓冲中的行数据 - ST7789需要大端序 */
    int buf_size = w * 2;
    uint8_t *buf = (uint8_t *)malloc(buf_size);
    if (!buf) return;

    for (int row = 0; row < h; row++) {
        int fb_offset = (y + row) * TFT_WIDTH + x;
        for (int col = 0; col < w; col++) {
            uint16_t px = g_fb[fb_offset + col];
            buf[col * 2]     = (px >> 8) & 0xFF;
            buf[col * 2 + 1] = px & 0xFF;
        }
        spi_transaction_t t = {
            .length = w * 16,
            .tx_buffer = buf,
        };
        spi_device_transmit(g_spi, &t);
    }
    free(buf);
}

void display_flush_all(void)
{
    display_flush(0, 0, TFT_WIDTH, TFT_HEIGHT);
}

/* 放大绘制字符 (scale=2 为2倍) */
void display_draw_large_ascii(int x, int y, int ch, uint16_t fg, uint16_t bg, int scale)
{
    if (ch < 0x20 || ch > 0x7E) ch = ' ';
    int idx = ch - 0x20;
    if (idx < 0 || idx >= FONT_6X12_COUNT) return;
    for (int row = 0; row < FONT_6X12_H; row++) {
        uint8_t bits = font_6x12[idx][row];
        for (int col = 0; col < FONT_6X12_W; col++) {
            uint16_t c = (bits & (0x80 >> col)) ? fg : bg;
            for (int dy = 0; dy < scale; dy++)
                for (int dx = 0; dx < scale; dx++)
                    display_draw_pixel(x + col * scale + dx, y + row * scale + dy, c);
        }
    }
}

void display_draw_large_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale)
{
    int cx = x;
    while (*text) {
        display_draw_large_ascii(cx, y, *text, fg, bg, scale);
        cx += FONT_6X12_W * scale;
        text++;
    }
}

/* 支持中英文混排的文本绘制 (scale=1) */
void display_draw_text(int x, int y, const char *text, uint16_t fg, uint16_t bg)
{
    int cx = x;
    while (*text) {
        unsigned char c = (unsigned char)*text;
        if (c < 0x80) {
            display_draw_char_ascii(cx, y, c, fg, bg);
            cx += 6;
            text++;
        } else {
            uint16_t unicode;
            int consumed = 1;
            if ((c & 0xE0) == 0xC0 && (text[1] & 0xC0) == 0x80) {
                unicode = ((c & 0x1F) << 6) | (text[1] & 0x3F);
                consumed = 2;
            } else if ((c & 0xF0) == 0xE0 && (text[1] & 0xC0) == 0x80 && (text[2] & 0xC0) == 0x80) {
                unicode = ((c & 0x0F) << 12) | ((text[1] & 0x3F) << 6) | (text[2] & 0x3F);
                consumed = 3;
            } else {
                unicode = 0xFFFD;
            }
            cx += display_draw_char_cn(cx, y, unicode, fg, bg);
            text += consumed;
        }
    }
}

void display_set_backlight(uint8_t brightness)
{
#if TFT_BL_ACTIVE_LOW
    gpio_set_level(TFT_BL_PIN, brightness > 0 ? 0 : 1);
#else
    gpio_set_level(TFT_BL_PIN, brightness > 0 ? 1 : 0);
#endif
}

int display_get_width(void)
{
    return TFT_WIDTH;
}

int display_get_height(void)
{
    return TFT_HEIGHT;
}

/* ---- 字体渲染 ---- */

int display_get_char_width(uint16_t ch)
{
    if (ch < 0x80) {
        return 6;  /* ASCII */
    } else {
        return 12; /* 中文 */
    }
}

static int find_cn_font(uint32_t unicode)
{
    int lo = 0, hi = FONT_CN_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint16_t c = font_cn_map[mid].unicode;
        if (c == unicode) return font_cn_map[mid].index;
        if (c < unicode) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

void display_draw_char_ascii(int x, int y, int ch, uint16_t fg, uint16_t bg)
{
    if (!g_fb) return;
    if (ch < 0x20 || ch > 0x7E) ch = ' ';
    int idx = ch - 0x20;
    if (idx < 0 || idx >= FONT_6X12_COUNT) return;

    for (int row = 0; row < FONT_6X12_H; row++) {
        uint8_t bits = font_6x12[idx][row];
        for (int col = 0; col < FONT_6X12_W; col++) {
            uint16_t color = (bits & (0x80 >> col)) ? fg : bg;
            display_draw_pixel(x + col, y + row, color);
        }
    }
}

int display_draw_char_cn(int x, int y, uint16_t unicode, uint16_t fg, uint16_t bg)
{
    if (!g_fb) return 12;

    if (unicode < 0x80) {
        int ch = (unicode >= 0x20 && unicode <= 0x7E) ? unicode : ' ';
        int idx = ch - 0x20;
        if (idx < 0 || idx >= FONT_6X12_COUNT) return 12;

        for (int row = 0; row < FONT_6X12_H; row++) {
            uint8_t bits = font_6x12[idx][row];
            for (int col = 0; col < FONT_6X12_W; col++) {
                uint16_t color = (bits & (0x80 >> col)) ? fg : bg;
                display_draw_pixel(x + col, y + row, color);
            }
        }
        return 6;
    }

    int font_idx = find_cn_font(unicode);
    if (font_idx < 0) {
        /* 未找到的字符显示方框占位 */
        for (int row = 0; row < 12; row++) {
            for (int col = 0; col < 12; col++) {
                int edge = (row == 0 || row == 11 || col == 0 || col == 11);
                display_draw_pixel(x + col, y + row, edge ? fg : bg);
            }
        }
        return 12;
    }

    for (int row = 0; row < FONT_CN_H; row++) {
        uint16_t bits = font_cn_data[font_idx][row];
        for (int col = 0; col < FONT_CN_W; col++) {
            uint16_t color = (bits & (0x8000 >> col)) ? fg : bg;
            display_draw_pixel(x + col, y + row, color);
        }
    }
    return 12;
}
