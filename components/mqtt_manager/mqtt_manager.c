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
#define GPIO_PIN_RELAY 2

esp_mqtt_client_handle_t global_mqtt_client = NULL;
bool is_mqtt_connected = false;

// Thêm biến để lưu trạng thái của Quạt (vì nó điều khiển qua ESP-NOW, mạch Gateway không đọc chân GPIO được)
static bool current_fan_state = false;
static bool current_dryer_on = false;   // true = Phơi (ON), false = Thu (OFF)
static bool current_dryer_auto = false; // true = Đang chế độ Tự động

static void mqtt_publish_ack_custom(
    esp_mqtt_client_handle_t client,
    const char *device_id,
    const char *state)
{
    if (client == NULL) return;
    char response_msg[256];
    snprintf(response_msg, sizeof(response_msg),
             "{\"id\": \"%s\", \"isOnline\": true, \"state\": \"%s\"}",
             device_id, state);
    esp_mqtt_client_publish(client, "DACS3/esp32_to_app", response_msg, 0, 1, 0);
    ESP_LOGI(TAG, "Sent ACK to App: ID=%s, State=%s", device_id, state);
}

static void mqtt_publish_ack(
    esp_mqtt_client_handle_t client,
    const char *device_id,
    bool is_on)
{
    char response_msg[256];
    snprintf(response_msg, sizeof(response_msg),
             "{\"id\": \"%s\", \"isOnline\": true, \"state\": \"%s\"}",
             device_id, is_on ? "ON" : "OFF");
    esp_mqtt_client_publish(client, "DACS3/esp32_to_app", response_msg, 0, 1, 0);
}

// ================= THÊM MỚI: HÀM BÁO TRẠNG THÁI NODE =================
void mqtt_manager_publish_node_status(bool is_online)
{
    if (!is_mqtt_connected || global_mqtt_client == NULL) return;
    char msg[128];
    // Đẩy trạng thái OFFLINE/ONLINE đích danh cho Node
    snprintf(msg, sizeof(msg), "{\"id\": \"Esp32_Node_DACS3\", \"isOnline\": %s}", is_online ? "true" : "false");
    esp_mqtt_client_publish(global_mqtt_client, "DACS3/esp32_to_app", msg, 0, 1, 0);
    ESP_LOGD(TAG, "Đã đẩy trạng thái Node: %s", is_online ? "ONLINE" : "OFFLINE");
}
// ======================================================================

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        is_mqtt_connected = true;
        ESP_LOGI(TAG, "=> ĐÃ KẾT NỐI THÀNH CÔNG VỚI EMQX BROKER!");
        esp_mqtt_client_subscribe(client, "DACS3/app_to_esp32", 0);
        ESP_LOGI(TAG, "Đã đăng ký hóng tin nhắn ở Topic: DACS3/app_to_esp32");

        char *msg = "{\"id\": \"esp32_startup\", \"isOnline\": true}";
        esp_mqtt_client_publish(client, "DACS3/esp32_to_app", msg, 0, 1, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        is_mqtt_connected = false;
        ESP_LOGW(TAG, "Đã mất kết nối với Broker, đang tự động thử lại...");
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "=== CÓ TIN NHẮN MỚI TỪ APP ===");
        if (strncmp(event->topic, "DACS3/app_to_esp32", event->topic_len) == 0)
        {
            char payload[256] = {0};
            int len = event->data_len < (sizeof(payload) - 1) ? event->data_len : (sizeof(payload) - 1);
            snprintf(payload, len + 1, "%.*s", len, event->data);

            ESP_LOGI(TAG, "Nội dung nhận được: %s", payload);

            cJSON *json = cJSON_Parse(payload);
            if (json != NULL)
            {
                cJSON *id_item = cJSON_GetObjectItem(json, "id");
                cJSON *cmd_item = cJSON_GetObjectItem(json, "command");

                if (cJSON_IsString(id_item) && cJSON_IsString(cmd_item))
                {
                    const char *device_id = id_item->valuestring;
                    const char *command = cmd_item->valuestring;

                    // 1. NẾU LÀ LỆNH ĐIỀU KHIỂN (ON / OFF)
                    if (strcmp(command, "ON") == 0 || strcmp(command, "OFF") == 0)
                    {
                        const bool is_on = (strcmp(command, "ON") == 0);

                        // Xử lý Giàn phơi (device_dryer)
                        if (strcmp(device_id, "device_dryer") == 0)
                        {
                            current_dryer_on = is_on;
                            current_dryer_auto = false; // Người dùng bấm nút -> Thoát chế độ Tự động
                            
                            const char *espnow_cmd = is_on ? "CMD:FORWARD" : "CMD:REVERSE";
                            ESP_LOGW(TAG, ">>> GIÀN PHƠI: Chuyển Manual -> %s <<<", is_on ? "PHƠI" : "THU");
                            
                            espnow_send_text_to_node(espnow_cmd);
                            mqtt_publish_ack_custom(client, device_id, is_on ? "ON" : "OFF");
                        }
                        // Xử lý Quạt (device_fan)
                        else if (strcmp(device_id, "device_fan") == 0)
                        {
                            current_fan_state = is_on;
                            const char *espnow_cmd = is_on ? "CMD:FAN_ON" : "CMD:FAN_OFF";
                            ESP_LOGW(TAG, ">>> QUẠT NODE: %s <<<", espnow_cmd);

                            espnow_send_text_to_node(espnow_cmd);
                            mqtt_publish_ack(client, "device_fan", is_on);
                        }
                        // Xử lý Relay tại chỗ (device_001)
                        else if (strcmp(device_id, "device_001") == 0)
                        {
                            ESP_LOGW(TAG, ">>> RELAY GATEWAY (GPIO%d) -> %s <<<", GPIO_PIN_RELAY, command);
                            gpio_set_level(GPIO_PIN_RELAY, is_on ? 1 : 0);
                            vTaskDelay(pdMS_TO_TICKS(50));
                            mqtt_publish_ack(client, device_id, is_on);
                        }
                        else {
                            ESP_LOGW(TAG, "Device ID không hỗ trợ điều khiển ON/OFF: %s", device_id);
                        }
                    }
                    // 2. NẾU LÀ LỆNH AUTO
                    else if (strcmp(command, "AUTO") == 0)
                    {
                        if (strcmp(device_id, "device_dryer") == 0)
                        {
                            current_dryer_auto = true;
                            ESP_LOGW(TAG, ">>> GIÀN PHƠI: Kích hoạt chế độ TỰ ĐỘNG (AUTO) <<<");
                            
                            espnow_send_text_to_node("CMD:AUTO");
                            mqtt_publish_ack_custom(client, device_id, "AUTO");
                        }
                        else {
                            ESP_LOGW(TAG, "Thiết bị này không hỗ trợ chế độ AUTO: %s", device_id);
                        }
                    }
                    // 3. NẾU LÀ LỆNH PING TỪ APP (Để đồng bộ và giữ kết nối)
                    else if (strcmp(command, "PING") == 0) 
                    {
                        ESP_LOGD(TAG, "Heartbeat PING received for: %s", device_id);
                        
                        if (strcmp(device_id, "device_dryer") == 0) {
                            if (current_dryer_auto) {
                                mqtt_publish_ack_custom(client, device_id, "AUTO");
                            } else {
                                mqtt_publish_ack(client, device_id, current_dryer_on);
                            }
                        } 
                        else if (strcmp(device_id, "device_fan") == 0) {
                            mqtt_publish_ack(client, "device_fan", current_fan_state);
                        } 
                        else if (strcmp(device_id, "device_001") == 0) {
                            bool relay_is_on = gpio_get_level(GPIO_PIN_RELAY) == 1;
                            mqtt_publish_ack(client, "device_001", relay_is_on);
                        } 
                        // ĐÃ CHỈNH SỬA THEO ĐỀ XUẤT CỦA BẠN: Gọi hàm request_data_from_node() thay vì gửi "PING_NODE"
                        else if (strcmp(device_id, "Esp32_Node_DACS3") == 0) {
                            ESP_LOGI(TAG, "App PING Node Cảm biến -> Gửi lệnh GET_DATA để kiểm tra!");
                            request_data_from_node(); 
                        }
                        else if (strcmp(device_id, "esp32_startup") == 0) {
                            mqtt_publish_ack(client, "esp32_startup", true);
                        } 
                        else {
                            mqtt_publish_ack_custom(client, device_id, "OFF"); 
                        }
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Không nhận diện được lệnh: %s", command);
                    }
                }
                cJSON_Delete(json);
            }
            else
            {
                ESP_LOGE(TAG, "Lỗi phân giải JSON payload!");
            }
        }
        ESP_LOGI(TAG, "=======================");
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT Event Error detected!");
        break;

    default:
        break;
    }
}

void mqtt_app_start(void)
{
    ESP_LOGI(TAG, "Khởi động MQTT Client...");

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = "broker.emqx.io",
        .broker.address.port = 1883,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.client_id = "Gateway_DACS3_S3_VietNam_9999",
        .network.timeout_ms = 10000,
    };

    global_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(global_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(global_mqtt_client);
}

void mqtt_manager_publish_sensor_data(const void *data_ptr)
{
    if (global_mqtt_client == NULL || data_ptr == NULL)
    {
        ESP_LOGW(TAG, "MQTT chưa sẵn sàng hoặc dữ liệu rỗng!");
        return;
    }

    if (!is_mqtt_connected)
    {
        ESP_LOGW(TAG, "Đang rớt mạng MQTT, tạm bỏ qua gói tin này để tránh dội bom (spam)!");
        return;
    }

    const sensor_data_t *data = (const sensor_data_t *)data_ptr;
    cJSON *root = cJSON_CreateObject();

    // Đích danh con Node để Backend ghi Telemetry
    cJSON_AddStringToObject(root, "id", "Esp32_Node_DACS3");
    cJSON_AddNumberToObject(root, "temp", data->temperature);
    cJSON_AddNumberToObject(root, "hum", data->humidity);
    cJSON_AddNumberToObject(root, "soil", data->soil_moisture);
    cJSON_AddNumberToObject(root, "light", data->light_level);
    cJSON_AddNumberToObject(root, "rain", data->rain_detected);

    char *json_string = cJSON_PrintUnformatted(root);

    if (json_string != NULL)
    {
        ESP_LOGI(TAG, "🚀 Chuẩn bị bắn lên EMQX: %s", json_string);
        esp_mqtt_client_publish(global_mqtt_client, "DACS3/esp32_to_app", json_string, 0, 1, 0);
        cJSON_free(json_string);
    }
    cJSON_Delete(root);
}