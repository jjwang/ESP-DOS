#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_flash.h"

#include "config.h"
#include "display_st7789.h"
#include "terminal.h"
#include "vfs.h"
#include "shell.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "tca8418.h"

static const char *TAG = "MAIN";

static shell_t g_shell;
static QueueHandle_t g_input_queue = NULL;

#define UART_QUEUE_SIZE 64

static int log_vprintf(const char *fmt, va_list args)
{
    return esp_rom_vprintf(fmt, args);
}

static void uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NUM_0, &cfg);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
}

/* TCA8418 键盘输入任务 */
static void kbd_task(void *arg)
{
    (void)arg;
    if (tca8418_init() != 0) {
        ESP_LOGW(TAG, "键盘初始化失败, 禁用");
        vTaskDelete(NULL);
        return;
    }
    while (1) {
        uint16_t ch;
        int ret = tca8418_read_key(&ch);
        if (ret > 0 && g_input_queue)
            xQueueSend(g_input_queue, &ch, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* 串口输入任务 */
static void input_task(void *arg)
{
    uint8_t buf[64];
    while (1) {
        int len = uart_read_bytes(UART_NUM_0, buf, sizeof(buf), pdMS_TO_TICKS(50));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                uint8_t ch = buf[i];
                static uint8_t ub[4];
                static int ul = 0, ue = 0;

                if (ue == 0) {
                    if (ch < 0x80) { uint16_t u = ch; xQueueSend(g_input_queue, &u, 0); }
                    else if ((ch & 0xE0) == 0xC0) { ub[0] = ch; ue = 1; ul = 1; }
                    else if ((ch & 0xF0) == 0xE0) { ub[0] = ch; ue = 2; ul = 1; }
                    else if ((ch & 0xF8) == 0xF0) { ub[0] = ch; ue = 3; ul = 1; }
                } else if ((ch & 0xC0) == 0x80) {
                    ub[ul++] = ch;
                    if (--ue == 0) {
                        uint16_t u;
                        if (ul == 2) u = ((ub[0] & 0x1F) << 6) | (ub[1] & 0x3F);
                        else if (ul == 3) u = ((ub[0] & 0x0F) << 12) | ((ub[1] & 0x3F) << 6) | (ub[2] & 0x3F);
                        else u = 0xFFFD;
                        xQueueSend(g_input_queue, &u, 0);
                    }
                } else { ue = 0; }
            }
        }
    }
}

/* Shell处理任务 */
static void shell_task(void *arg)
{
    (void)arg;

    /* 初始化文件系统 */
    if (vfs_init() != 0) {
        term_puts(&g_terminal, "文件系统初始化失败!\n");
    } else {
        uint32_t total = 0, used = 0;
        vfs_info(&total, &used);
        char info[128];
        snprintf(info, sizeof(info), "文件系统就绪: %dK / %dK\n",
                 (int)(used / 1024), (int)(total / 1024));
        term_puts(&g_terminal, info);
    }

    term_puts(&g_terminal, "\n");
    term_render(&g_terminal);

    /* Shell初始化 */
    shell_init(&g_shell, &g_terminal);

    /* 打印欢迎和提示符 */
    shell_print_prompt(&g_shell);
    term_render(&g_terminal);

    /* 主循环 - 带输入超时自动执行 */
    uint16_t ch;
    int idle_count = 0;
    while (g_shell.running) {
        if (xQueueReceive(g_input_queue, &ch, pdMS_TO_TICKS(50)) == pdTRUE) {
            idle_count = 0;
            if (ch < 0x80) {
                uint8_t ascii = (uint8_t)ch;
                uart_write_bytes(UART_NUM_0, &ascii, 1);
            }

            shell_process_char(&g_shell, ch);

            if (ch == '\r' || ch == '\n') {
                uint8_t lf = '\n';
                uart_write_bytes(UART_NUM_0, &lf, 1);
            }
        } else {
            idle_count++;
            /* 每100ms刷新显示 (用于光标闪烁) */
            if (idle_count % 2 == 0) {
                g_terminal.dirty = 1;
                term_render(&g_terminal);
            }
            if (idle_count > 4 && g_shell.input_len > 0) {
                idle_count = 0;
                uint16_t cr = '\r';
                shell_process_char(&g_shell, cr);
                uint8_t lf = '\n';
                uart_write_bytes(UART_NUM_0, &lf, 1);
            }
        }
    }
}

/* 显示启动画面 (直接绘制, 不依赖终端) */
static void show_splash(void)
{
    display_fill(COLOR_BLACK);

    /* OpenCrab Logo 2倍大小 */
    int logo_w = 8 * 6 * 2;   /* 96px */
    int logo_h = 12 * 2;      /* 24px */
    int lx = (TFT_WIDTH - logo_w) / 2;
    int ly = 20;

    display_draw_large_text(lx + 1, ly + 1, "OpenCrab", 0x4208, COLOR_BLACK, 2);
    display_draw_large_text(lx, ly, "OpenCrab", 0xFFFF, COLOR_BLACK, 2);

    uint32_t flash_sz = 0;
    esp_flash_get_size(NULL, &flash_sz);
    size_t psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    int iy = ly + logo_h + 16;
    int col1 = 16;
    int lh = 14;

    display_draw_text(col1, iy, "型号: ESP32-1732S019", 0xC618, COLOR_BLACK);
    display_draw_text(col1, iy + lh, "芯片: ESP32-S3  双核", 0xC618, COLOR_BLACK);
    char buf[48];
    snprintf(buf, sizeof(buf), "内存: PSRAM %dMB  Flash %dMB",
             (int)(psram / 1024 / 1024), (int)(flash_sz / 1024 / 1024));
    display_draw_text(col1, iy + lh * 2, buf, 0xC618, COLOR_BLACK);
    snprintf(buf, sizeof(buf), "显示: ST7789  %dx%d", TFT_WIDTH, TFT_HEIGHT);
    display_draw_text(col1, iy + lh * 3, buf, 0xC618, COLOR_BLACK);
    snprintf(buf, sizeof(buf), "版本: v0.1.0  %s %s", __DATE__, __TIME__);
    display_draw_text(col1, iy + lh * 4, buf, 0xC618, COLOR_BLACK);

    display_flush_all();
}

void app_main(void)
{
    esp_log_set_vprintf(log_vprintf);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* 初始化NVS (需要用于SPIFFS) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    uart_init();

    /* 初始化显示 */
    ESP_LOGI(TAG, "初始化显示...");
    display_init();

    /* 初始化终端 */
    ESP_LOGI(TAG, "初始化终端...");
    term_init(&g_terminal);

    /* 显示启动画面 + 倒计时 */
    show_splash();

    int iy = TFT_HEIGHT / 2 + 30;
    char buf[64];

    display_fill_rect(0, iy, TFT_WIDTH, 14, COLOR_BLACK);
    snprintf(buf, sizeof(buf), "编译: %s %s", __DATE__, __TIME__);
    display_draw_text(16, iy, buf, 0xC618, COLOR_BLACK);

    iy += 16;
    display_fill_rect(0, iy, TFT_WIDTH, 14, COLOR_BLACK);
    for (int i = 3; i > 0; i--) {
        snprintf(buf, sizeof(buf), "系统启动中, 剩余 %d 秒...  ", i);
        display_draw_text(16, iy, buf, 0xFFFF, COLOR_BLACK);
        display_flush_all();
        vTaskDelay(pdMS_TO_TICKS(1000));
        display_fill_rect(0, iy, TFT_WIDTH, 14, COLOR_BLACK);
    }
    display_fill(COLOR_BLACK);
    display_flush_all();

    /* 初始化WiFi (netif/event loop只需一次) */
    ESP_LOGI(TAG, "初始化WiFi...");
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    /* 创建输入队列 */
    g_input_queue = xQueueCreate(UART_QUEUE_SIZE, sizeof(uint16_t));
    if (!g_input_queue) {
        ESP_LOGE(TAG, "无法创建输入队列!");
        return;
    }

    /* 创建串口输入任务 (在APP_CPU上运行) */
    TaskHandle_t input_handle;
    xTaskCreatePinnedToCore(input_task, "uart_input", 4096, NULL, 10, &input_handle, 1);

    /* 创建键盘输入任务 */
    xTaskCreatePinnedToCore(kbd_task, "kbd", 4096, NULL, 9, NULL, 1);

    /* 创建Shell任务 (在PRO_CPU上运行) */
    TaskHandle_t shell_handle;
    xTaskCreatePinnedToCore(shell_task, "shell", 8192, NULL, 8, &shell_handle, 0);

    /* 主任务退出 */
    vTaskDelete(NULL);
}
