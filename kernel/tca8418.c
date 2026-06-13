#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "tca8418.h"
#include "config.h"

static const char *TAG = "KBD";

#define I2C_MASTER_FREQ_HZ 100000
#define TCA8418_ADDR_LO   0x34
#define TCA8418_ADDR_HI   0x35

static uint8_t s_addr = 0;

/* 键映射: 硬件键码 = row * 10 + col + 1
 * PCB 实际布线 (jaycomp.kicad_pcb):
 *   ROW0: K050-K056 → 物理 R4 右半
 *   ROW1: K043-K049 → 物理 R4 左半
 *   ROW2: K036-K042 → 物理 R3 右半
 *   ROW3: K029-K035 → 物理 R3 左半
 *   ROW4: K022-K028 → 物理 R2 右半
 *   ROW5: K015-K021 → 物理 R2 左半
 *   ROW6: K008-K014 → 物理 R1 右半
 *   ROW7: K001-K007 → 物理 R1 左半
 *
 *   物理布局:
 *   ┌───┬───┬───┬───┬───┬───┬───┐   ┌───┬───┬───┬───┬───┬───┬───┐
 *   │Esc│ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │   │ 7 │ 8 │ 9 │ 0 │ - │ = │Bsp│
 *   ├───┼───┼───┼───┼───┼───┼───┤   ├───┼───┼───┼───┼───┼───┼───┤
 *   │ q │ w │ e │ r │ t │ y │ u │   │ i │ o │ p │ [ │ ] │ \ │ ↵ │
 *   ├───┼───┼───┼───┼───┼───┼───┤   ├───┼───┼───┼───┼───┼───┼───┤
 *   │Cap│ a │ s │ d │ f │ g │ h │   │ j │ k │ l │ ; │ ' │ ↑ │Spc│
 *   ├───┼───┼───┼───┼───┼───┼───┤   ├───┼───┼───┼───┼───┼───┼───┤
 *   │ ` │ , │ z │ x │ c │ v │ b │   │ n │ m │ . │ / │ ← │ ↓ │ → │
 *   └───┴───┴───┴───┴───┴───┴───┘   └───┴───┴───┴───┴───┴───┴───┘
 */
static const uint16_t s_keymap[TCA8418_KEYS] = {
    /* ROW0 (K050-K056): n, m, ., /, ←, ↓, → */
    'n', 'm', '.', '/', KEY_LEFT, KEY_DOWN, KEY_RIGHT, 0,0,0,
    /* ROW1 (K043-K049): `, ,, z, x, c, v, b */
    '`', ',', 'z', 'x', 'c', 'v', 'b', 0,0,0,
    /* ROW2 (K036-K042): j, k, l, ;, ', ↑, Space */
    'j', 'k', 'l', ';', '\'', KEY_UP, ' ', 0,0,0,
    /* ROW3 (K029-K035): 0(caps), a, s, d, f, g, h */
    0, 'a', 's', 'd', 'f', 'g', 'h', 0,0,0,
    /* ROW4 (K022-K028): i, o, p, [, ], \, Enter */
    'i', 'o', 'p', '[', ']', '\\', '\r', 0,0,0,
    /* ROW5 (K015-K021): q, w, e, r, t, y, u */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 0,0,0,
    /* ROW6 (K008-K014): 7, 8, 9, 0, -, =, Bksp */
    '7', '8', '9', '0', '-', '=', 0x7F, 0,0,0,
    /* ROW7 (K001-K007): Esc, 1, 2, 3, 4, 5, 6 */
    0x1B, '1', '2', '3', '4', '5', '6', 0,0,0,
};

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(I2C_NUM_1, s_addr, buf, 2, pdMS_TO_TICKS(50));
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_write_read_device(I2C_NUM_1, s_addr,
                                        &reg, 1, val, 1, pdMS_TO_TICKS(50));
}

static int probe_addr(uint8_t addr)
{
    uint8_t reg = REG_CFG, val;
    esp_err_t ret = i2c_master_write_read_device(I2C_NUM_1, addr, &reg, 1, &val, 1, pdMS_TO_TICKS(20));
    return (ret == ESP_OK) ? 0 : -1;
}

int tca8418_init(void)
{
    /* 配置 INT 引脚 (GPIO6) 为输入 */
    gpio_config_t io_conf = {
        .pin_bit_mask = BIT64(KBD_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    uint8_t addrs[] = {TCA8418_ADDR_LO, TCA8418_ADDR_HI};
    s_addr = 0;
    for (int i = 0; i < (int)(sizeof(addrs) / sizeof(addrs[0])); i++) {
        if (probe_addr(addrs[i]) == 0) {
            s_addr = addrs[i];
            ESP_LOGI(TAG, "TCA8418 发现 @0x%02x", s_addr);
            break;
        }
    }
    if (s_addr == 0) {
        ESP_LOGE(TAG, "TCA8418 未响应 (试过 0x%02x, 0x%02x)", TCA8418_ADDR_LO, TCA8418_ADDR_HI);
        return -1;
    }

    uint8_t who;
    if (reg_read(REG_CFG, &who) != ESP_OK) {
        ESP_LOGE(TAG, "TCA8418 读 CFG 失败");
        return -1;
    }
    ESP_LOGI(TAG, "CFG init=0x%02x", who);

    /* 解锁配置 */
    reg_write(REG_KEY_LCK_EC, 0x9C);
    vTaskDelay(pdMS_TO_TICKS(2));

    /* 配置 8×7 矩阵 (ROW0-7 = GPIO0-7, COL0-6 = GPIO8-14) */
    reg_write(REG_KP_GPIO1, 0xFF);      /* GPIO0-7: 行 */
    reg_write(REG_KP_GPIO2, 0x7F);      /* GPIO8-14: 列 */
    reg_write(REG_KP_GPIO3, 0x00);

    /* 方向: 行=输入(0), 列=输出(1) */
    reg_write(REG_GPIO_DIR1, 0x00);     /* GPIO0-7: 全部输入 (行) */
    reg_write(REG_GPIO_DIR2, 0x7F);     /* GPIO8-14: 输出 (列) */
    reg_write(REG_GPIO_DIR3, 0x00);

    /* GPI 事件模式: 8 行全部使能 */
    reg_write(REG_GPI_EM1, 0xFF);
    reg_write(REG_GPI_EM2, 0x00);
    reg_write(REG_GPI_EM3, 0x00);

    /* 行上拉 (GPIO0-7: 2-bit 编码 10=上拉) */
    reg_write(REG_GPIO_PULL1, 0xAA);    /* GPIO0-3 pull-up */
    reg_write(REG_GPIO_PULL2, 0xAA);    /* GPIO4-7 pull-up */
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
    int processed = 0;

    while (1) {
        uint8_t event;
        if (reg_read(REG_KEY_EVENT_A, &event) != ESP_OK)
            break;

        if (event == 0)
            break;

        int pressed = (event & KEY_EVENT_VALUE) != 0;
        int code = event & KEY_EVENT_CODE;

        if (pressed) {
            int idx = code - 1;
            if (idx >= 0 && idx < TCA8418_KEYS) {
                uint16_t c = s_keymap[idx];
                if (c != 0) {
                    *ch = c;
                    processed = 1;
                }
            }
        }
    }

    reg_write(REG_INT_STAT, 0xFF);

    /* 再读一次: 处理清除 INT_STAT 期间到达的事件 */
    uint8_t late;
    if (reg_read(REG_KEY_EVENT_A, &late) == ESP_OK && late != 0) {
        do {
            int pressed = (late & KEY_EVENT_VALUE) != 0;
            int code = late & KEY_EVENT_CODE;
            if (pressed && processed == 0) {
                int idx = code - 1;
                if (idx >= 0 && idx < TCA8418_KEYS) {
                    uint16_t c = s_keymap[idx];
                    if (c != 0) {
                        *ch = c;
                        processed = 1;
                    }
                }
            }
        } while (reg_read(REG_KEY_EVENT_A, &late) == ESP_OK && late != 0);
        reg_write(REG_INT_STAT, 0xFF);
    }

    return processed;
}
