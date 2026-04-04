/**
 * Sensor Reading Module for Environmental Monitoring
 *
 * Supports:
 *   - DHT11: Single-wire digital data (GPIO14)
 *   - Light Level (LDR): Digital GPIO input LM393 DO (GPIO33)
 *   - Rain Sensor: Digital GPIO input LM393 DO (GPIO32)
 */

#include "sensors.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SENSORS";

/* ========== PIN CONFIGURATION ========== */
#define DHT11_GPIO GPIO_NUM_14              // DHT11 data pin
#define RAIN_SENSOR_LM393_PIN GPIO_NUM_32   // Rain sensor DO
#define LIGHT_SENSOR_LM393_PIN GPIO_NUM_33  // Light sensor DO

/* ========== DHT11 TIMING (us) ========== */
#define DHT11_START_SIGNAL_MS 20
#define DHT11_RESPONSE_TIMEOUT_US 200
#define DHT11_BIT_START_TIMEOUT_US 80
#define DHT11_BIT_HIGH_TIMEOUT_US 120
#define DHT11_BIT_HIGH_THRESHOLD_US 40

static portMUX_TYPE dht11_spinlock = portMUX_INITIALIZER_UNLOCKED;

// Store the last read time to prevent reading too frequently (DHT11 needs 2s)
static int64_t last_dht11_read_time = 0;
static float last_temperature = 0.0f;
static float last_humidity = 0.0f;

/**
 * Initialize all sensors
 * Sets up GPIO pins and ADC for sensor reading
 *
 * Returns: 0 on success, -1 on failure
 */
int sensors_init(void)
{
    ESP_LOGI(TAG, "Initializing sensors...");

    // ===== DHT11 DIGITAL INPUT =====
    gpio_config_t dht11_conf = {
        .pin_bit_mask = (1ULL << DHT11_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&dht11_conf);
    gpio_set_level(DHT11_GPIO, 1); // Set idle high
    ESP_LOGI(TAG, "DHT11 configured on GPIO14 (DATA, Open-Drain)");

    // ===== LM393 DIGITAL OUTPUT GPIO INPUTS =====
    gpio_config_t io_conf = {
        .pin_bit_mask =
            (1ULL << RAIN_SENSOR_LM393_PIN) |
            (1ULL << LIGHT_SENSOR_LM393_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "LM393 DO configured (GPIO32=Rain, GPIO33=Light)");

    ESP_LOGI(TAG, "Sensors initialized successfully");
    return 0;
}

/**
 * Wait until the DHT11 data line reaches the desired level
 *
 * Returns: 0 on success, -1 on timeout
 */
static int dht11_wait_for_level(int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();

    while (gpio_get_level(DHT11_GPIO) != level)
    {
        if ((esp_timer_get_time() - start) > timeout_us)
        {
            return -1;
        }
    }

    return 0;
}

static int dht11_read_raw(uint8_t data[5])
{
    memset(data, 0, 5);

    if (dht11_wait_for_level(0, DHT11_RESPONSE_TIMEOUT_US) != 0)
    {
        return -1; // Timeout waiting for sensor LOW ACK
    }

    if (dht11_wait_for_level(1, DHT11_RESPONSE_TIMEOUT_US) != 0)
    {
        return -2; // Timeout waiting for sensor HIGH ACK
    }

    if (dht11_wait_for_level(0, DHT11_RESPONSE_TIMEOUT_US) != 0)
    {
        return -3; // Timeout waiting for end of ACK (sensor pulling LOW to start bit)
    }

    for (int i = 0; i < 40; i++)
    {
        if (dht11_wait_for_level(1, DHT11_BIT_START_TIMEOUT_US) != 0)
        {
            return -4; // Timeout waiting for bit start (HIGH)
        }

        int64_t start = esp_timer_get_time();
        if (dht11_wait_for_level(0, DHT11_BIT_HIGH_TIMEOUT_US) != 0)
        {
            return -5; // Timeout waiting for bit finish (LOW)
        }

        int high_us = (int)(esp_timer_get_time() - start);
        uint8_t bit = (high_us > DHT11_BIT_HIGH_THRESHOLD_US) ? 1 : 0;
        data[i / 8] = (data[i / 8] << 1) | bit;
    }

    return 0;
}

/**
 * Read temperature and humidity from DHT11 (single-wire digital)
 *
 * Returns: 0 on success, -1 on failure
 */
int dht11_read(float *temp, float *humidity)
{
    if (temp == NULL || humidity == NULL)
    {
        return -1;
    }

    int64_t current_time = esp_timer_get_time();
    if (last_dht11_read_time > 0 && (current_time - last_dht11_read_time) < 2000000)
    {
        // DHT11 needs at least 2 seconds between reads
        *temp = last_temperature;
        *humidity = last_humidity;
        return 0; // Return cached value
    }

    // Pull low to signal start
    gpio_set_level(DHT11_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(DHT11_START_SIGNAL_MS));
    
    // Release the line to be pulled high by resistor, wait 30us
    gpio_set_level(DHT11_GPIO, 1);
    esp_rom_delay_us(30);

    uint8_t data[5] = {0};
    int result = 0;

    portENTER_CRITICAL(&dht11_spinlock);
    result = dht11_read_raw(data);
    portEXIT_CRITICAL(&dht11_spinlock);

    // Always update the read time so the sensor gets 2 seconds of rest even after a failure
    last_dht11_read_time = esp_timer_get_time();

    if (result != 0)
    {
        ESP_LOGW(TAG, "DHT11 read timeout (code: %d)", result);
        *temp = last_temperature;
        *humidity = last_humidity;
        return -1;
    }

    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4])
    {
        ESP_LOGW(TAG, "DHT11 checksum mismatch");
        *temp = last_temperature;
        *humidity = last_humidity;
        return -1;
    }

    *humidity = (float)data[0] + ((float)data[1] * 0.1f);
    *temp = (float)data[2] + ((float)data[3] * 0.1f);
    
    // Cache valid readings
    last_temperature = *temp;
    last_humidity = *humidity;
    last_dht11_read_time = esp_timer_get_time();

    return 0;
}

/**
 * Read light level from LDR using LM393 comparator output
 *
 * Returns light as binary: 0% (dark) or 100% (bright)
 *
 * Returns: 0 on success, -1 on failure
 */
int adc_read_light(float *level)
{
    // Read digital comparator output from GPIO33
    // HIGH (1) = dark, LOW (0) = bright (INVERTED)
    int light_detected = gpio_get_level(LIGHT_SENSOR_LM393_PIN);

    // Invert: HIGH = dark (0%), LOW = bright (100%)
    *level = light_detected ? 0.0f : 100.0f;

    return 0;
}

/**
 * Read rain sensor using LM393 comparator output
 *
 * Returns: 0 on success, -1 on failure
 */
int rain_read(int *detected)
{
    // Read digital comparator output from GPIO32
    // LOW (0) = rain detected, HIGH (1) = dry
    *detected = gpio_get_level(RAIN_SENSOR_LM393_PIN) == 0 ? 1 : 0;
    return 0;
}

/**
 * Read all sensors and return combined data
 */
void sensors_read(sensor_readings_t *readings)
{
    memset(readings, 0, sizeof(sensor_readings_t));

    // Read all sensors silently without logging to avoid monitor spam
    dht11_read(&readings->temperature, &readings->humidity);
    adc_read_light(&readings->light_level);
    rain_read(&readings->rain_detected);
    
    // Vituralization value
    readings->soil_moisture = 50.0f; 
}
