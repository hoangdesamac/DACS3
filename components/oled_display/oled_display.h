#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdint.h>

typedef struct
{
    float temperature;   // Temperature in °C
    float humidity;      // Humidity in %
    float soil_moisture; // Soil moisture % (not displayed on 128x32)
    float light_level;   // Light level (not displayed on 128x32)
    int rain_detected;   // Rain status (not displayed on 128x32)
    char time_str[20];   // Time string HH:MM:SS AM/PM
} sensor_data_t;

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
