#ifndef ESPNOW_MANAGER_H
#define ESPNOW_MANAGER_H

#include <stdint.h>

// ============================================================================
// CẤU TRÚC DỮ LIỆU GIAO TIẾP
// ============================================================================
#pragma pack(push, 1)
typedef struct {
    float temperature;     // 4 bytes
    float humidity;        // 4 bytes
    float soil_moisture;   // 4 bytes
    float light_level;     // 4 bytes
    uint8_t rain_detected; // 1 byte
} sensor_data_t;
#pragma pack(pop)

// ============================================================================
// KHAI BÁO CÁC HÀM GIAO TIẾP VỚI MAIN.C
// ============================================================================
void init_s3_espnow(void);
void request_data_from_node(void);

#endif /* ESPNOW_MANAGER_H */