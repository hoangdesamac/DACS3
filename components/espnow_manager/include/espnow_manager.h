#ifndef ESPNOW_MANAGER_H
#define ESPNOW_MANAGER_H

#include <stdint.h>

// ============================================================================
// CẤU TRÚC DỮ LIỆU GIAO TIẾP (BẮT BUỘC PHẢI GIỐNG HỆT SENSOR.H BÊN NODE)
// ============================================================================
#pragma pack(push, 1)
typedef struct {
    char time_str[16];     // Giữ nguyên để khớp byte với Node
    float temperature;
    float humidity;
    float light_level;     // Đưa light_level lên trên để đúng thứ tự với Node
    float soil_moisture;
    uint8_t rain_detected;
} sensor_data_t;
#pragma pack(pop)

// ============================================================================
// KHAI BÁO CÁC HÀM GIAO TIẾP VỚI MAIN.C
// ============================================================================
void init_s3_espnow(void);
void request_data_from_node(void);

#endif /* ESPNOW_MANAGER_H */