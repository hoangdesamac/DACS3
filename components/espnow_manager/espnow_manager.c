#include "espnow_manager.h"
#include <string.h>
#include <stdio.h>
#include "esp_now.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_manager.h"

static const char *TAG = "S3_ESPNOW";
static uint8_t node_mac[] = {0xB0, 0xCB, 0xD8, 0x8A, 0x82, 0xA0};

static bool is_expected_node(const uint8_t *mac)
{
    return (memcmp(mac, node_mac, 6) == 0);
}

static void s3_on_mac_send(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    const uint8_t *mac_addr = info->des_addr;
    
    // Báo cáo trực tiếp tình trạng kết nối lên MQTT
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "Gửi lệnh tới Node %02X:%02X... -> Thành công ✅", mac_addr[0], mac_addr[1]);
        mqtt_manager_publish_node_status(true); // Node còn sống
    } else {
        ESP_LOGE(TAG, "Gửi lệnh tới Node %02X:%02X... -> Thất bại ❌", mac_addr[0], mac_addr[1]);
        mqtt_manager_publish_node_status(false); // Node rớt mạng
    }
}

static void s3_on_mac_recv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    const uint8_t *mac_addr = esp_now_info->src_addr;
    const unsigned expected_len = (unsigned)sizeof(espnow_node_payload_t);

    if (!is_expected_node(mac_addr))
    {
        ESP_LOGW(TAG, "Bỏ qua gói từ MAC lạ %02X:%02X:%02X:%02X:%02X:%02X (len=%d)",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5], data_len);
        return;
    }

    if ((unsigned)data_len != expected_len)
    {
        ESP_LOGW(TAG, "⚠️ Sai kích thước payload từ Node: received=%d bytes, expected=%u bytes",
                 data_len, expected_len);
        return;
    }

    espnow_node_payload_t recv_payload = {0};
    memcpy(&recv_payload, data, sizeof(recv_payload));

    ESP_LOGI(TAG, "✅ RX %d bytes | Node %02X:%02X | seq=%lu | time=%s",
             data_len, mac_addr[4], mac_addr[5],
             (unsigned long)recv_payload.seq, recv_payload.time_str);

    ESP_LOGI(TAG, "   🌡️ Temp: %.1f °C | 💧 Hum: %.1f %% | ☀️ Light: %.1f %% | 🌧️ Rain: %u",
             recv_payload.temperature,
             recv_payload.humidity,
             recv_payload.light_level,
             recv_payload.rain_detected);

    ESP_LOGI(TAG, "   Raw[L:%d R:%d] Status[DHT:%d L:%d R:%d] WiFi[%u, RSSI:%d] Heap:%lu",
             recv_payload.light_raw,
             recv_payload.rain_raw,
             recv_payload.dht_status,
             recv_payload.light_status,
             recv_payload.rain_status,
             recv_payload.wifi_connected,
             recv_payload.wifi_rssi,
             (unsigned long)recv_payload.free_heap);

    sensor_data_t mqtt_data = {0};
    memcpy(mqtt_data.time_str, recv_payload.time_str, sizeof(mqtt_data.time_str));
    mqtt_data.temperature = recv_payload.temperature;
    mqtt_data.humidity = recv_payload.humidity;
    mqtt_data.light_level = recv_payload.light_level;
    mqtt_data.rain_detected = recv_payload.rain_detected;
    mqtt_data.soil_moisture = 0.0f;

    mqtt_manager_publish_sensor_data(&mqtt_data);
}

void init_s3_espnow(void)
{
    if (esp_now_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Lỗi khởi tạo ESP-NOW!");
        return;
    }

    ESP_LOGI(TAG, "Expect espnow_node_payload_t size = %u", (unsigned)sizeof(espnow_node_payload_t));

    esp_now_register_send_cb(s3_on_mac_send);
    esp_now_register_recv_cb(s3_on_mac_recv);

    esp_now_peer_info_t peerInfo = {0};
    memcpy(peerInfo.peer_addr, node_mac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) == ESP_OK)
    {
        ESP_LOGI(TAG, "Đã khóa mục tiêu ESP-Node: B0:CB:D8:8A:82:A0");
    }
    else
    {
        ESP_LOGE(TAG, "Lỗi thêm ESP-Node vào danh sách Peer!");
    }
}

void request_data_from_node(void)
{
    const char *command = "GET_DATA";
    esp_err_t result = esp_now_send(node_mac, (const uint8_t *)command, strlen(command));

    if (result == ESP_OK)
    {
        ESP_LOGI(TAG, "Đã hét: 'Đưa dữ liệu đây!' (%s)", command);
    }
    else
    {
        ESP_LOGE(TAG, "Lỗi phát lệnh! Mã lỗi: %d", result);
    }
}

esp_err_t espnow_send_text_to_node(const char *text)
{
    if (text == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    size_t len = strlen(text);
    if (len == 0 || len > ESP_NOW_MAX_DATA_LEN)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    return esp_now_send(node_mac, (const uint8_t *)text, len);
}