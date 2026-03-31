#include "espnow_manager.h"
#include <string.h>
#include <stdio.h>
#include "esp_now.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_manager.h" // Gọi hàm MQTT thoải mái

static const char *TAG = "S3_ESPNOW";
static uint8_t node_mac[] = {0xB0, 0xCB, 0xD8, 0x8A, 0x82, 0xA0};

static void s3_on_mac_send(const esp_now_send_info_t *info, esp_now_send_status_t status) {
    const uint8_t *mac_addr = info->des_addr;
    ESP_LOGI(TAG, "Gửi lệnh tới Node %02X:%02X... -> %s", 
             mac_addr[0], mac_addr[1], 
             status == ESP_NOW_SEND_SUCCESS ? "Thành công ✅" : "Thất bại ❌");
}

static void s3_on_mac_recv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len) {
    const uint8_t *mac_addr = esp_now_info->src_addr;
    
    if (data_len >= 16) {
        sensor_data_t recv_data = {0}; 
        int copy_len = (data_len < sizeof(sensor_data_t)) ? data_len : sizeof(sensor_data_t);
        memcpy(&recv_data, data, copy_len);
        
        ESP_LOGI(TAG, "✅ ĐÃ NHẬN GÓI TIN %d BYTES TỪ NODE %02X:%02X:", data_len, mac_addr[4], mac_addr[5]);
        ESP_LOGI(TAG, "   🌡️ Nhiệt độ: %.1f °C", recv_data.temperature);
        ESP_LOGI(TAG, "   💧 Độ ẩm: %.1f %%", recv_data.humidity);
        ESP_LOGI(TAG, "   🌱 Độ ẩm đất: %.1f %%", recv_data.soil_moisture);
        ESP_LOGI(TAG, "   ☀️ Ánh sáng: %.1f %%", recv_data.light_level);
        ESP_LOGI(TAG, "   🌧️ Trạng thái mưa: %d", recv_data.rain_detected);
        ESP_LOGI(TAG, "--------------------------------------------------");

        // Bắn lên MQTT Cloud (truyền địa chỉ biến, hàm bên kia sẽ tự ép sang void*)
        mqtt_manager_publish_sensor_data(&recv_data);

    } else {
        ESP_LOGW(TAG, "⚠️ Gói tin quá nhỏ (%d bytes), không đủ chứa dữ liệu cảm biến!", data_len);
    }
}

void init_s3_espnow(void) {
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "Lỗi khởi tạo ESP-NOW!");
        return;
    }
    
    esp_now_register_send_cb(s3_on_mac_send);
    esp_now_register_recv_cb(s3_on_mac_recv);

    esp_now_peer_info_t peerInfo = {0};
    memcpy(peerInfo.peer_addr, node_mac, 6);
    peerInfo.channel = 0; 
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        ESP_LOGI(TAG, "Đã khóa mục tiêu ESP-Node: B0:CB:D8:8A:82:A0");
    } else {
        ESP_LOGE(TAG, "Lỗi thêm ESP-Node vào danh sách Peer!");
    }
}

void request_data_from_node(void) {
    const char *command = "GET_DATA"; 
    esp_err_t result = esp_now_send(node_mac, (const uint8_t *)command, strlen(command));
    
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Đã hét: 'Đưa dữ liệu đây!' (%s)", command);
    } else {
        ESP_LOGE(TAG, "Lỗi phát lệnh! Mã lỗi: %d", result);
    }
}