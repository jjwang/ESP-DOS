#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "tca8418.h"
#include "config.h"

static const char *TAG = "KBD";

#define I2C_MASTER_FREQ_HZ 100000

/* 默认 QWERTY 键位映射表 (8行 × 10列 = 80键)
 * 按索引 = row * 10 + col 排列, 0 = 无定义
 */
static const uint16_t s_keymap[TCA8418_KEYS] = {
    /* Row 0: 数字行 */
    '`',  '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',
    /* Row 1: 数字行续 + 功能 */
    '0',  '-',  '=',  0,    0,    0,    0,    0,    0,    0,
    /* Row 2: QWERTY 上排 */
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  'o',  'p',
    /* Row 3: QWERTY 中排 */
    'a',  's',  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
    /* Row 4: QWERTY 下排 */
    'z',  'x',  'c',  'v',  'b',  'n',  'm',  ',',  '.',  '/',
    /* Row 5: 功能/控制 */
    0x1B, 0x09, 0x08, 0x7F, 0x0D, 0x20, 0x1B, 0,    0,    0,
    /* Row 6: 扩展 */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    /* Row 7: 保留 */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
};

static const uint16_t s_keymap_shift[TCA8418_KEYS] = {
    /* Row 0: 数字行 Shift */
    '~',  '!',  '@',  '#',  '$',  '%',  '^',  '&',  '*',  '(',
    /* Row 1: 数字行续 + 功能 */
    ')',  '_',  '+',  0,    0,    0,    0,    0,    0,    0,
    /* Row 2: QWERTY 上排 Shift */
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  'O',  'P',
    /* Row 3: QWERTY 中排 Shift */
    'A',  'S',  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
    /* Row 4: QWERTY 下排 Shift */
    'Z',  'X',  'C',  'V',  'B',  'N',  'M',  '<',  '>',  '?',
    /* Row 5: 功能/控制 */
    0x1B, 0x09, 0x08, 0x7F, 0x0D, 0x20, 0x1B, 0,    0,    0,
    /* Row 6: 扩展 */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    /* Row 7: 保留 */
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
};

/* I2C 写寄存器 */
static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(I2C_NUM_1, TCA8418_I2C_ADDR, buf, 2, pdMS_TO_TICKS(50));
}

/* I2C 读寄存器 */
static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_write_read_device(I2C_NUM_1, TCA8418_I2C_ADDR,
                                        &reg, 1, val, 1, pdMS_TO_TICKS(50));
}

/* 写多字节 (用于 keymap lock 等) */
static esp_err_t reg_write_buf(uint8_t reg, const uint8_t *data, int len)
{
    uint8_t buf[32];
    if (len + 1 > (int)sizeof(buf)) return ESP_ERR_INVALID_SIZE;
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    return i2c_master_write_to_device(I2C_NUM_1, TCA8418_I2C_ADDR, buf, len + 1, pdMS_TO_TICKS(50));
}

int tca8418_init(void)
{
    /* 配置 I2C 主机 */
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_I2C_SDA_PIN,
        .scl_io_num = CONFIG_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_1, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_1, I2C_MODE_MASTER, 0, 0, 0));

    /* 检测 TCA8418 是否存在 */
    uint8_t who;
    if (reg_read(TCA8418_REG_CFG, &who) != ESP_OK) {
        ESP_LOGE(TAG, "TCA8418 未响应, 检查 I2C 接线");
        return -1;
    }

    /* 复位: 先读 CFG, 重新写入清零 */
    reg_write(TCA8418_REG_CFG, 0);
    vTaskDelay(pdMS_TO_TICKS(5));

    /* 配置键盘矩阵尺寸 (8行 × 10列) */
    reg_write(TCA8418_REG_CFG,
              TCA8418_CFG_KE_IEN |          /* 键盘中断使能 */
              TCA8418_CFG_INT_CFG |          /* 开漏低电平中断 */
              0x07);                         /* ROWS=8 (bits 4-2: 010?), COLS=10 (bits 1-0: 01?) */

    /* 清除中断 */
    reg_write(TCA8418_REG_INT_STAT, 0xFF);

    ESP_LOGI(TAG, "TCA8418 初始化完成");
    return 0;
}

int tca8418_read_key(uint16_t *ch)
{
    uint8_t int_stat;
    if (reg_read(TCA8418_REG_INT_STAT, &int_stat) != ESP_OK)
        return 0;

    if (!(int_stat & TCA8418_INT_KE))
        return 0;  /* 无键盘事件 */

    /* 读取 FIFO 中的事件 */
    uint8_t event_a, event_b;
    if (reg_read(TCA8418_REG_KEY_EVENT_A, &event_a) != ESP_OK ||
        reg_read(TCA8418_REG_KEY_EVENT_B, &event_b) != ESP_OK) {
        reg_write(TCA8418_REG_INT_STAT, TCA8418_INT_KE);
        return 0;
    }

    /* 清除中断标志 */
    reg_write(TCA8418_REG_INT_STAT, TCA8418_INT_KE);

    uint16_t event = ((uint16_t)event_b << 8) | event_a;
    int release = (event >> 14) & 1;
    int key_idx = event & 0x3F;  /* 6-bit key index */

    if (key_idx >= TCA8418_KEYS)
        return 0;

    if (release) {
        /* 键释放 — 暂不处理, 仅用于 Shift 状态跟踪(未来扩展) */
        return -1;
    }

    /* 查找 Shift 键索引 (Row5, Col0-1 通常为 Shift/Ctrl) */
    static int shift_pressed = 0;

    /* 检查是否是 Shift (索引 50 或根据实际布局调整) */
    if (key_idx == 50) {  /* 左 Shift */
        shift_pressed = 1;
        return 0;
    }

    uint16_t code = shift_pressed
        ? s_keymap_shift[key_idx]
        : s_keymap[key_idx];

    /* 大多数非 Shift 键按下后复位 shift 状态 */
    if (key_idx != 50)
        shift_pressed = 0;

    if (code == 0)
        return 0;

    *ch = code;
    return 1;
}
