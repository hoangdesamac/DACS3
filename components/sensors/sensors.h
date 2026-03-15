#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

typedef struct
{
    float temperature;
    float humidity;
    float soil_moisture;
    float light_level;
    int rain_detected;
} sensor_readings_t;

/**
 * Initialize sensors (DHT22, ADC for soil/light, GPIO for rain)
 */
int sensors_init(void);

/**
 * Read all sensor values
 */
void sensors_read(sensor_readings_t *readings);

/**
 * Read DHT22 temperature and humidity
 */
int dht22_read(float *temp, float *humidity);

/**
 * Read analog sensors via ADC
 */
int adc_read_soil(float *percentage);
int adc_read_light(float *level);

/**
 * Read rain sensor (GPIO digital input)
 */
int rain_read(int *detected);

#endif // SENSORS_H
