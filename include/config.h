#ifndef __CONFIG_H__
#define __CONFIG_H__

/* ==========================================================
 * Jaycomp-DOS - 配置文件
 * 根据你的硬件连接修改以下引脚定义
 * ========================================================== */

#include <stdint.h>

/* ---- ST7789 显示屏引脚配置 ---- */
/*
 * 常见接线方式:
 *   1.9寸 320x170 ST7789 模块:
 *     TFT_CS   -> GPIO4
 *     TFT_DC   -> GPIO5
 *     TFT_RST  -> GPIO6
 *     TFT_MOSI -> GPIO7
 *     TFT_SCLK -> GPIO15
 *     TFT_BL   -> GPIO16
 *
 *   其他常见接法:
 *     TFT_CS   -> GPIO10
 *     TFT_DC   -> GPIO11
 *     TFT_RST  -> GPIO12
 *     TFT_MOSI -> GPIO11
 *     TFT_SCLK -> GPIO12
 *     TFT_BL   -> GPIO7
 *
 *   请根据你实际购买的模块调整！
 */
/*
 * ESP32-1732S019 (来自官方文档):
 *   TFT_CS   -> GPIO10
 *   TFT_DC   -> GPIO11
 *   TFT_RST  -> GPIO1
 *   TFT_MOSI -> GPIO13
 *   TFT_SCLK -> GPIO12
 *   TFT_BL   -> GPIO14 (高电平点亮)
 *   TFT_MISO -> GPIO12 (与SCLK共用?? 但显示只写不读)
 */
#define TFT_CS_PIN      GPIO_NUM_10
#define TFT_DC_PIN      GPIO_NUM_11
#define TFT_RST_PIN     GPIO_NUM_1
#define TFT_MOSI_PIN    GPIO_NUM_13
#define TFT_SCLK_PIN    GPIO_NUM_12

#define TFT_BL_PIN      GPIO_NUM_14
#define TFT_BL_ACTIVE_LOW   0   /* 高电平点亮 */

/* ST7789 列偏移 (240-170)/2 = 35, 使170像素居中显示 */
#define TFT_COL_OFFSET  35

#define TFT_HOST        SPI2_HOST
#define TFT_SPI_CLOCK_HZ    40000000  /* 40MHz SPI时钟 */
#define TFT_WIDTH       320
#define TFT_HEIGHT      170
#define TFT_MADCTL      0x60    /* 横屏模式: MV=1, MX=1 */

/* ---- 终端配置 ---- */
#define TERM_FONT_W     12      /* 字体宽度(像素) - 统一12px */
#define TERM_FONT_H     12      /* 字体高度(像素) */
#define TERM_LINE_SPACE 2       /* 行间距(像素) */
#define TERM_LINE_H     (TERM_FONT_H + TERM_LINE_SPACE)  /* 14px */
#define TERM_COLS       (TFT_WIDTH / TERM_FONT_W)        /* 26列 - 只用于中文计数 */
#define TERM_ROWS       (TFT_HEIGHT / TERM_LINE_H)       /* 12行 */
#define TERM_SCROLLBACK 200     /* 回滚行数 */

/* ---- 颜色定义 (RGB565) ---- */
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_GREEN     0x07E0
#define COLOR_RED       0xF800
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_ORANGE    0xFD20
#define COLOR_GRAY      0x8410
#define COLOR_DARK_GRAY 0x4208
#define COLOR_LIGHT_GRAY 0xC618  /* RGB565 浅灰 */
#define COLOR_AMBER     0xFB60  /* 琥珀色 - 经典终端颜色 */
#define COLOR_DOS_GREEN 0x07E0  /* DOS 经典绿 */

/* ---- TCA8418 键盘 I2C 引脚 (I2C_NUM_1) ---- */
#define CONFIG_I2C_SDA_PIN  4
#define CONFIG_I2C_SCL_PIN  5
#define KBD_INT_PIN         6       /* TCA8418 INT (开漏, 有10k上拉) */

/* ---- DS3231 RTC I2C 引脚 (I2C_NUM_0) ---- */
#define DS3231_I2C_SDA_PIN  15
#define DS3231_I2C_SCL_PIN  16

/* ---- 系统配置 ---- */
#define SHELL_PROMPT    "A:\\>"
#define COLOR_LIGHT_BLUE  0x07E0  /* DOS 经典绿 */
#define SHELL_HISTORY   16
#define MAX_CMD_LEN     256
#define MAX_ARGS        16

/* ---- 文件名常量 ---- */
#define VERSION_STR     "Jaycomp-DOS Version 1.0"
#define BUILD_DATE      __DATE__ " " __TIME__

#endif /* __CONFIG_H__ */
