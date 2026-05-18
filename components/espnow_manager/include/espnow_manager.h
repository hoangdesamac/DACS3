#ifndef ESPNOW_MANAGER_H
#define ESPNOW_MANAGER_H

#include <stdint.h>
#include "esp_err.h"

// ============================================================================
// PAYLOAD MỚI TỪ NODE (PHẢI KHỚP 100% THỨ TỰ + KIỂU + PACK)
// ============================================================================
#pragma pack(push, 1)
typedef struct
{
    uint32_t seq;
    int64_t epoch_time;
    char time_str[16];

    float temperature;
    float humidity;
    float light_level;
    uint8_t rain_detected;

    int8_t light_raw;
    int8_t rain_raw;

    int8_t dht_status;
    int8_t light_status;
    int8_t rain_status;

    uint8_t hanger_mode;
    uint8_t motor_state;
    uint8_t motor_target;
    uint8_t limit_in;
    uint8_t limit_out;

    uint8_t state_fan_mist;
    uint8_t state_light;

    uint8_t wifi_connected;
    int8_t wifi_rssi;

    uint8_t sntp_synced;
    uint32_t uptime_ms;
    uint32_t free_heap;
    uint8_t reset_reason;
} espnow_node_payload_t;
#pragma pack(pop)

// ============================================================================
// STRUCT CŨ DÙNG CHO MQTT (GIỮ TƯƠNG THÍCH mqtt_manager.c HIỆN TẠI)
// ============================================================================
#pragma pack(push, 1)
typedef struct
{
    char time_str[16];
    float temperature;
    float humidity;
    float light_level;
    float soil_moisture;
    uint8_t rain_detected;
} sensor_data_t;
#pragma pack(pop)

// ============================================================================
// API
// ============================================================================
void init_s3_espnow(void);
void request_data_from_node(void);

/** Gửi chuỗi lệnh ESP-NOW tới Node (peer đã add trong init). */
esp_err_t espnow_send_text_to_node(const char *text);

#endif /* ESPNOW_MANAGER_H */