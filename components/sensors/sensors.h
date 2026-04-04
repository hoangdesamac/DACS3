#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

// ĐÂY LÀ STRUCT CHUẨN DUY NHẤT SẼ ĐƯỢC DÙNG CẢ Ở NODE VÀ GATEWAY
// Dùng #pragma pack để tránh lỗi padding memory khi gửi qua ESP-NOW
#pragma pack(push, 1)
typedef struct
{
    char time_str[16];     // Phục vụ cho OLED (Gateway sẽ nhận nhưng không dùng)
    float temperature;
    float humidity;
    float light_level;
    float soil_moisture;
    uint8_t rain_detected; // Đồng bộ dùng uint8_t với Gateway
} sensor_data_t;
#pragma pack(pop)

int sensors_init(void);
void sensors_read(sensor_data_t *readings);
int dht11_read(float *temp, float *humidity);
int adc_read_light(float *level);
int rain_read(int *detected); // Có thể đổi thành uint8_t* nếu muốn chuẩn hóa hoàn toàn

#endif // SENSORS_H
