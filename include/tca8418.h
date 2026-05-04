#ifndef __TCA8418_H__
#define __TCA8418_H__

#include <stdint.h>

/* I2C 地址 (7位) */
#define TCA8418_I2C_ADDR    0x34

/* 寄存器 */
#define TCA8418_REG_CFG         0x01
#define TCA8418_REG_INT_STAT    0x02
#define TCA8418_REG_KEY_LC      0x03
#define TCA8418_REG_GPIO_INT_LVL1 0x0C
#define TCA8418_REG_GPIO_INT_LVL2 0x0D
#define TCA8418_REG_DEBOUNCE_DIS1 0x0E
#define TCA8418_REG_GPIO_PULL1   0x0F
#define TCA8418_REG_GPIO_PULL2   0x10
#define TCA8418_REG_GPIO_PULL3   0x11
#define TCA8418_REG_KEY_EVENT_A 0x15
#define TCA8418_REG_KEY_EVENT_B 0x16
#define TCA8418_REG_KEY_LC_0    0x1D
#define TCA8418_REG_KEY_LC_1    0x1E
#define TCA8418_REG_KEY_LC_2    0x1F
#define TCA8418_REG_KEY_LC_3    0x20
#define TCA8418_REG_KEY_LC_4    0x21
#define TCA8418_REG_KEY_LC_5    0x22
#define TCA8418_REG_KEY_LC_6    0x23
#define TCA8418_REG_KEY_LC_7    0x24
#define TCA8418_REG_KEY_LC_8    0x25
#define TCA8418_REG_KEY_LC_9    0x26

/* CFG 寄存器位 */
#define TCA8418_CFG_KE_IEN      (1 << 6)  /* 键盘中断使能 */
#define TCA8418_CFG_GPI_IEN     (1 << 5)  /* GPIO 中断使能 */
#define TCA8418_CFG_INT_CFG     (1 << 1)  /* 中断配置: 1=开漏低电平, 0=推挽 */
#define TCA8418_CFG_OVR_FLOW_M  (1 << 0)  /* FIFO 溢出时覆盖旧事件 */

/* INT_STAT 位 */
#define TCA8418_INT_KE          (1 << 6)  /* 键盘事件 */
#define TCA8418_INT_GP          (1 << 5)  /* GPIO 事件 */

/* 键盘行列数 */
#define TCA8418_ROWS 8
#define TCA8418_COLS 10
#define TCA8418_KEYS (TCA8418_ROWS * TCA8418_COLS)

/* 事件格式 */
#define TCA8418_EVENT_MASK      0x7F
#define TCA8418_EVENT_RELEASE   0x80

/* ---- API ---- */

/* 初始化 I2C 和 TCA8418 */
int tca8418_init(void);

/* 读取一个按键事件, 返回 unicode 字符.
 *   返回 >0  = 按下的字符
 *   返回 0   = 无按键
 *   返回 -1  = 键释放 (可忽略)
 */
int tca8418_read_key(uint16_t *ch);

#endif
