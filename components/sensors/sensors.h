#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

typedef struct
{
    float temperature;
    float humidity;
    float light_level;
    int rain_detected;
} sensor_readings_t;

/**
 * Initialize sensors (DHT11, GPIO for light and rain)
 */
int sensors_init(void);

/**
 * Read all sensor values
 */
void sensors_read(sensor_readings_t *readings);

/**
 * Read DHT11 temperature and humidity
 */
int dht11_read(float *temp, float *humidity);

/**
 * Read light level via GPIO
 */
int adc_read_light(float *level);

/**
 * Read rain sensor (GPIO digital input)
 */
int rain_read(int *detected);

#endif // SENSORS_H
