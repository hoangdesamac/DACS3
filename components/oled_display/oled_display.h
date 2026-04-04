#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdint.h>
#include "sensors.h" 

/**
 * Initialize SSD1306 OLED display over I2C
 * Returns 0 on success, -1 on failure
 */
int oled_init(void);

/**
 * Display sensor data on OLED
 */
void oled_display_sensor_data(const sensor_data_t *data);

/**
 * Clear the display
 */
void oled_clear(void);

/**
 * Send raw command to OLED
 */
void oled_send_cmd(uint8_t cmd);

/**
 * Send raw data to OLED
 */
void oled_send_data(uint8_t *data, uint16_t len);

#endif // OLED_DISPLAY_H
