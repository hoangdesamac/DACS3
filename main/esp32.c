/**
 * DACS3 Main Application
    }
 *   - Reads multiple sensors (temperature, humidity, light, rain)
    // ===== State Machine =====
 *   - Displays readings on SSD1306 OLED display in real-time
 *   - Sends sensor data via ESP-NOW wireless protocol
 *   - Syncs time via SNTP (NTP)
 *
 * Task Architecture:
 *   - Main task: ESP-NOW initialization and periodic message sending
 *   - Sensor task: Reads all sensors every 2 seconds and updates display
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "espnow.h"
#include "oled_display.h"
#include "sensors.h"
#include "motor.h"
#include "driver/gpio.h"

static const char *TAG = "MAIN";

static volatile bool sntp_time_synced = false;

/* ========== WIFI CONFIGURATION ========== */
#define WIFI_SSID "love her"
#define WIFI_PASS "123456789"

/* Event group for WiFi events */
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

/* ========== CONFIGURATION ========== */
#define SENSOR_LIVE_LOG_INTERVAL_SEC 2
#define SENSOR_SUMMARY_LOG_INTERVAL_SEC 300
#define CLOTHES_TASK_LOG_INTERVAL_SEC 2

// Target device MAC address for ESP-NOW communication
// Change this to the MAC address of your receiving device
static uint8_t peer_mac[] = {0x3C, 0xDC, 0x75, 0x6E, 0x98, 0x2C};

/* ========== DEVICE PIN SETTINGS ========== */
#define PIN_LIGHT 4 // Chân điều khiển Relay Đèn

// Trạng thái các thiết bị (State lưu lại để theo dõi)
static bool state_fan_mist = false;
static bool state_light = false;

/* ========== SHARED DATA ========== */
// Global sensor data structure for display
// This is updated by the sensor task and read by main task for ESP-NOW sending
static sensor_data_t display_data = {0};
static uint32_t espnow_seq = 0;

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

typedef enum
{
    MODE_AUTO,
    MODE_MANUAL_FORWARD,
    MODE_MANUAL_REVERSE
} hanger_mode_t;
static volatile hanger_mode_t current_hanger_mode = MODE_AUTO; // Mặc định chạy Tự Động
static volatile motor_direction_t desired_motor_state = MOTOR_REVERSE;
// Latched limit switch states: once a limit is hit, remain latched until movement
// clears the switch (prevents accidental re-extension beyond limit)
static volatile uint8_t latched_limit_in = 0;
static volatile uint8_t latched_limit_out = 0;

/**
 * WiFi event handler
 * Handles WiFi connection/disconnection events and starts SNTP when connected
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "WiFi STA started, connecting to AP...");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi disconnected! Reason: %d, reconnecting...", disconnected->reason);
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * ESP-NOW message receive callback
 * Called when a message is received from another ESP device
 *
 * In a multi-device setup, this could receive sensor data from other nodes
 */
static void on_message(const uint8_t *src_mac, const uint8_t *data, int len)
{
    // Kiểm tra xem dữ liệu đến có phải là struct sensor_data_t không thông qua độ dài (29 byte)
    if (len == sizeof(sensor_data_t))
    {
        // Ép kiểu trực tiếp byte từ mạng thành struct
        sensor_data_t *incoming_data = (sensor_data_t *)data;

        ESP_LOGI(TAG, "[ESP-NOW RX] MAC: %02x:%02x:%02x:%02x:%02x:%02x | Temp: %.1fC | Hum: %.1f%% | Light: %.0f%% | Rain: %s",
                 src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5],
                 incoming_data->temperature,
                 incoming_data->humidity,
                 incoming_data->light_level,
                 incoming_data->rain_detected ? "YES" : "NO");
    }
    else
    {
        // Coi dữ liệu gửi tới là một chuỗi văn bản (String) thông thường
        char buf[256] = {0};
        memcpy(buf, data, len < 255 ? len : 255);

        // --- NHẬN LỆNH ĐIỀU KHIỂN TỪ XA ---
        if (strncmp(buf, "CMD:FORWARD", 11) == 0)
        {
            current_hanger_mode = MODE_MANUAL_FORWARD;
            ESP_LOGI(TAG, "[ESP-NOW RX] Bật CHẾ ĐỘ THỦ CÔNG: PHƠI ĐỒ (FORWARD)");
        }
        else if (strncmp(buf, "CMD:REVERSE", 11) == 0)
        {
            current_hanger_mode = MODE_MANUAL_REVERSE;
            ESP_LOGI(TAG, "[ESP-NOW RX] Bật CHẾ ĐỘ THỦ CÔNG: CẤT ĐỒ (REVERSE)");
        }
        else if (strncmp(buf, "CMD:AUTO", 8) == 0)
        {
            current_hanger_mode = MODE_AUTO;
            ESP_LOGI(TAG, "[ESP-NOW RX] Bật lại CHẾ ĐỘ TỰ ĐỘNG (Theo cảm biến)");
        }
        // ===== LỆNH CHO QUẠT & PHUN SƯƠNG =====
        else if (strncmp(buf, "CMD:FAN_ON", 10) == 0)
        {
            state_fan_mist = true;
            drv8833_ac_set_power(true); // Fan + Mist = 1 device
            ESP_LOGI(TAG, "[ESP-NOW RX] Lệnh: BẬT Quạt + Phun sương");
        }
        else if (strncmp(buf, "CMD:FAN_OFF", 11) == 0)
        {
            state_fan_mist = false;
            drv8833_ac_set_power(false); // Fan + Mist = 1 device
            ESP_LOGI(TAG, "[ESP-NOW RX] Lệnh: TẮT Quạt + Phun sương");
        }
        // ===== LỆNH CHO ĐÈN =====
        else if (strncmp(buf, "CMD:LIGHT_ON", 12) == 0)
        {
            state_light = true;
            gpio_set_level(PIN_LIGHT, 1);
            ESP_LOGI(TAG, "[ESP-NOW RX] Lệnh: BẬT Đèn");
        }
        else if (strncmp(buf, "CMD:LIGHT_OFF", 13) == 0)
        {
            state_light = false;
            gpio_set_level(PIN_LIGHT, 0);
            ESP_LOGI(TAG, "[ESP-NOW RX] Lệnh: TẮT Đèn");
        }
        // Không xác định được lệnh
        else
        {
            ESP_LOGI(TAG, "[ESP-NOW RX] MAC: %02x:%02x:%02x:%02x:%02x:%02x | Msg Không rõ lệnh: %s",
                     src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5],
                     buf);
        }
    }
}

static const char *motor_dir_to_str(motor_direction_t dir)
{
    switch (dir)
    {
    case MOTOR_FORWARD:
        return "FORWARD";
    case MOTOR_REVERSE:
        return "REVERSE";
    default:
        return "STOP";
    }
}

/**
 * SNTP (Simple Network Time Protocol) time sync notification callback
 * Called when system time is successfully synchronized with NTP server
 */
static void time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
    sntp_time_synced = true;
    ESP_LOGI(TAG, "Time synchronized with NTP server");
}

/**
 * Initialize and connect to WiFi
 *
 * WiFi connection is required for SNTP time synchronization
 * This function:
 *   1. Initializes WiFi in STA (Station) mode
 *   2. Registers event handlers for WiFi events
 *   3. Connects to the configured SSID/password
 *   4. Waits up to 30 seconds for connection
 */
static void initialize_wifi(void)
{
    ESP_LOGI(TAG, "Initializing WiFi...");

    // Create event group for WiFi events
    wifi_event_group = xEventGroupCreate();

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default WiFi STA interface
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers for WiFi and IP events
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Configure WiFi settings optimized for mobile hotspot compatibility
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .scan_method = WIFI_FAST_SCAN,
            .bssid_set = false,
            .channel = 0,         // Auto-detect channel
            .listen_interval = 3, // Reduced for mobile hotspot
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = -127,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = false,
                .required = false},
            .ft_enabled = false,
            .sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED, // Allow WPA2-only hotspots
            .sae_h2e_identifier = {0},
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Set WiFi to 802.11 b/g mode (more compatible with mobile hotspots)
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
    ESP_LOGI(TAG, "WiFi bandwidth set to HT20 (802.11 b/g mode)");

    // Set WiFi power save mode to none
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "WiFi power save disabled");

    ESP_LOGI(TAG, "Starting WiFi with SSID: %s", WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait for WiFi connection with watchdog resets
    ESP_LOGI(TAG, "Waiting for WiFi connection (timeout: 60 seconds)...");
    int timeout_ms = 60000;
    int elapsed_ms = 0;
    int log_count = 0;

    while (elapsed_ms < timeout_ms)
    {
        // Wait in 5-second chunks to allow watchdog resets
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                                               pdFALSE, pdTRUE, pdMS_TO_TICKS(5000));

        if (bits & WIFI_CONNECTED_BIT)
        {
            ESP_LOGI(TAG, "WiFi connection successful");
            return;
        }

        elapsed_ms += 5000;

        // Log only every 30 seconds (every 6 iterations) to reduce monitor spam
        if (++log_count % 6 == 0)
        {
            ESP_LOGI(TAG, "Still waiting for WiFi... (%d/%d ms)", elapsed_ms, timeout_ms);
        }
    }

    ESP_LOGW(TAG, "WiFi connection timeout after 60 seconds - stopping WiFi and using local time");
    // We should not stop WiFi here if we want to use ESP-NOW!
    // esp_wifi_stop();
    // esp_wifi_deinit();
}

/**
 * Initialize SNTP for time synchronization
 *
 * This allows the device to get the current time from a NTP server
 * Needed for displaying accurate timestamps on the OLED
 *
 * Uses pool.ntp.org as the NTP server
 */
static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP (Network Time Protocol)...");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

/**
 * Wait for SNTP to synchronize time with NTP server
 * Times out after 10 seconds with a warning
 *
 * This ensures we have accurate time before starting sensor display
 */
static void wait_for_time_sync(void)
{
    time_t now = time(NULL);
    struct tm timeinfo = *localtime(&now);
    int retry = 0;
    const int retry_count = 10;

    // Wait up to 10 seconds for time to be set
    // Check by seeing if year is reasonable (< 2016 means not synchronized)
    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count)
    {
        // Only log on first and last attempt to reduce spam
        if (retry == 1 || retry == retry_count - 1)
        {
            ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        }
        vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds, try again
        now = time(NULL);
        timeinfo = *localtime(&now);
    }

    if (timeinfo.tm_year < (2016 - 1900))
    {
        ESP_LOGW(TAG, "Time sync failed, will use local time");
    }
    else
    {
        sntp_time_synced = true;
        ESP_LOGI(TAG, "System time is synchronized");
    }
}

/**
 * Display task - reads and displays sensor data on OLED
 *
 * This FreeRTOS task:
 *   1. Reads DHT11 temperature and humidity from GPIO 14
 *   2. Reads light sensor status from LM393 comparator (GPIO 33)
 *   3. Reads rain sensor status from LM393 comparator (GPIO 32)
 *   4. Gets current time in Vietnam timezone (UTC+7)
 *   5. Displays all sensor data on SSD1306 OLED display
 *
 * Runs continuously, updating display every 2 seconds
 * Automatically repeats forever
 */
static void display_task(void *pvParameters)
{
    // Task loop - runs forever until deleted
    static int update_count = 0;

    while (1)
    {
        // Get current system time and convert to Vietnam timezone
        time_t now = time(NULL);
        struct tm *timeinfo = localtime(&now);

        bool time_ok = sntp_time_synced || (timeinfo->tm_year >= (2016 - 1900));

        if (time_ok)
        {
            // Format time as HH:MM:SS AM/PM (already in Vietnam timezone from setenv above)
            strftime(display_data.time_str, sizeof(display_data.time_str), "%I:%M:%S %p", timeinfo);
        }
        else
        {
            snprintf(display_data.time_str, sizeof(display_data.time_str), "Syncing...");
        }

        // ===== READ SENSOR VALUES =====
        // Read DHT11 temperature and humidity
        int dht_status = dht11_read(&display_data.temperature, &display_data.humidity);
        if (dht_status != 0)
        {
            // Failed to read DHT11, use dummy values
            display_data.temperature = 0.0f;
            display_data.humidity = 0.0f;
        }

        // Read LDR light level
        int light_status = adc_read_light(&display_data.light_level);
        if (light_status != 0)
        {
            display_data.light_level = 0.0f;
        }

        // Read rain sensor (Sử dụng biến tạm int để tránh lỗi con trỏ do struct dùng uint8_t)
        int temp_rain = 0;
        int rain_status = rain_read(&temp_rain);
        if (rain_status != 0)
        {
            display_data.rain_detected = 0;
        }
        else
        {
            display_data.rain_detected = (uint8_t)temp_rain;
        }

        int rain_raw = -1;
        int light_raw = -1;
        rain_read_raw(&rain_raw);
        light_read_raw(&light_raw);

        // Update OLED display
        oled_display_sensor_data(&display_data);

        wifi_ap_record_t ap_info;
        int8_t wifi_rssi = 0;
        uint8_t wifi_connected = 0;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
        {
            wifi_connected = 1;
            wifi_rssi = ap_info.rssi;
        }

        espnow_node_payload_t payload = {0};
        payload.seq = ++espnow_seq;
        payload.epoch_time = (int64_t)now;
        memcpy(payload.time_str, display_data.time_str, sizeof(payload.time_str));

        payload.temperature = display_data.temperature;
        payload.humidity = display_data.humidity;
        payload.light_level = display_data.light_level;
        payload.rain_detected = display_data.rain_detected;

        payload.light_raw = (int8_t)light_raw;
        payload.rain_raw = (int8_t)rain_raw;

        payload.dht_status = (int8_t)dht_status;
        payload.light_status = (int8_t)light_status;
        payload.rain_status = (int8_t)rain_status;

        payload.hanger_mode = (uint8_t)current_hanger_mode;
        payload.motor_state = (uint8_t)motor_get_direction();
        payload.motor_target = (uint8_t)desired_motor_state;
        // Report latched limit states (1 = reached and latched, 0 = clear)
        payload.limit_in = latched_limit_in;
        payload.limit_out = latched_limit_out;

        payload.state_fan_mist = state_fan_mist ? 1 : 0;
        payload.state_light = state_light ? 1 : 0;

        payload.wifi_connected = wifi_connected;
        payload.wifi_rssi = wifi_rssi;

        payload.sntp_synced = sntp_time_synced ? 1 : 0;
        payload.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        payload.free_heap = esp_get_free_heap_size();
        payload.reset_reason = (uint8_t)esp_reset_reason();

        espnow_send(peer_mac, (const uint8_t *)&payload, sizeof(payload));

        update_count++;

        // Monitor-friendly full sensor line for easier tracking
        if (update_count % SENSOR_LIVE_LOG_INTERVAL_SEC == 0)
        {
            ESP_LOGI(TAG, "[SENSOR] %s | Temp: %.1fC | Hum: %.1f%% | Light: %.0f%% | Rain: %s",
                     display_data.time_str,
                     display_data.temperature,
                     display_data.humidity,
                     display_data.light_level,
                     display_data.rain_detected ? "YES" : "NO");
        }

        // Full sensor summary at a longer interval
        if (update_count % SENSOR_SUMMARY_LOG_INTERVAL_SEC == 0)
        {
            ESP_LOGI(TAG, "[SUMMARY] Temp: %.1fC | Hum: %.1f%% | Light: %.0f%% | Rain: %s",
                     display_data.temperature,
                     display_data.humidity,
                     display_data.light_level,
                     display_data.rain_detected ? "YES" : "NO");
        }

        // Update display every 1 second to show live sensor updates
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * Automatic Clothes Hanger Control Task with Limit Switches
 *
 * Logic:
 *   - HANG OUT (FORWARD): Bright (light > 50%) AND NO RAIN
 *     * Runs motor forward until GPIO5 limit switch pressed (fully extended)
 *
 *   - PULL IN (REVERSE): Rainy OR Dark (light <= 50%)
 *     * Runs motor reverse until GPIO17 limit switch pressed (fully retracted)
 *
 *   - STOP: Limit switch reached, or conditions don't require change
 *
 * Safety features:
 *   - Motor stops when corresponding limit switch is pressed
 *   - Direct reaction every 1 second (no hysteresis delay)
 */
static void clothes_hanger_task(void *pvParameters)
{
    const TickType_t CONTROL_PERIOD = pdMS_TO_TICKS(10); // Chu kỳ cập nhật 10ms để phản hồi tức thì
    int log_counter = 0;

    // Simple immediate-response logic: PHƠI nếu ánh sáng lớn hơn ngưỡng và không mưa
    const float LIGHT_THRESHOLD = 50.0f;

    // ===== STATE =====
    motor_direction_t currentMotorState = MOTOR_STOP;
    desired_motor_state = MOTOR_REVERSE; // Default to pull in on boot

    ESP_LOGI(TAG, "Starting SMART automatic clothes hanger control task...");

    while (1)
    {
        // ===== Read Sensor =====
        float light_level = 0.0f;
        int rain_detected = 0;
        int light_raw = -1;
        int rain_raw = -1;

        adc_read_light(&light_level);
        rain_read(&rain_detected);
        light_read_raw(&light_raw);
        rain_read_raw(&rain_raw);

        // ===== Read Limit Switches =====
        int startPressed = motor_read_limit_switch_in(); // LIMIT_SWITCH_START (retracted)
        int endPressed = motor_read_limit_switch_out();  // LIMIT_SWITCH_END (extended)

        // Latch: once a limit is reached, remember it until movement clears the switch
        if (startPressed)
        {
            latched_limit_in = 1;
        }
        if (endPressed)
        {
            latched_limit_out = 1;
        }

        // ===== SMART Motor Direction Logic =====
        if (current_hanger_mode != MODE_AUTO)
        {
            // ---> ĐIỀU KHIỂN BẰNG TAY TỪ ESP KHÁC <---
            desired_motor_state = (current_hanger_mode == MODE_MANUAL_FORWARD) ? MOTOR_FORWARD : MOTOR_REVERSE;
        }
        else
        {
            // ---> CHẾ ĐỘ TỰ ĐỘNG BẰNG CẢM BIẾN <---
            // Immediate decision: phơi nếu đủ sáng và không mưa, ngược lại thu
            if (light_level > LIGHT_THRESHOLD && !rain_detected)
            {
                desired_motor_state = MOTOR_FORWARD;
            }
            else
            {
                desired_motor_state = MOTOR_REVERSE;
            }
        }

        // --- Safety: require stable desired state for several cycles before moving
        static motor_direction_t last_desired = MOTOR_STOP;
        static int stable_count = 0;
        const int REQUIRED_STABLE = 5; // number of CONTROL_PERIOD cycles

        if (desired_motor_state == last_desired)
        {
            stable_count++;
        }
        else
        {
            last_desired = desired_motor_state;
            stable_count = 0;
        }

        // Log sensor and switch state for first seconds to help debugging
        static int boot_log_count = 0;
        if (boot_log_count < 10)
        {
            ESP_LOGI(TAG, "BOOT-DBG sensors: light=%.1f rain=%d start=%d end=%d desired=%s",
                     light_level, rain_detected, startPressed, endPressed,
                     motor_dir_to_str(desired_motor_state));
            boot_log_count++;
        }

        // ===== State Machine =====
        switch (currentMotorState)
        {

        // --- 1: Motor Stopped ---
        case MOTOR_STOP:
            // Tách riêng điều kiện cho từng chiều để tránh bị kẹt
            if (desired_motor_state == MOTOR_FORWARD)
            {
                // Nếu trước đó đã latched ở START (đã chạm lúc thu), cho phép clear latch
                if (latched_limit_in)
                {
                    latched_limit_in = 0; // Clear only when user/sensors request FORWARD
                    ESP_LOGI(TAG, "Cleared latched_limit_in due to FORWARD request");
                }

                // Muốn đi TIẾN: Chỉ khi không bị latched ở END
                if (!latched_limit_out)
                {
                    motor_set_direction(MOTOR_FORWARD);
                    currentMotorState = MOTOR_FORWARD;
                    ESP_LOGI(TAG, "CLOTHES HANGER - Chuyển sang PHƠI DO (FORWARD)");
                }
            }
            else if (desired_motor_state == MOTOR_REVERSE)
            {
                // Nếu trước đó đã latched ở END (đã chạm lúc phơi), cho phép clear latch
                if (latched_limit_out)
                {
                    latched_limit_out = 0; // Clear only when user/sensors request REVERSE
                    ESP_LOGI(TAG, "Cleared latched_limit_out due to REVERSE request");
                }

                // Muốn đi LÙI: Chỉ khi không bị latched ở START
                if (!latched_limit_in)
                {
                    motor_set_direction(MOTOR_REVERSE);
                    currentMotorState = MOTOR_REVERSE;
                    ESP_LOGI(TAG, "CLOTHES HANGER - Chuyển sang KÉO VÀO (REVERSE)");
                }
            }
            break;

        // --- 2: Motor Forward (Phơi) ---
        case MOTOR_FORWARD:
            // Ưu tiên 1: Chạm công tắc hành trình thì dừng và latch
            if (endPressed)
            {
                latched_limit_out = 1;
                motor_set_direction(MOTOR_STOP);
                currentMotorState = MOTOR_STOP;
                ESP_LOGI(TAG, "HẠN CHẾ CUỐI - Dừng motor (Đã phơi xong) - latched_out=1");
            }
            // Ưu tiên 2: Cảm biến thay đổi ý định (trời mưa/tối) -> cho phép đổi chiều
            else if (desired_motor_state == MOTOR_REVERSE)
            {
                motor_set_direction(MOTOR_STOP);
                motor_set_direction(MOTOR_REVERSE);
                currentMotorState = MOTOR_REVERSE;
                ESP_LOGI(TAG, "CLOTHES HANGER - Đổi ý định: Chuyển sang KÉO VÀO (REVERSE)");
            }
            break;

        // --- 3: Motor Reverse (Thu) ---
        case MOTOR_REVERSE:
            // Ưu tiên 1: Chạm công tắc hành trình thì dừng và latch
            if (startPressed)
            {
                latched_limit_in = 1;
                motor_set_direction(MOTOR_STOP);
                currentMotorState = MOTOR_STOP;
                ESP_LOGI(TAG, "HẠN CHẾ ĐẦU - Dừng motor (Đã thu xong) - latched_in=1");
            }
            // Ưu tiên 2: Cảm biến thay đổi ý định (trời nắng lại) -> cho phép đổi chiều
            else if (desired_motor_state == MOTOR_FORWARD)
            {
                motor_set_direction(MOTOR_STOP);
                motor_set_direction(MOTOR_FORWARD);
                currentMotorState = MOTOR_FORWARD;
                ESP_LOGI(TAG, "CLOTHES HANGER - Đổi ý định: Chuyển sang PHƠI DO (FORWARD)");
            }
            break;

        default:
            motor_set_direction(MOTOR_STOP);
            currentMotorState = MOTOR_STOP;
            break;
        }

        // ===== HEARTBEAT LOG (Chu kỳ 2 giây = 200 * 10ms) =====
        if (++log_counter % 200 == 0)
        {
            ESP_LOGI(TAG, "[MOTOR] Current: %-7s | Target: %-7s | Light: %3.0f%% | Mưa: %s | Latched[In:%d, Out:%d]",
                     motor_dir_to_str(currentMotorState),
                     motor_dir_to_str(desired_motor_state),
                     light_level,
                     rain_detected ? "CÓ" : "KHÔNG",
                     latched_limit_in, latched_limit_out);
        }

        // NOTE: Do NOT auto-clear latches on physical release anymore.
        // Latches remain until an opposite-direction request clears them.
        vTaskDelay(CONTROL_PERIOD); // Delay 10ms
    }
}

// static void debug_fan_task(void *pvParameters)
// {
//     TaskHandle_t notify_task = (TaskHandle_t)pvParameters;
//     ESP_LOGI(TAG, "Starting DRV8833 boot self-test (10s): FAN 3s -> MIST 3s -> BOTH 4s");

//     // 1) FAN only (3s)
//     ESP_LOGI(TAG, "TEST1/3: FAN ON (GPIO25/26), MIST OFF (GPIO27/12)");
//     drv8833_fan_set_power(true);
//     drv8833_ultrasonic_set_power(false);
//     vTaskDelay(pdMS_TO_TICKS(3000));

//     // 2) MIST only (3s)
//     ESP_LOGI(TAG, "TEST2/3: MIST ON (GPIO27/12), FAN OFF (GPIO25/26)");
//     drv8833_fan_set_power(false);
//     drv8833_ultrasonic_set_power(true);
//     vTaskDelay(pdMS_TO_TICKS(3000));

//     // 3) BOTH (AC) (4s)
//     ESP_LOGI(TAG, "TEST3/3: AC ON (fan + mist)");
//     drv8833_ac_set_power(true);
//     vTaskDelay(pdMS_TO_TICKS(4000));

//     ESP_LOGI(TAG, "DRV8833 self-test: OFF (fan + mist)");
//     drv8833_ac_set_power(false);

//     ESP_LOGI(TAG, "DRV8833 boot self-test complete");
//     if (notify_task != NULL)
//     {
//         xTaskNotifyGive(notify_task);
//     }
//     vTaskDelete(NULL);
// }

/**
 * Build NVS flash...

 *
 * Initialization sequence:
 *   1. NVS flash initialization (needed for WiFi/BLE)
 *   2. OLED display initialization
 *   3. Sensor initialization
 *   4. Motor and limit switch initialization
 *   5. SNTP time synchronization
 *   6. ESP-NOW wireless protocol setup
 *   7. Create sensor reading task
 *   8. Create clothes hanger control task
 *
 * Note: All initialization must complete before main loops start
 */
void app_main(void)
{
    // ===== NVS FLASH INITIALIZATION =====
    // NVS (Non-Volatile Storage) is needed for WiFi credentials
    // Erase and reinitialize if version mismatch (can happen after OTA updates)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ===== OLED DISPLAY INITIALIZATION =====
    // Initialize I2C bus and SSD1306 display
    // This must happen before sensor task to show status
    ESP_LOGI(TAG, "Initializing OLED display...");
    vTaskDelay(pdMS_TO_TICKS(500)); // Give I2C time to stabilize
    if (oled_init() != 0)
    {
        ESP_LOGE(TAG, "OLED initialization FAILED - display will not work");
        // Continue anyway - other features might still work
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // ===== SENSOR INITIALIZATION =====
    // Initialize DHT11, LDR, and rain sensor
    ESP_LOGI(TAG, "Initializing sensors...");
    vTaskDelay(pdMS_TO_TICKS(500)); // Give GPIO/ADC time to initialize
    if (sensors_init() != 0)
    {
        ESP_LOGE(TAG, "Sensor initialization FAILED");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    // ===== MOTOR CONTROL INITIALIZATION =====
    // Initialize motor control on GPIO18 (IN1), GPIO19 (IN2)
    // Initialize limit switches on GPIO5 (OUT), GPIO17 (IN)
    ESP_LOGI(TAG, "Initializing motor control with limit switches...");
    if (motor_init() != 0)
    {
        ESP_LOGE(TAG, "Motor control initialization FAILED");
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // // ===== TẠO TASK DEBUG CHẠY QUẠT DRV8833 (TEST NGAY KHI KHỞI ĐỘNG) =====
    // // Chạy sớm để bạn quan sát quạt hoạt động trước khi WiFi/SNTP có thể làm chậm boot.
    // ESP_LOGI(TAG, "Creating debug fan task (boot test)...");
    // TaskHandle_t self_task = xTaskGetCurrentTaskHandle();
    // BaseType_t debug_task_created = xTaskCreate(
    //     debug_fan_task, // Task function
    //     "debug_fan",    // Task name (for debugging)
    //     2048,           // Stack size
    //     self_task,      // Notify this task when self-test completes
    //     3,              // Priority
    //     NULL            // Task handle (not needed)
    // );
    // if (debug_task_created == pdPASS)
    // {
    //     // Wait for self-test to finish before configuring other GPIOs (avoid conflicts on GPIO27/26).
    //     (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(12000));
    // }
    // else
    // {
    //     ESP_LOGE(TAG, "Failed to create debug_fan task");
    // }

    // ===== PERIPHERAL RELAYS INITIALIZATION =====
    ESP_LOGI(TAG, "Initializing peripheral relay (Light)...");

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_LIGHT), // Chỉ cài đặt chân không bị trùng lặp
        .pull_down_en = 0,
        .pull_up_en = 0};
    gpio_config(&io_conf);

    // Tắt các thiết bị mặc định khi vừa khởi động
    gpio_set_level(PIN_LIGHT, 0);

    // ===== CREATE CLOTHES HANGER CONTROL TASK (START EARLY) =====
    // Start right after motor init so hanger automation runs immediately at boot.
    ESP_LOGI(TAG, "Creating automatic clothes hanger control task...");
    BaseType_t clothes_task_created = xTaskCreate(
        clothes_hanger_task, // Task function
        "clothes_hanger",    // Task name (for debugging)
        4096,                // Stack size in bytes
        NULL,                // Task input parameter
        5,                   // Priority (higher than display for quick motor reaction)
        NULL                 // Task handle (not needed)
    );
    if (clothes_task_created != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create clothes_hanger task");
    }
    else
    {
        ESP_LOGI(TAG, "clothes_hanger task created successfully");
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    // ===== TIMEZONE CONFIGURATION =====
    // Set timezone to Vietnam (ICT, UTC+7)
    // This applies to all time functions like localtime()
    setenv("TZ", "ICT-7", 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to Vietnam (UTC+7)");

    // ===== WIFI INITIALIZATION =====
    // Must initialize WiFi before SNTP for time synchronization
    // Connects to your specified WiFi network for NTP time sync
    // Will timeout after 60 seconds if connection fails, then falls back to local time
    ESP_LOGI(TAG, "Setting up WiFi connection...");
    initialize_wifi();

    // ===== TIME SYNCHRONIZATION =====
    // Initialize SNTP to sync time with NTP server
    // This ensures the OLED displays the correct Vietnam time (UTC+7)
    // WiFi must be connected first for NTP to work
    // Falls back to local time if WiFi connection fails
    ESP_LOGI(TAG, "Setting up time synchronization with NTP...");
    initialize_sntp();
    wait_for_time_sync();

    // ===== ESP-NOW WIRELESS PROTOCOL =====
    ESP_LOGI(TAG, "Initializing ESP-NOW protocol...");
    espnow_init();
    espnow_register_recv_cb(on_message);
    espnow_add_peer(peer_mac);

    // ===== CREATE DISPLAY TASK =====
    // Creates a FreeRTOS task for displaying time
    // Task: Shows time on OLED every second
    ESP_LOGI(TAG, "Creating display task...");
    xTaskCreate(
        display_task,   // Task function
        "display_task", // Task name (for debugging)
        8192,           // Stack size in bytes (increased from 4096)
        NULL,           // Task input parameter
        4,              // Priority (0-24, lowered to 4)
        NULL            // Task handle (not needed)
    );
    vTaskDelay(pdMS_TO_TICKS(500));

    // ===== MAIN LOOP: SILENT OPERATION =====
    // Device runs silently, OLED updates every 5 minutes in background
    ESP_LOGI(TAG, "Initialization complete. Device running silently...");

    while (1)
    {
        // Just keep the watchdog alive and let display task handle everything
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every 1 second
    }
}
