#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "ds3231.h"
#include "config.h"

static const char *TAG = "RTC";

#define I2C_NUM  I2C_NUM_0
#define I2C_FREQ 100000

/* DS3231 寄存器 */
#define REG_SEC    0x00
#define REG_MIN    0x01
#define REG_HOUR   0x02
#define REG_DAY    0x03
#define REG_DATE   0x04
#define REG_MONTH  0x05
#define REG_YEAR   0x06
#define REG_TEMP_MSB 0x11
#define REG_TEMP_LSB 0x12

static uint8_t bcd2bin(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0F); }
static uint8_t bin2bcd(uint8_t bin) { return ((bin / 10) << 4) | (bin % 10); }

static esp_err_t ds3231_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_write_read_device(I2C_NUM, DS3231_I2C_ADDR,
                                        &reg, 1, val, 1, pdMS_TO_TICKS(50));
}

static esp_err_t ds3231_read_buf(uint8_t reg, uint8_t *buf, int len)
{
    return i2c_master_write_read_device(I2C_NUM, DS3231_I2C_ADDR,
                                        &reg, 1, buf, len, pdMS_TO_TICKS(50));
}

int ds3231_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = (gpio_num_t)DS3231_I2C_SDA_PIN,
        .scl_io_num = (gpio_num_t)DS3231_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM, I2C_MODE_MASTER, 0, 0, 0));

    uint8_t sec;
    if (ds3231_read(REG_SEC, &sec) != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 未响应 (SDA=GPIO%d, SCL=GPIO%d)",
                 DS3231_I2C_SDA_PIN, DS3231_I2C_SCL_PIN);
        return -1;
    }
    ESP_LOGI(TAG, "DS3231 检测成功 (I2C_NUM_0, SEC=0x%02x)", sec);
    return 0;
}

int ds3231_get_time(struct tm *tm)
{
    uint8_t buf[7];
    if (ds3231_read_buf(REG_SEC, buf, 7) != ESP_OK)
        return -1;

    tm->tm_sec  = bcd2bin(buf[0] & 0x7F);
    tm->tm_min  = bcd2bin(buf[1] & 0x7F);
    tm->tm_hour = bcd2bin(buf[2] & 0x3F);
    tm->tm_wday = bcd2bin(buf[3] & 0x07) - 1;  /* DS3231: 1=Sun..7=Sat, POSIX: 0=Sun */
    tm->tm_mday = bcd2bin(buf[4] & 0x3F);
    tm->tm_mon  = bcd2bin(buf[5] & 0x1F) - 1;  /* DS3231: 1-12, POSIX: 0-11 */
    tm->tm_year = bcd2bin(buf[6]) + 100;        /* DS3231: 0-99, POSIX: years since 1900 */
    tm->tm_isdst = 0;

    return 0;
}

int ds3231_set_time(const struct tm *tm)
{
    uint8_t buf[8] = {
        REG_SEC,
        bin2bcd(tm->tm_sec),
        bin2bcd(tm->tm_min),
        bin2bcd(tm->tm_hour),
        bin2bcd(tm->tm_wday + 1),   /* POSIX: 0=Sun → DS3231: 1=Sun */
        bin2bcd(tm->tm_mday),
        bin2bcd(tm->tm_mon + 1),    /* POSIX: 0-11 → DS3231: 1-12 */
        bin2bcd(tm->tm_year - 100), /* POSIX: years since 1900 → DS3231: 0-99 */
    };

    return i2c_master_write_to_device(I2C_NUM, DS3231_I2C_ADDR, buf, 8, pdMS_TO_TICKS(50));
}

int ds3231_get_temp(float *temp_c)
{
    uint8_t msb, lsb;
    if (ds3231_read(REG_TEMP_MSB, &msb) != ESP_OK ||
        ds3231_read(REG_TEMP_LSB, &lsb) != ESP_OK)
        return -1;

    *temp_c = (float)((int16_t)((msb << 8) | lsb)) / 256.0f;
    return 0;
}
