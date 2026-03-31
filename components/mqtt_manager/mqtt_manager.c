#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt_manager.h"
#include "cJSON.h" 
#include "driver/gpio.h"
#include "espnow_manager.h" 

static const char *TAG = "MQTT_MANAGER";
#define GPIO_PIN_RELAY 2 // Đồng nhất với chân bên main.c

esp_mqtt_client_handle_t global_mqtt_client = NULL;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "=> ĐÃ KẾT NỐI THÀNH CÔNG VỚI HIVEMQ BROKER!");
            esp_mqtt_client_subscribe(client, "DACS3/app_to_esp32", 0);
            ESP_LOGI(TAG, "Đã đăng ký hóng tin nhắn ở Topic: DACS3/app_to_esp32");
            
            char *msg = "{\"id\": \"esp32_startup\", \"isOnline\": true}";
            esp_mqtt_client_publish(client, "DACS3/esp32_to_app", msg, 0, 1, 0);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Đã mất kết nối với Broker, đang tự động thử lại...");
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "=== CÓ TIN NHẮN MỚI TỪ APP ===");
            if (strncmp(event->topic, "DACS3/app_to_esp32", event->topic_len) == 0) {
                char payload[256] = {0}; 
                int len = event->data_len < (sizeof(payload)-1) ? event->data_len : (sizeof(payload)-1);
                snprintf(payload, len + 1, "%.*s", len, event->data);
                
                ESP_LOGI(TAG, "Nội dung nhận được: %s", payload);

                cJSON *json = cJSON_Parse(payload);
                if (json != NULL) {
                    cJSON *id_item = cJSON_GetObjectItem(json, "id");
                    cJSON *cmd_item = cJSON_GetObjectItem(json, "command");

                    if (cJSON_IsString(id_item) && cJSON_IsString(cmd_item)) {
                        const char *device_id = id_item->valuestring; 
                        const char *command = cmd_item->valuestring;  
                        char response_msg[256]; 

                        if (strcmp(command, "ON") == 0) {
                            ESP_LOGW(TAG, ">>> LỆNH: BẬT THIẾT BỊ ! ID: %s <<<", device_id);
                            gpio_set_level(GPIO_PIN_RELAY, 1);
                            vTaskDelay(100 / portTICK_PERIOD_MS); 
                            snprintf(response_msg, sizeof(response_msg), 
                                     "{\"id\": \"%s\", \"isOnline\": true, \"state\": \"ON\"}", device_id);
                            esp_mqtt_client_publish(client, "DACS3/esp32_to_app", response_msg, 0, 1, 0);
                            
                        } else if (strcmp(command, "OFF") == 0) {
                            ESP_LOGW(TAG, ">>> LỆNH: TẮT THIẾT BỊ ! ID: %s <<<", device_id);
                            gpio_set_level(GPIO_PIN_RELAY, 0);
                            vTaskDelay(100 / portTICK_PERIOD_MS); 
                            snprintf(response_msg, sizeof(response_msg), 
                                     "{\"id\": \"%s\", \"isOnline\": true, \"state\": \"OFF\"}", device_id);
                            esp_mqtt_client_publish(client, "DACS3/esp32_to_app", response_msg, 0, 1, 0);
                        } else {
                            ESP_LOGE(TAG, "Không nhận diện được lệnh command: %s", command);
                        }
                    } else {
                        ESP_LOGE(TAG, "Dữ liệu JSON bị thiếu 'id' hoặc 'command'!");
                    }
                    cJSON_Delete(json); 
                } else {
                    ESP_LOGE(TAG, "Parse JSON thất bại! Kiểm tra lại định dạng App gửi xuống.");
                }
            }
            ESP_LOGI(TAG, "=======================");
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Lỗi MQTT!");
            break;
            
        default:
            break;
    }
}

void mqtt_app_start(void) {
    ESP_LOGI(TAG, "Khởi động MQTT Client...");
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.hivemq.com",
    };

    global_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(global_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(global_mqtt_client);
}

// ==============================================================================
// ĐÃ SỬA: NHẬN CON TRỎ VOID TỪ HÀM KÊU GỌI ĐỂ TRÁNH INCLUDE CHÉO
// ==============================================================================
void mqtt_manager_publish_sensor_data(const void *data_ptr) {
    if (global_mqtt_client == NULL || data_ptr == NULL) {
        ESP_LOGW(TAG, "MQTT chưa sẵn sàng hoặc dữ liệu rỗng!");
        return;
    }

    // BƯỚC QUAN TRỌNG: Ép kiểu (cast) con trỏ void về lại đúng struct của nó
    const sensor_data_t *data = (const sensor_data_t *)data_ptr;

    // 1. Tạo đối tượng JSON
    cJSON *root = cJSON_CreateObject();
    
    // Gắn ID thiết bị
    cJSON_AddStringToObject(root, "id", "device_002"); 
    
    // Đọc trạng thái chân Relay hiện tại
    int relay_state = gpio_get_level(GPIO_PIN_RELAY);
    cJSON_AddStringToObject(root, "state", relay_state ? "ON" : "OFF");

    // Đẩy thông số cảm biến vào JSON
    cJSON_AddNumberToObject(root, "temp", data->temperature);
    cJSON_AddNumberToObject(root, "hum", data->humidity);
    cJSON_AddNumberToObject(root, "soil", data->soil_moisture);
    cJSON_AddNumberToObject(root, "light", data->light_level);
    cJSON_AddNumberToObject(root, "rain", data->rain_detected);

    // 2. Chuyển JSON thành chuỗi ký tự (String)
    char *json_string = cJSON_PrintUnformatted(root);
    
    if (json_string != NULL) {
        ESP_LOGI(TAG, "🚀 Chuẩn bị bắn lên HiveMQ: %s", json_string);
        // 3. Publish lên Topic
        esp_mqtt_client_publish(global_mqtt_client, "DACS3/esp32_to_app", json_string, 0, 1, 0);
        
        // 4. Dọn rác bộ nhớ
        cJSON_free(json_string); 
    }
    cJSON_Delete(root);
}