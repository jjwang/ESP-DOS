#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "display_st7789.h"
#include "config.h"
#include "tca8418.h"
#include "terminal.h"

#define GRAVITY       1
#define JUMP_FORCE    12
#define GROUND_Y      140
#define DINO_X        45
#define MAX_OBSTACLES 6
#define MAX_CLOUDS    3
#define INIT_SPEED    5
#define MAX_SPEED     14
#define SPAWN_INTERVAL 120
#define MIN_SPAWN     55

extern QueueHandle_t g_input_queue;

#define COL_DINO    0x4208
#define COL_SKY     0xC638
#define COL_GROUND  0xAD55
#define COL_LINE    0x632C
#define COL_GREEN   0x0560
#define COL_GRAY    0x8410
#define COL_WHITE   0xFFFF
#define COL_RED     0xF800

/* Sprite data: bits packed in lower N bits of uint64_t.
   Bit (w-1) = leftmost column, bit 0 = rightmost column. */

/* ---- Dino: 40 wide × 45 tall ---- */
#define DINO_W 40
#define DINO_H 45

static const uint64_t dino_stand[45] = {
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x000000000007FF80ULL,
    0x00000000000FFF80ULL,
    0x00000000001DFFE0ULL,
    0x00000000001CFFE0ULL,
    0x00000000001DFFE0ULL,
    0x00000000001FFFE0ULL,
    0x00000000001FFFE0ULL,
    0x00000000001FFFE0ULL,
    0x00000000001FFFE0ULL,
    0x00000000001FE000ULL,
    0x00000000001FE000ULL,
    0x00000000001FFF00ULL,
    0x00000000003F8000ULL,
    0x00000030007F8000ULL,
    0x0000003001FF8000ULL,
    0x0000003003FF8000ULL,
    0x000000380FFFF000ULL,
    0x0000003E1FFF9000ULL,
    0x0000003E3FFF9000ULL,
    0x0000003FFFFF8000ULL,
    0x0000003FFFFF8000ULL,
    0x0000003FFFFF8000ULL,
    0x0000000FFFFF8000ULL,
    0x0000000FFFFE0000ULL,
    0x00000007FFFE0000ULL,
    0x00000001FFFC0000ULL,
    0x00000001FFFC0000ULL,
    0x000000007FF00000ULL,
    0x000000007FF00000ULL,
    0x000000003E700000ULL,
    0x000000003C100000ULL,
    0x0000000038100000ULL,
    0x0000000030100000ULL,
    0x0000000030100000ULL,
    0x000000003C1C0000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
};

/* run1: 双腿分开（大步） */
static const uint64_t dino_run1[45] = {
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x000000000003FFF0ULL,
    0x000000000003FFF0ULL,
    0x00000000000F7FFCULL,
    0x00000000000E7FFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FF800ULL,
    0x00000000000FF800ULL,
    0x00000000000FFFC0ULL,
    0x00000000000FF040ULL,
    0x00000060003FE000ULL,
    0x0000006000FFE000ULL,
    0x0000006001FFE000ULL,
    0x0000007807FFFE00ULL,
    0x000000780FFFFE00ULL,
    0x0000007E1FFFE600ULL,
    0x0000007E1FFFE600ULL,
    0x0000007FFFFFE000ULL,
    0x0000007FFFFFE000ULL,
    0x0000007FFFFFE000ULL,
    0x0000001FFFFFE000ULL,
    0x0000001FFFFFC000ULL,
    0x00000007FFFF8000ULL,
    0x00000007FFFF8000ULL,
    0x00000001FFFE0000ULL,
    0x00000001FFFE0000ULL,
    0x000000007FFC0000ULL,
    0x000000007FFC0000ULL,
    0x000000001F0F0000ULL,
    0x000000001F0F0000ULL,
    0x000000001C000000ULL,
    0x0000000018000000ULL,
    0x0000000010000000ULL,
    0x000000001C000000ULL,
    0x000000001C000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
};

/* run2: 双腿并拢（右腿前移，左腿不变） */
static const uint64_t dino_run2[45] = {
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x000000000003FFF0ULL,
    0x000000000003FFF8ULL,
    0x00000000000F7FFCULL,
    0x00000000000E3FFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FF800ULL,
    0x00000000000FFFC0ULL,
    0x00000000000FFFE0ULL,
    0x00000020003FE000ULL,
    0x00000030003FE000ULL,
    0x0000003000FFE000ULL,
    0x0000003001FFE000ULL,
    0x0000003807FFFE00ULL,
    0x0000003C0FFFF600ULL,
    0x0000003E1FFFE600ULL,
    0x0000003FFFFFE000ULL,
    0x0000003FFFFFE000ULL,
    0x0000003FFFFFE000ULL,
    0x0000003FFFFFE000ULL,
    0x0000000FFFFFE000ULL,
    0x0000000FFFFF8000ULL,
    0x00000003FFFF8000ULL,
    0x00000001FFFF0000ULL,
    0x00000001FFFF0000ULL,
    0x000000007FFC0000ULL,
    0x000000007FFC0000ULL,
    0x000000001C3C0000ULL,
    0x000000001C3C0000ULL,
    0x00000000070C0000ULL,
    0x00000000070C0000ULL,
    0x00000000000C0000ULL,
    0x00000000000C0000ULL,
    0x00000000000F0000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
};

/* jump: 跳跃/缩腿 */
static const uint64_t dino_jump[45] = {
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x000000000003FFF0ULL,
    0x000000000003FFF8ULL,
    0x00000000000F7FFCULL,
    0x00000000000E3FFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FFFFCULL,
    0x00000000000FF800ULL,
    0x00000000000FFFC0ULL,
    0x00000000000FFFE0ULL,
    0x00000020003FE000ULL,
    0x00000030003FE000ULL,
    0x0000003000FFE000ULL,
    0x0000003001FFE000ULL,
    0x0000003807FFFE00ULL,
    0x0000003C0FFFF600ULL,
    0x0000003E1FFFE600ULL,
    0x0000003FFFFFE000ULL,
    0x0000003FFFFFE000ULL,
    0x0000003FFFFFE000ULL,
    0x0000003FFFFFE000ULL,
    0x0000000FFFFFE000ULL,
    0x0000000FFFFF8000ULL,
    0x00000003FFFF8000ULL,
    0x00000001FFFF0000ULL,
    0x00000001FFFF0000ULL,
    0x000000007FFC0000ULL,
    0x000000007FFC0000ULL,
    0x000000001F3C0000ULL,
    0x000000001F3C0000ULL,
    0x000000001C0C0000ULL,
    0x000000001C0C0000ULL,
    0x00000000180C0000ULL,
    0x000000001C0C0000ULL,
    0x000000001C0F0000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
};


static const uint64_t dino_duck2[35] = {
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000004000000000ULL,
    0x0000004000003FF8ULL,
    0x0000004000003FF8ULL,
    0x000000783FFC3FF8ULL,
    0x000000783FFC7FFEULL,
    0x0000007FFFFFF7FEULL,
    0x0000007FFFFFF7FEULL,
    0x0000003FFFFFFFFEULL,
    0x0000003FFFFFFFFEULL,
    0x0000003FFFFFFFFEULL,
    0x0000001FFFFFFFFEULL,
    0x0000001FFFFFFFFEULL,
    0x00000007FFFFFFFEULL,
    0x00000007FFFFFFFEULL,
    0x00000003FFFFFF00ULL,
    0x00000003FFFFFF00ULL,
    0x00000003FFFFFF00ULL,
    0x00000000FFFE3FF0ULL,
    0x00000000FFFE3FF0ULL,
    0x000000007FFE0000ULL,
    0x000000007F840000ULL,
    0x00000000F1E40000ULL,
    0x00000000F1E60000ULL,
    0x00000000F1E60000ULL,
    0x00000000E0060000ULL,
    0x00000000E0000000ULL,
    0x00000000C0000000ULL,
    0x00000000C0000000ULL,
    0x00000000E0000000ULL,
    0x00000000E0000000ULL,
    0x00000000E0000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
};


static const uint64_t dino_dead[45] = {
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x000000000003FFF0ULL,
    0x000000000003FFF0ULL,
    0x00000000000F1FF8ULL,
    0x00000000000E5FF8ULL,
    0x00000000000E5FF8ULL,
    0x00000000000E1FF8ULL,
    0x00000000000FFFF8ULL,
    0x00000000000FFFF8ULL,
    0x00000000000FFFF8ULL,
    0x00000000000FFFF8ULL,
    0x00000000000FFFF8ULL,
    0x00000000000FFFC0ULL,
    0x00000000000FFFC0ULL,
    0x00000010001FE000ULL,
    0x00000010003FE000ULL,
    0x0000001000FFE000ULL,
    0x0000001000FFE000ULL,
    0x0000001C07FFFC00ULL,
    0x0000001C07FFE400ULL,
    0x0000001E1FFFE400ULL,
    0x0000001FFFFFE000ULL,
    0x0000001FFFFFE000ULL,
    0x0000001FFFFFE000ULL,
    0x0000000FFFFFE000ULL,
    0x0000000FFFFF8000ULL,
    0x00000003FFFF8000ULL,
    0x00000003FFFF8000ULL,
    0x00000000FFFE0000ULL,
    0x00000000FFFE0000ULL,
    0x000000003FFC0000ULL,
    0x000000003FF80000ULL,
    0x000000001F380000ULL,
    0x000000001E080000ULL,
    0x000000001C080000ULL,
    0x0000000018080000ULL,
    0x0000000018080000ULL,
    0x000000001C0E0000ULL,
    0x000000000C0E0000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
};

static const uint64_t dino_duck1[35] = {
    0x0000000000000000ULL,
    0x0000000000000000ULL,
    0x0000006000000000ULL,
    0x0000006000003FF8ULL,
    0x0000006000003FF8ULL,
    0x000000781FFC3FF8ULL,
    0x000000781FFC7FFEULL,
    0x0000007FFFFFF7FEULL,
    0x0000007FFFFFF7FEULL,
    0x0000001FFFFFFFFEULL,
    0x0000001FFFFFFFFEULL,
    0x0000001FFFFFFFFEULL,
    0x0000000FFFFFFFFEULL,
    0x0000000FFFFFFFFEULL,
    0x00000007FFFFFFFEULL,
    0x00000007FFFFFFFEULL,
    0x00000001FFFFFF00ULL,
    0x00000001FFFFFF00ULL,
    0x00000001FFFFFF00ULL,
    0x00000000FFFF3FF0ULL,
    0x00000000FFFF3FF0ULL,
    0x000000007FFF0000ULL,
    0x000000007FC40000ULL,
    0x0000000047840000ULL,
    0x0000000047870000ULL,
    0x0000000047870000ULL,
    0x0000000077070000ULL,
    0x0000000077000000ULL,
    0x0000000004000000ULL,
    0x0000000004000000ULL,
    0x0000000007000000ULL,
    0x0000000007000000ULL,
    0x0000000007000000ULL,
    0x0000000000000000ULL,
    0x0000000000000000ULL,
};

/* ---- Obstacles: 1x pixel data (rendered at 2x) ---- */
#define CACTUS_SMALL_W 10
#define CACTUS_SMALL_H 19
static const uint64_t cactus_small_data[19] = {
    0x0000000000000000ULL,
    0x0000000000000010ULL,
    0x0000000000000038ULL,
    0x0000000000000038ULL,
    0x000000000000003AULL,
    0x000000000000003AULL,
    0x00000000000000BAULL,
    0x00000000000000BAULL,
    0x00000000000000BAULL,
    0x00000000000000BEULL,
    0x00000000000000BCULL,
    0x00000000000000F8ULL,
    0x0000000000000078ULL,
    0x0000000000000038ULL,
    0x0000000000000038ULL,
    0x0000000000000038ULL,
    0x0000000000000038ULL,
    0x0000000000000038ULL,
    0x0000000000000010ULL,
};


#define CACTUS_LARGE_W 12
#define CACTUS_LARGE_H 24
static const uint64_t cactus_large_data[24] = {
    0x0000000000000000ULL,
    0x00000000000000E0ULL,
    0x00000000000000E0ULL,
    0x00000000000000E0ULL,
    0x00000000000000E0ULL,
    0x00000000000000E0ULL,
    0x00000000000000E6ULL,
    0x0000000000000CE6ULL,
    0x0000000000000CE6ULL,
    0x0000000000000CE6ULL,
    0x0000000000000CE6ULL,
    0x0000000000000CE6ULL,
    0x0000000000000CE6ULL,
    0x0000000000000CE6ULL,
    0x0000000000000FFEULL,
    0x00000000000007FCULL,
    0x00000000000003F0ULL,
    0x00000000000000E0ULL,
    0x00000000000000E0ULL,
    0x00000000000000E0ULL,
    0x00000000000000E0ULL,
    0x00000000000000E0ULL,
    0x00000000000000E0ULL,
    0x00000000000000E0ULL,
};

#define PTERO_W 20
#define PTERO_H 7
static const uint64_t ptero_data1[7] = {
    0x0000000000004000ULL,
    0x000000000001E000ULL,
    0x0000000000013FC0ULL,
    0x0000000000000FF0ULL,
    0x0000000000000720ULL,
    0x0000000000000C00ULL,
    0x0000000000000000ULL,
};
static const uint64_t ptero_data2[7] = {
    0x0000000000000000ULL,
    0x0000000000001000ULL,
    0x000000000000CC00ULL,
    0x000000000003EF00ULL,
    0x0000000000003F80ULL,
    0x0000000000000FF0ULL,
    0x0000000000000020ULL,
};



typedef enum { OBJ_CACTUS_SMALL, OBJ_CACTUS_LARGE, OBJ_PTERO } obj_type_t;

typedef struct {
    int x, y;
    int active;
    obj_type_t type;
    const uint64_t *data;
    const uint64_t *data2;
    int w, h;     /* 1x pixel data dimensions */
    int anim_frame, anim_timer;
    int frac_x; /* 子像素位置 (×10) */
} obstacle_t;

typedef struct { int x, y; int active; } cloud_t;
typedef enum { STATE_WAITING, STATE_PLAYING, STATE_GAME_OVER } game_state_t;

static obstacle_t obs[MAX_OBSTACLES];
static cloud_t clouds[MAX_CLOUDS];
static int dino_y, dino_vy, ducking;
static int score, high_score, speed, frame, spawn_timer, ground_x;
static game_state_t state;
static int blink_timer, blink_state; /* 0=normal eye, 1=blinking */
static int duck_release_timer; /* 下蹲释放后延迟帧数 */

/* Draw a 1-bit sprite at 1x: scan for horizontal runs, render with fill_rect.
   Bits stored LSB-aligned: bit (w-1-col) = column col. */
static void draw_sprite(int x, int y, const uint64_t *data, int w, int h, uint16_t color) {
    for (int row = 0; row < h; row++) {
        uint64_t bits = data[row];
        int col = 0;
        while (col < w) {
            if (bits & (1ULL << (w - 1 - col))) {
                int start = col;
                while (col < w && (bits & (1ULL << (w - 1 - col))))
                    col++;
                display_fill_rect(x + start, y + row, col - start, 1, color);
            } else {
                col++;
            }
        }
    }
}

/* Draw a 1-bit sprite at 2x (each pixel = 2×2 block, grouped into runs). */
static void draw_sprite_2x(int x, int y, const uint64_t *data, int w, int h, uint16_t color) {
    for (int row = 0; row < h; row++) {
        uint64_t bits = data[row];
        int col = 0;
        while (col < w) {
            if (bits & (1ULL << (w - 1 - col))) {
                int start = col;
                while (col < w && (bits & (1ULL << (w - 1 - col))))
                    col++;
                display_fill_rect(x + start * 2, y + row * 2, (col - start) * 2, 2, color);
            } else {
                col++;
            }
        }
    }
}

#define DINO_DUCK_H 35

static void draw_dino_pixels(int x, int y, const uint64_t *data, int h, int eye_state) {
    draw_sprite(x, y, data, DINO_W, h, COL_DINO);
    if (h == DINO_DUCK_H) {
        if (eye_state == 1) display_fill_rect(x + 28, y + 7, 2, 2, COL_WHITE);
        return;
    }
    if (eye_state == 1) { /* 活着：小白点 */
        display_fill_rect(x + 23, y + 6, 2, 2, COL_WHITE);
    } /* eye_state == 2: 死掉不画眼睛 */
}

static void draw_ground(void) {
    display_fill_rect(0, 0, 320, GROUND_Y, COL_SKY);
    display_fill_rect(0, GROUND_Y, 320, 170 - GROUND_Y, COL_GROUND);
    display_fill_rect(0, GROUND_Y, 320, 2, COL_LINE);
    for (int x = -(ground_x % 50); x < 320; x += 50) {
        display_fill_rect(x, GROUND_Y + 5, 3, 2, COL_LINE);
        display_fill_rect(x + 20, GROUND_Y + 9, 4, 2, COL_LINE);
    }
}

static void draw_dino(void) {
    int x = DINO_X, y = dino_y;
    int on_ground = (dino_y >= GROUND_Y - DINO_H);
    int eye = 1; /* 默认活着 = 小白点 */

    if (state == STATE_GAME_OVER) eye = 2; /* 死了 = 白+黑 */
    else if (state == STATE_WAITING) eye = blink_state; /* 待机闪烁 */

    /* 每个精灵脚底位置不同，单独调Y偏移使其贴地 */
    if (state == STATE_WAITING) {
        draw_dino_pixels(x, y + 6, dino_stand, DINO_H, eye);
        return;
    }
    if (state == STATE_GAME_OVER) {
        draw_dino_pixels(x, y, dino_dead, DINO_H, eye);
        return;
    }
    if (ducking && on_ground) {
        draw_dino_pixels(x, y + 13, (frame / 6) % 2 ? dino_duck1 : dino_duck2, DINO_DUCK_H, eye);
        return;
    }
    if (!on_ground || dino_vy < 0) {
        draw_dino_pixels(x, y + 5, dino_jump, DINO_H, eye);
    } else if ((frame / 6) % 2 == 0) {
        draw_dino_pixels(x, y + 4, dino_run1, DINO_H, eye);
    } else {
        draw_dino_pixels(x, y + 5, dino_run2, DINO_H, eye);
    }
}

static void draw_obstacles(void) {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obs[i].active) continue;
        uint16_t c = (obs[i].type == OBJ_PTERO) ? COL_GRAY : COL_GREEN;
        const uint64_t *d = obs[i].data;
        if (obs[i].data2 && obs[i].anim_frame) d = obs[i].data2;
        draw_sprite_2x(obs[i].x, obs[i].y, d, obs[i].w, obs[i].h, c);
    }
}

static void draw_clouds(void) {
    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (!clouds[i].active) continue;
        display_fill_rect(clouds[i].x, clouds[i].y, 28, 6, COL_WHITE);
        display_fill_rect(clouds[i].x + 4, clouds[i].y - 4, 20, 4, COL_WHITE);
        display_fill_rect(clouds[i].x + 8, clouds[i].y - 7, 12, 3, COL_WHITE);
    }
}

static void draw_score(void) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%04d", score);
    for (int i = 0; i < len; i++)
        display_draw_char_ascii(260 + i * 7, 8, buf[i], COL_DINO, COL_SKY);
}

static void draw_game_over(void) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "GAME OVER");
    int x = (320 - len * 8) / 2;
    for (int i = 0; i < len; i++)
        display_draw_char_ascii(x + i * 8, 70, buf[i], COL_RED, COL_SKY);
    if (score > 0) {
        len = snprintf(buf, sizeof(buf), "SCORE: %d", score);
        x = (320 - len * 8) / 2;
        for (int i = 0; i < len; i++)
            display_draw_char_ascii(x + i * 8, 84, buf[i], COL_DINO, COL_SKY);
    }
    if (high_score > 0) {
        len = snprintf(buf, sizeof(buf), "BEST: %d", high_score);
        x = (320 - len * 7) / 2;
        for (int i = 0; i < len; i++)
            display_draw_char_ascii(x + i * 7, 98, buf[i], COL_DINO, COL_SKY);
    }
    const char *again = "PRESS ENTER";
    x = (320 - 11 * 7) / 2;
    for (int i = 0; again[i]; i++)
        display_draw_char_ascii(x + i * 7, 114, again[i], COL_GRAY, COL_SKY);
}

static void draw_title(void) {
    display_draw_large_text((320 - 4 * 12) / 2, 50, "DINO", COL_DINO, COL_SKY, 2);
    display_draw_text((320 - 6 * 6) / 2, 80, "RUNNER", COL_DINO, COL_SKY);
    display_draw_text((320 - 11 * 6) / 2, 100, "PRESS ENTER", COL_GRAY, COL_SKY);
}

static void render_frame(void) {
    draw_ground(); draw_clouds(); draw_obstacles(); draw_dino(); draw_score();
    if (state == STATE_WAITING) draw_title();
    if (state == STATE_GAME_OVER) draw_game_over();
    display_flush_all();
}

static void reset_game(void) {
    dino_y = GROUND_Y - DINO_H; dino_vy = 0; ducking = 0;
    score = 0; speed = INIT_SPEED; frame = 0; spawn_timer = 0; ground_x = 0;
    blink_timer = 100 + rand() % 150; blink_state = 0; duck_release_timer = 0;
    for (int i = 0; i < MAX_OBSTACLES; i++) obs[i].active = 0;
    for (int i = 0; i < MAX_CLOUDS; i++) {
        clouds[i].x = rand() % 320;
        clouds[i].y = rand() % 50 + 10;
        clouds[i].active = 1;
    }
    state = STATE_PLAYING;
}

static void spawn_obstacle(void) {
    int r = rand() % 20;
    obj_type_t t; const uint64_t *data; int w, h;
    if (r < 10) {
        t = OBJ_CACTUS_SMALL; data = cactus_small_data; w = CACTUS_SMALL_W; h = CACTUS_SMALL_H;
    } else if (r < 17) {
        t = OBJ_CACTUS_LARGE; data = cactus_large_data; w = CACTUS_LARGE_W; h = CACTUS_LARGE_H;
    } else {
        t = OBJ_PTERO; data = ptero_data1; w = PTERO_W; h = PTERO_H;
    }
    int y = (t == OBJ_PTERO) ? GROUND_Y - 50 - rand() % 30 : GROUND_Y - h * 2;
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obs[i].active) {
            obs[i].x = 320; obs[i].frac_x = 3200; obs[i].y = y; obs[i].active = 1;
            obs[i].type = t; obs[i].data = data; obs[i].w = w; obs[i].h = h;
            obs[i].data2 = (t == OBJ_PTERO) ? ptero_data2 : NULL;
            obs[i].anim_frame = 0; obs[i].anim_timer = 0;
            break;
        }
    }
}

static void update_obstacles(void) {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obs[i].active) continue;
        obs[i].frac_x -= speed * 10;
        obs[i].x = obs[i].frac_x / 10;
        if (obs[i].x < -50) obs[i].active = 0;
        if (obs[i].data2) {
            if (++obs[i].anim_timer >= 8) {
                obs[i].anim_timer = 0;
                obs[i].anim_frame = !obs[i].anim_frame;
            }
        }
    }
    if (--spawn_timer <= 0) {
        int interval = SPAWN_INTERVAL - score / 40;
        if (interval < MIN_SPAWN) interval = MIN_SPAWN;
        spawn_timer = interval; spawn_obstacle();
    }
}

static int check_collision(void) {
    int dx = DINO_X + 6, dw = DINO_W - 12;
    int dy = dino_y + 6, dh = DINO_H - 10;
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obs[i].active) continue;
        int ox = obs[i].x + 6, ow = obs[i].w * 2 - 12;
        int oy = obs[i].y + 4, oh = obs[i].h * 2 - 8;
        if (dx < ox + ow && dx + dw > ox && dy < oy + oh && dy + dh > oy) return 1;
    }
    return 0;
}

static void update_clouds(void) {
    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (!clouds[i].active) continue;
        clouds[i].x -= 1;
        if (clouds[i].x < -40) {
            clouds[i].x = 320 + rand() % 60;
            clouds[i].y = rand() % 50 + 10;
        }
    }
}

void dino_run(void) {
    display_fill(COL_SKY);
    srand(42); reset_game(); state = STATE_WAITING;
    TickType_t last = xTaskGetTickCount();

    while (1) {
        uint16_t ch = 0;
        while (xQueueReceive(g_input_queue, &ch, 0) == pdTRUE) {
            if (ch == 0x001B) goto exit;
            if (ch == '\r' || ch == '\n') {
                if (state == STATE_WAITING || state == STATE_GAME_OVER) {
                    reset_game(); render_frame();
                    last = xTaskGetTickCount(); goto framedone;
                }
            }
            if ((ch == KEY_UP || ch == ' ') && state == STATE_PLAYING && dino_y >= GROUND_Y - DINO_H)
                dino_vy = -JUMP_FORCE;
            if (ch == KEY_DOWN && state == STATE_PLAYING) ducking = 1;
        }

        if (state == STATE_PLAYING) {
            if (ducking && !tca8418_is_pressed(KEY_DOWN)) ducking = 0;
            dino_vy += GRAVITY; dino_y += dino_vy;
            int gy = GROUND_Y - DINO_H;
            if (dino_y >= gy) { dino_y = gy; dino_vy = 0; }
            score++; frame++;
            if (score % 80 == 0 && speed < MAX_SPEED) speed++;
            ground_x = (ground_x + speed) % 200;
            update_obstacles(); update_clouds();
            if (check_collision()) { state = STATE_GAME_OVER; if (score > high_score) high_score = score; }
        }
        if (state == STATE_WAITING) {
            if (--blink_timer <= 0) {
                blink_state = !blink_state;
                blink_timer = blink_state ? 6 : (80 + rand() % 120);
            }
        }
        render_frame();
        TickType_t now = xTaskGetTickCount();
        int32_t ms = (now - last) * portTICK_PERIOD_MS;
        if (ms < 30) vTaskDelay(pdMS_TO_TICKS(30 - ms));
        last = xTaskGetTickCount();
framedone: ;
    }
exit:
    g_terminal.dirty = 1;
    term_render(&g_terminal);
}
