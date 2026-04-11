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

    // 1. FIX LỖI GPIO: Đổi thành INPUT_OUTPUT để đọc lại được trạng thái
    gpio_reset_pin(GPIO_PIN_RELAY);
    gpio_set_direction(GPIO_PIN_RELAY, GPIO_MODE_INPUT_OUTPUT); // <--- QUAN TRỌNG
    gpio_set_level(GPIO_PIN_RELAY, 0);

    // 2. Khởi tạo WiFi
    wifi_init_sta();
    
    // 3. FIX LỖI CHỜ LÂU: Giảm từ 10s xuống 2.5s vì WiFi kết nối rất nhanh
    vTaskDelay(2500 / portTICK_PERIOD_MS); 
    
    // In địa chỉ MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "ĐỊA CHỈ MAC S3: %02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "=========================================");
    
    // Khởi tạo ESP-NOW và MQTT
    init_s3_espnow();
    mqtt_app_start();
    
    // Vòng lặp chính
    while (1) {
        request_data_from_node();
        vTaskDelay(5000 / portTICK_PERIOD_MS); 
    }
}