#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt_manager.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "espnow_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MQTT_MANAGER";

// ========== CẤU HÌNH PIN TẠI GATEWAY ==========
#define GPIO_PIN_RELAY 2   // Relay phụ (Thiết bị 001)
#define GPIO_PIN_LED   4   // ĐÈN LED CHÍNH (Điều khiển trực tiếp)

esp_mqtt_client_handle_t global_mqtt_client = NULL;
bool is_mqtt_connected = false;

// Biến lưu trạng thái để đồng bộ với App
static bool current_fan_state = false;
static bool current_led_state = false;  
static bool current_dryer_on = false;   
static bool current_dryer_auto = false; 

// ================= HÀM KHỞI TẠO PHẦN CỨNG TẠI CHỖ =================
static void gateway_hardware_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_PIN_RELAY) | (1ULL << GPIO_PIN_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Đảm bảo lúc mới bật nguồn đèn và relay đều TẮT (Mức 0 cho Active High)
    gpio_set_level(GPIO_PIN_RELAY, 0);
    gpio_set_level(GPIO_PIN_LED, 0);
    
    ESP_LOGI(TAG, "Hardware Init: GPIO 2 (Relay) và GPIO 4 (LED) đã sẵn sàng.");
}

// ================= HÀM GỬI MQTT AN TOÀN (CÓ KIỂM TRA TRẠNG THÁI) =================
static void safe_mqtt_publish(const char *topic, const char *data)
{
    if (global_mqtt_client == NULL) {
        ESP_LOGE(TAG, "LỖI: Client MQTT chưa được khởi tạo!");
        return;
    }

    if (!is_mqtt_connected) {
        ESP_LOGW(TAG, "MQTT ĐANG NGOẠI TUYẾN: Bản tin bị hủy hoặc đang chờ kết nối lại...");
        return;
    }

    // Gửi với QoS 1 để đảm bảo tin nhắn đến được Broker
    int msg_id = esp_mqtt_client_publish(global_mqtt_client, topic, data, 0, 1, 0);
    
    if (msg_id != -1) {
        ESP_LOGI(TAG, "Đã đẩy bản tin vào hàng đợi thành công (ID: %d)", msg_id);
    } else {
        ESP_LOGE(TAG, "LỖI: Không thể gửi bản tin lên Broker!");
    }
}

// ================= HÀM PHẢN HỒI MQTT CHO APP =================
static void mqtt_publish_ack_custom(const char *device_id, const char *state)
{
    char response_msg[256];
    
    // So sánh chuỗi: Nếu state là "ON" thì gán is_on = true, ngược lại là false
    bool is_on = (strcmp(state, "ON") == 0);

    // Gắn trạng thái is_on vào thẳng chữ isOnline để App Android không bị nhầm lẫn nữa
    snprintf(response_msg, sizeof(response_msg),
             "{\"id\": \"%s\", \"isOnline\": %s, \"state\": \"%s\"}",
             device_id, is_on ? "true" : "false", state);
             
    safe_mqtt_publish("DACS3/esp32_to_app", response_msg);
    ESP_LOGI(TAG, "Sent ACK -> App: %s is %s", device_id, state);
}

static void mqtt_publish_ack(const char *device_id, bool is_on)
{
    mqtt_publish_ack_custom(device_id, is_on ? "ON" : "OFF");
}

void mqtt_manager_publish_node_status(bool is_online)
{
    char msg[128];
    snprintf(msg, sizeof(msg), "{\"id\": \"Esp32_Node_DACS3\", \"isOnline\": %s}", is_online ? "true" : "false");
    safe_mqtt_publish("DACS3/esp32_to_app", msg);
}

// ================= XỬ LÝ SỰ KIỆN MQTT =================
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        is_mqtt_connected = true;
        ESP_LOGI(TAG, "=> ĐÃ KẾT NỐI BROKER EMQX ✅");
        esp_mqtt_client_subscribe(client, "DACS3/app_to_esp32", 0);
        
        // Báo trạng thái Gateway Online
        safe_mqtt_publish("DACS3/esp32_to_app", "{\"id\": \"esp32_startup\", \"isOnline\": true}");
        break;

    case MQTT_EVENT_DISCONNECTED:
        is_mqtt_connected = false;
        ESP_LOGW(TAG, "=> MẤT KẾT NỐI BROKER ❌ (Đang tự động thử kết nối lại...)");
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "Broker đã xác nhận nhận được tin nhắn ID: %d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
    {
        if (strncmp(event->topic, "DACS3/app_to_esp32", event->topic_len) != 0) break;

        char payload[256] = {0};
        snprintf(payload, sizeof(payload), "%.*s", event->data_len, event->data);
        ESP_LOGI(TAG, "Payload: %s", payload);

        cJSON *json = cJSON_Parse(payload);
        if (!json) break;

        cJSON *id_item = cJSON_GetObjectItem(json, "id");
        cJSON *cmd_item = cJSON_GetObjectItem(json, "command");

        if (cJSON_IsString(id_item) && cJSON_IsString(cmd_item))
        {
            const char *device_id = id_item->valuestring;
            const char *command = cmd_item->valuestring;

            // --- LỆNH ON / OFF ---
            if (strcmp(command, "ON") == 0 || strcmp(command, "OFF") == 0)
            {
                const bool is_on = (strcmp(command, "ON") == 0);

                // 1. ĐÈN LED (Điều khiển trực tiếp tại Gateway - GPIO 4)
                if (strcmp(device_id, "device_led") == 0)
                {
                    current_led_state = is_on;
                    gpio_set_level(GPIO_PIN_LED, is_on ? 1 : 0); // Active High
                    ESP_LOGW(TAG, "LOCAL CONTROL: LED (GPIO4) -> %s", is_on ? "ON" : "OFF");
                    mqtt_publish_ack("device_led", is_on);
                }
                // 2. GIÀN PHƠI (Điều khiển từ xa qua Node - ESP-NOW)
                else if (strcmp(device_id, "device_dryer") == 0)
                {
                    current_dryer_on = is_on;
                    current_dryer_auto = false;
                    espnow_send_text_to_node(is_on ? "CMD:FORWARD" : "CMD:REVERSE");
                    mqtt_publish_ack(device_id, is_on);
                }
                // 3. QUẠT (Điều khiển từ xa qua Node - ESP-NOW)
                else if (strcmp(device_id, "device_fan") == 0)
                {
                    current_fan_state = is_on;
                    espnow_send_text_to_node(is_on ? "CMD:FAN_ON" : "CMD:FAN_OFF");
                    mqtt_publish_ack("device_fan", is_on);
                }
                // 4. RELAY PHỤ TẠI CHỖ (GPIO 2)
                else if (strcmp(device_id, "device_001") == 0)
                {
                    gpio_set_level(GPIO_PIN_RELAY, is_on ? 1 : 0);
                    mqtt_publish_ack(device_id, is_on);
                }
            }
            // --- LỆNH AUTO ---
            else if (strcmp(command, "AUTO") == 0)
            {
                if (strcmp(device_id, "device_dryer") == 0)
                {
                    current_dryer_auto = true;
                    espnow_send_text_to_node("CMD:AUTO");
                    mqtt_publish_ack_custom(device_id, "AUTO");
                }
            }
            // --- LỆNH PING (ĐỒNG BỘ TRẠNG THÁI) ---
            else if (strcmp(command, "PING") == 0) 
            {
                if (strcmp(device_id, "device_led") == 0) {
                    mqtt_publish_ack("device_led", current_led_state);
                } 
                else if (strcmp(device_id, "device_dryer") == 0) {
                    if (current_dryer_auto) mqtt_publish_ack_custom(device_id, "AUTO");
                    else mqtt_publish_ack(device_id, current_dryer_on);
                } 
                else if (strcmp(device_id, "device_fan") == 0) {
                    mqtt_publish_ack("device_fan", current_fan_state);
                }
                else if (strcmp(device_id, "Esp32_Node_DACS3") == 0) {
                    request_data_from_node(); 
                }
                else {
                    mqtt_publish_ack_custom(device_id, "OFF");
                }
            }
        }
        cJSON_Delete(json);
        break;
    }
    default:
        break;
    }
}

void mqtt_app_start(void)
{
    // Khởi tạo phần cứng trước khi chạy MQTT
    gateway_hardware_init();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = "broker.emqx.io",
        .broker.address.port = 1883,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.client_id = "Gateway_DACS3_VietNam_New",
    };

    global_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(global_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(global_mqtt_client);
}

void mqtt_manager_publish_sensor_data(const void *data_ptr)
{
    // Loại bỏ check is_mqtt_connected tại đây vì safe_mqtt_publish đã lo liệu
    if (data_ptr == NULL) return;

    const sensor_data_t *data = (const sensor_data_t *)data_ptr;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", "Esp32_Node_DACS3");
    cJSON_AddNumberToObject(root, "temp", data->temperature);
    cJSON_AddNumberToObject(root, "hum", data->humidity);
    cJSON_AddNumberToObject(root, "soil", data->soil_moisture);
    cJSON_AddNumberToObject(root, "light", data->light_level);
    cJSON_AddNumberToObject(root, "rain", data->rain_detected);

    char *json_string = cJSON_PrintUnformatted(root);
    if (json_string) {
        safe_mqtt_publish("DACS3/esp32_to_app", json_string);
        cJSON_free(json_string);
    }
    cJSON_Delete(root);
}