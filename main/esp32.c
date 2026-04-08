/**
 * DACS3 Main Application
 *
 * Environmental Sensor Monitoring System
 *
 * Features:
 *   - Reads multiple sensors (temperature, humidity, light, rain)
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

/* ========== SHARED DATA ========== */
// Global sensor data structure for display
// This is updated by the sensor task and read by main task for ESP-NOW sending
static sensor_data_t display_data = {0};

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
    // Copy message to safe buffer (limit to 255 bytes)
    char buf[256] = {0};
    memcpy(buf, data, len < 255 ? len : 255);
    ESP_LOGI(TAG, "Got message: %s", buf);
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

        // Update OLED display
        oled_display_sensor_data(&display_data);

        // ===== GỬI DATA QUA ESP-NOW (ĐÃ SỬA) =====
        // Gửi trực tiếp cấu trúc nhị phân thay vì dùng snprintf chuyển thành chuỗi (String)
        // Điều này đảm bảo Gateway (nhận) có thể ép kiểu trực tiếp từ byte sang Struct một cách chính xác
        espnow_send(peer_mac, (const uint8_t *)&display_data, sizeof(sensor_data_t));

        int rain_raw = -1;
        int light_raw = -1;
        rain_read_raw(&rain_raw);
        light_read_raw(&light_raw);

        update_count++;

        // Monitor-friendly full sensor line for easier tracking
        if (update_count % SENSOR_LIVE_LOG_INTERVAL_SEC == 0)
        {
            ESP_LOGI(TAG, "SENSOR LIVE - %s | DHT:%s T=%.1fC H=%.1f%% | LDR:%s DO=%d => %s (%.0f%%) | RAIN:%s DO=%d => %s",
                     display_data.time_str,
                     (dht_status == 0) ? "OK" : "ERR",
                     display_data.temperature,
                     display_data.humidity,
                     (light_status == 0) ? "OK" : "ERR",
                     light_raw,
                     (display_data.light_level >= 50.0f) ? "BRIGHT" : "DARK",
                     display_data.light_level,
                     (rain_status == 0) ? "OK" : "ERR",
                     rain_raw,
                     display_data.rain_detected ? "WET" : "DRY");
        }

        // Full sensor summary at a longer interval
        if (update_count % SENSOR_SUMMARY_LOG_INTERVAL_SEC == 0)
        {
            ESP_LOGI(TAG, "SENSORS UPDATE - Time: %s | Temp: %.1f°C | Humidity: %.1f%% | Light: %.0f%% | Rain: %s",
                     display_data.time_str,
                     display_data.temperature,
                     display_data.humidity,
                     display_data.light_level,
                     display_data.rain_detected ? "WET" : "DRY");
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
    const float BRIGHT_THRESHOLD = 50.0f;
    const TickType_t CONTROL_PERIOD = pdMS_TO_TICKS(100); // Giảm delay xuống 100ms để giống delay_custom(100)
    int log_counter = 0;

    // ===== LATCH flags & STATE =====
    bool startLimitLatched = false;
    bool endLimitLatched = false;
    motor_direction_t currentMotorState = MOTOR_STOP;

    ESP_LOGI(TAG, "Starting automatic clothes hanger control task (Arduino Logic Mode)...");

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
        int startPressed = motor_read_limit_switch_in(); // LIMIT_SWITCH_START
        int endPressed = motor_read_limit_switch_out();  // LIMIT_SWITCH_END

        // ===== Definite Motor Direction Logic =====
        // Logic: Trời sáng VÀ Không mưa -> Phơi đồ (FORWARD)
        // Ngược lại -> Thu đồ (REVERSE)
        motor_direction_t desiredMotorState =
            (light_level > BRIGHT_THRESHOLD && !rain_detected) ? MOTOR_FORWARD : MOTOR_REVERSE;

        // ===== State Machine =====
        switch (currentMotorState)
        {

        // --- 1: Motor Stopped ---
        case MOTOR_STOP:
            // Tách riêng điều kiện cho từng chiều để tránh bị kẹt
            if (desiredMotorState == MOTOR_FORWARD)
            {
                // Muốn đi TIẾN: Chỉ cần chưa chạm công tắc giới hạn ĐÍCH (End)
                // Và reset cờ latch của chiều ngược lại nếu cần
                if (!endPressed)
                {
                    endLimitLatched = false; // Reset latch đích
                    motor_set_direction(MOTOR_FORWARD);
                    currentMotorState = MOTOR_FORWARD;
                    ESP_LOGI(TAG, "CLOTHES HANGER - Chuyển sang PHƠI DO (FORWARD)");
                }
            }
            else if (desiredMotorState == MOTOR_REVERSE)
            {
                // Muốn đi LÙI: Chỉ cần chưa chạm công tắc giới hạn ĐẦU (Start)
                if (!startPressed)
                {
                    startLimitLatched = false; // Reset latch đầu
                    motor_set_direction(MOTOR_REVERSE);
                    currentMotorState = MOTOR_REVERSE;
                    ESP_LOGI(TAG, "CLOTHES HANGER - Chuyển sang KÉO VÀO (REVERSE)");
                }
            }
            break;

        // --- 2: Motor Forward (Phơi) ---
        case MOTOR_FORWARD:
            // Ưu tiên 1: Chạm công tắc hành trình thì dừng
            if (endPressed)
            {
                motor_set_direction(MOTOR_STOP);
                currentMotorState = MOTOR_STOP;
                endLimitLatched = true;
                ESP_LOGI(TAG, "HẠN CHẾ CUỐI - Dừng motor (Đã phơi xong)");
            }
            // Ưu tiên 2: Cảm biến thay đổi ý định (trời mưa/tối)
            else if (desiredMotorState == MOTOR_REVERSE)
            {
                motor_set_direction(MOTOR_STOP);
                vTaskDelay(pdMS_TO_TICKS(500)); // Đợi dừng hẳn
                motor_set_direction(MOTOR_REVERSE);
                currentMotorState = MOTOR_REVERSE;
                ESP_LOGI(TAG, "CLOTHES HANGER - Đổi ý định: Chuyển sang KÉO VÀO (REVERSE)");
            }
            break;

        // --- 3: Motor Reverse (Thu) ---
        case MOTOR_REVERSE:
            // Ưu tiên 1: Chạm công tắc hành trình thì dừng
            if (startPressed)
            {
                motor_set_direction(MOTOR_STOP);
                currentMotorState = MOTOR_STOP;
                startLimitLatched = true;
                ESP_LOGI(TAG, "HẠN CHẾ CÓI - Dừng motor (Đã thu xong)");
            }
            // Ưu tiên 2: Cảm biến thay đổi ý định (trời nắng lại)
            else if (desiredMotorState == MOTOR_FORWARD)
            {
                motor_set_direction(MOTOR_STOP);
                vTaskDelay(pdMS_TO_TICKS(500)); // Đợi dừng hẳn
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

        // ===== HEARTBEAT LOG (Chu kỳ 2 giây = 20 * 100ms) =====
        if (++log_counter % 20 == 0)
        {
            ESP_LOGI(TAG, "CLOTHES TASK - LDR:%.0f%% | Rain:%s | Limit START:%d END:%d | Curr:%s Desired:%s",
                     light_level, rain_detected ? "WET" : "DRY",
                     startPressed, endPressed,
                     motor_dir_to_str(currentMotorState),
                     motor_dir_to_str(desiredMotorState));
        }

        vTaskDelay(CONTROL_PERIOD); // Delay 100ms
    }
}

/**
 * Application entry point
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
