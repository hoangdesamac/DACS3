#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"

// Gọi các Manager (Module)
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "espnow_manager.h"

static const char *TAG = "MAIN_APP";

void app_main(void) {
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "HỆ THỐNG GATEWAY ESP32 ĐANG KHỞI ĐỘNG...");
    ESP_LOGI(TAG, "=========================================");

    // 1. Khởi tạo WiFi
    wifi_init_sta();
    
    // 2. Chờ cấp IP (Giữ nguyên 2.5s theo ý bạn)
    vTaskDelay(2500 / portTICK_PERIOD_MS); 
    
    // 3. In địa chỉ MAC để khai báo cho mạch Node
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "ĐỊA CHỈ MAC CỦA GATEWAY: %02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "=========================================");
    
    // 4. Khởi tạo ESP-NOW
    init_s3_espnow(); 
    
    // 5. Khởi tạo MQTT (Hàm này giờ sẽ tự động gọi gateway_hardware_init() để setup GPIO 2 và GPIO 4)
    mqtt_app_start();
    
    // 6. Vòng lặp chính: Định kỳ yêu cầu mạch Node gửi dữ liệu cảm biến
    while (1) {
        request_data_from_node();
        vTaskDelay(5000 / portTICK_PERIOD_MS); 
    }
}