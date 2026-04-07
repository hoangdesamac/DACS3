#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct
{
    char time_str[16];
    float temperature;
    float humidity;
    float light_level;
    uint8_t rain_detected;
} sensor_data_t;
#pragma pack(pop)

int sensors_init(void);
void sensors_read(sensor_data_t *readings);
int dht11_read(float *temp, float *humidity);
int adc_read_light(float *level);
int rain_read(int *detected);
int light_read_raw(int *raw_level);
int rain_read_raw(int *raw_level);

#endif
