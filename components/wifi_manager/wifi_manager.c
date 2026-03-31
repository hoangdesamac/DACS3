#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_manager.h" // Nhúng chính header của nó

// --- 1. CẤU HÌNH MẠNG CHÍNH ---
#define WIFI_SSID_1      "love her"
#define WIFI_PASS_1      "123456789"

// --- 2. CẤU HÌNH MẠNG PHỤ (DỰ PHÒNG) ---
#define WIFI_SSID_2      "RedmiNote13"
#define WIFI_PASS_2      "11223344"

#define MAXIMUM_RETRY    5

static const char *TAG = "WIFI_MANAGER";
static int s_retry_num = 0;
static int current_network = 1; // Biến theo dõi: 1 là mạng chính, 2 là mạng phụ

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Đang kết nối mạng CHÍNH: %s", WIFI_SSID_1);
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Đang thử kết nối lại mạng %d... (Lần %d/%d)", current_network, s_retry_num, MAXIMUM_RETRY);
        } else {
            // Đã hết số lần thử. Kiểm tra xem đang ở mạng nào?
            if (current_network == 1) {
                ESP_LOGE(TAG, "Mạng CHÍNH thất bại! Chuyển sang mạng PHỤ: %s", WIFI_SSID_2);
                
                // Đổi trạng thái sang mạng 2 và reset bộ đếm
                current_network = 2; 
                s_retry_num = 0;     
                
                // Cấu hình lại WiFi bằng thông tin mạng 2
                wifi_config_t wifi_config = {
                    .sta = {
                        .ssid = WIFI_SSID_2,
                        .password = WIFI_PASS_2,
                        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                    },
                };
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                esp_wifi_connect(); // Thử kết nối mạng 2
            } else {
                // Nếu current_network == 2 mà vẫn thất bại thì bó tay
                ESP_LOGE(TAG, "KẾT NỐI CẢ 2 MẠNG WIFI ĐỀU THẤT BẠI HOÀN TOÀN!");
            }
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        // In ra tên mạng đang kết nối thành công cho ngầu
        ESP_LOGI(TAG, "=> ĐÃ BẮT ĐƯỢC WIFI (%s)! IP: " IPSTR, 
                 (current_network == 1) ? WIFI_SSID_1 : WIFI_SSID_2, 
                 IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
    }
}

void wifi_init_sta(void) {
    // Khởi tạo NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   KÍCH HOẠT MODULE WIFI MANAGER        ");
    ESP_LOGI(TAG, "========================================");

    // Cấu hình WiFi ban đầu (Dùng mạng số 1)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID_1,
            .password = WIFI_PASS_1,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}