#ifndef __DS3231_H__
#define __DS3231_H__

#include <stdint.h>
#include <time.h>

#define DS3231_I2C_ADDR 0x68

int ds3231_init(void);
int ds3231_get_time(struct tm *tm);
int ds3231_set_time(const struct tm *tm);
int ds3231_get_temp(float *temp_c);

#endif
