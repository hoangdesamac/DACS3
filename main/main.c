#include "esp_rom_gpio.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"

// Gọi các Manager (Module)
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "espnow_manager.h"

static const char *TAG = "MAIN_APP";
#define GPIO_PIN_RELAY 2

void app_main(void) {
    ESP_LOGI(TAG, "Hệ thống đang khởi động...");

    // Khởi tạo GPIO
    gpio_reset_pin(GPIO_PIN_RELAY);
    gpio_set_direction(GPIO_PIN_RELAY, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_PIN_RELAY, 0);

    // 1. Khởi tạo WiFi
    wifi_init_sta();
    vTaskDelay(10000 / portTICK_PERIOD_MS); // Chờ WiFi bắt sóng để lấy Channel
    
    // In địa chỉ MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "ĐỊA CHỈ MAC S3: %02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "=========================================");
    
    // 2. Khởi tạo ESP-NOW (Chạy sau WiFi)
    init_s3_espnow();

    // 3. Khởi tạo MQTT
    mqtt_app_start();
    
    // Vòng lặp chính
    while (1) {
        // Yêu cầu ESP-Node gửi dữ liệu
        request_data_from_node();
        vTaskDelay(5000 / portTICK_PERIOD_MS); // 5 giây gửi 1 lệnh
    }
}