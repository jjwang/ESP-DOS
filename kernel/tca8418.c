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

/* 键映射: 键索引 = row * MAX_COLS + col, MAX_COLS=10
 * 4×4 数字键盘接 R0-R3, C0-C3
 *   R0: 1  2  3  Enter
 *   R1: 4  5  6  Bksp
 *   R2: 7  8  9  Space
 *   R3: *  0  #  Esc
 * 硬件键码 = row * 10 + col + 1
 */
static const uint16_t s_keymap[TCA8418_KEYS] = {
    '*',  '0',  '#',  'D',  0, 0, 0, 0, 0, 0,   /* TCA R0 = 物理 R3 */
    '7',  '8',  '9',  'C',  0, 0, 0, 0, 0, 0,   /* TCA R1 = 物理 R2 */
    '4',  '5',  '6',  'B',  0, 0, 0, 0, 0, 0,   /* TCA R2 = 物理 R1 */
    '1',  '2',  '3',  'A',  0, 0, 0, 0, 0, 0,   /* TCA R3 = 物理 R0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(I2C_NUM_1, TCA8418_I2C_ADDR, buf, 2, pdMS_TO_TICKS(50));
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_write_read_device(I2C_NUM_1, TCA8418_I2C_ADDR,
                                        &reg, 1, val, 1, pdMS_TO_TICKS(50));
}

int tca8418_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = (gpio_num_t)CONFIG_I2C_SDA_PIN,
        .scl_io_num = (gpio_num_t)CONFIG_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_1, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_1, I2C_MODE_MASTER, 0, 0, 0));

    uint8_t who;
    if (reg_read(REG_CFG, &who) != ESP_OK) {
        ESP_LOGE(TAG, "TCA8418 未响应");
        return -1;
    }
    ESP_LOGI(TAG, "CFG init=0x%02x", who);

    /* 解锁配置 */
    reg_write(REG_KEY_LCK_EC, 0x9C);
    vTaskDelay(pdMS_TO_TICKS(2));

    /* 配置行列 GPIO (KP_GPIO)
     * 4 行 (GPIO0-3) + 4 列 (GPIO8-11)
     * mask = (0x0F) + (0x0F << 8) = 0x0F0F
     */
    reg_write(REG_KP_GPIO1, 0x0F);
    reg_write(REG_KP_GPIO2, 0x0F);
    reg_write(REG_KP_GPIO3, 0x00);

    /* 方向: 行=输入(0), 列=输出(1) */
    reg_write(REG_GPIO_DIR1, 0x00);     /* GPIO0-7: 全部输入 */
    reg_write(REG_GPIO_DIR2, 0x0F);     /* GPIO8-11: 输出 */
    reg_write(REG_GPIO_DIR3, 0x00);

    /* GPI 事件模式: 行 R0-R3 使能 */
    reg_write(REG_GPI_EM1, 0x0F);
    reg_write(REG_GPI_EM2, 0x00);
    reg_write(REG_GPI_EM3, 0x00);

    /* 行上拉 (GPIO0-3: 2-bit 编码 10=上拉) */
    reg_write(REG_GPIO_PULL1, 0xAA);
    reg_write(REG_GPIO_PULL2, 0x00);
    reg_write(REG_GPIO_PULL3, 0x00);

    /* 去抖: 使能 (0=使能) */
    reg_write(REG_DEBOUNCE_DIS1, 0x00);
    reg_write(REG_DEBOUNCE_DIS2, 0x00);
    reg_write(REG_DEBOUNCE_DIS3, 0x00);

    /* 锁定 */
    reg_write(REG_KEY_LCK_EC, 0x00);
    vTaskDelay(pdMS_TO_TICKS(2));

    /* 配置 CFG: INT_CFG(开漏) | OVR_FLOW_IEN | KE_IEN */
    reg_write(REG_CFG, CFG_INT_CFG | CFG_OVR_FLOW_IEN | CFG_KE_IEN);
    vTaskDelay(pdMS_TO_TICKS(5));

    /* 清除中断 */
    reg_write(REG_INT_STAT, 0xFF);
    vTaskDelay(pdMS_TO_TICKS(2));

    ESP_LOGI(TAG, "TCA8418 初始化完成");
    return 0;
}

int tca8418_read_key(uint16_t *ch)
{
    uint8_t int_stat;
    if (reg_read(REG_INT_STAT, &int_stat) != ESP_OK)
        return 0;

    if (!(int_stat & INT_STAT_K_INT))
        return 0;

    uint8_t event;
    if (reg_read(REG_KEY_EVENT_A, &event) != ESP_OK) {
        reg_write(REG_INT_STAT, 0xFF);
        return 0;
    }

    reg_write(REG_INT_STAT, 0xFF);

    if (event == 0)
        return 0;

    int pressed = (event & KEY_EVENT_VALUE) != 0;
    int code = event & KEY_EVENT_CODE;

    if (!pressed)
        return -1;

    int idx = code - 1;
    if (idx < 0 || idx >= TCA8418_KEYS)
        return 0;

    uint16_t c = s_keymap[idx];
    if (c == 0)
        return 0;

    *ch = c;
    return 1;
}
