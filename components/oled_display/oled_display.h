#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdint.h>
#include "sensors.h"

int oled_init(void);

void oled_display_sensor_data(const sensor_data_t *data);

void oled_clear(void);

void oled_send_cmd(uint8_t cmd);

void oled_send_data(const uint8_t *data, uint16_t len);

#endif
