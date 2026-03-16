/**
 * Sensor Reading Module for Environmental Monitoring
 *
 * Supports:
 *   - DHT22: Analog Output via LM393 (GPIO36/ADC1_CH0)
 *   - Soil Moisture: Digital GPIO input LM393 DO (GPIO34)
 *   - Light Level (LDR): Digital GPIO input LM393 DO (GPIO33)
 *   - Rain Sensor: Digital GPIO input LM393 DO (GPIO32)
 */

#include "sensors.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SENSORS";

/* ========== PIN CONFIGURATION ========== */
#define DHT22_AO_ADC_CHANNEL ADC_CHANNEL_0  // DHT22 AO via LM393 (GPIO36)
#define SOIL_MOISTURE_LM393_PIN GPIO_NUM_34 // Soil moisture DO
#define RAIN_SENSOR_LM393_PIN GPIO_NUM_32   // Rain sensor DO
#define LIGHT_SENSOR_LM393_PIN GPIO_NUM_33  // Light sensor DO

/* ========== ADC CONFIGURATION ========== */
static adc_oneshot_unit_handle_t adc1_handle; // Handle to ADC1 controller

/**
 * Initialize all sensors
 * Sets up GPIO pins and ADC for sensor reading
 *
 * Returns: 0 on success, -1 on failure
 */
int sensors_init(void)
{
    ESP_LOGI(TAG, "Initializing sensors...");

    // ===== ADC INITIALIZATION FOR DHT22 AO =====
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    if (adc_oneshot_new_unit(&init_config, &adc1_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize ADC");
        return -1;
    }

    // ===== CONFIGURE DHT22 ADC CHANNEL =====
    adc_oneshot_chan_cfg_t adc_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };

    adc_oneshot_config_channel(adc1_handle, DHT22_AO_ADC_CHANNEL, &adc_config);
    ESP_LOGI(TAG, "DHT22 AO ADC configured on GPIO36 (ADC1_CH0)");

    // ===== LM393 DIGITAL OUTPUT GPIO INPUTS =====
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SOIL_MOISTURE_LM393_PIN) |
                        (1ULL << RAIN_SENSOR_LM393_PIN) |
                        (1ULL << LIGHT_SENSOR_LM393_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "LM393 DO configured (GPIO32=Rain, GPIO33=Light, GPIO34=Soil)");

    ESP_LOGI(TAG, "Sensors initialized successfully");
    return 0;
}

/**
 * Read temperature and humidity from DHT22 via LM393 AO
 *
 * Returns: 0 on success, -1 on failure
 */
int dht22_read(float *temp, float *humidity)
{
    int adc_reading;

    // Read ADC value from GPIO36 (DHT22 AO output)
    if (adc_oneshot_read(adc1_handle, DHT22_AO_ADC_CHANNEL, &adc_reading) != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC read DHT22 failed");
        *temp = 0.0f;
        *humidity = 0.0f;
        return -1;
    }

    // Convert ADC (0-4095) to voltage (0-3.3V)
    float voltage = (adc_reading / 4095.0f) * 3.3f;

    // Map voltage to temperature and humidity
    // Temperature: -40 + (voltage / 3.3) * 165 (range -40 to +125°C)
    *temp = -40.0f + (voltage / 3.3f) * 165.0f;

    // Humidity: (voltage / 3.3) * 100 (range 0 to 100%)
    *humidity = (voltage / 3.3f) * 100.0f;

    // Clamp to valid ranges
    if (*temp < -40.0f)
        *temp = -40.0f;
    if (*temp > 125.0f)
        *temp = 125.0f;
    if (*humidity < 0.0f)
        *humidity = 0.0f;
    if (*humidity > 100.0f)
        *humidity = 100.0f;

    return 0;
}

/**
 * Read soil moisture level using LM393 comparator output
 *
 * Returns soil moisture as binary: 0% (dry) or 100% (wet)
 *
 * Returns: 0 on success, -1 on failure
 */
int adc_read_soil(float *percentage)
{
    // Read digital comparator output from GPIO34
    // HIGH (1) = dry, LOW (0) = wet
    int soil_wet = gpio_get_level(SOIL_MOISTURE_LM393_PIN) == 0 ? 1 : 0;

    *percentage = soil_wet ? 100.0f : 0.0f;

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
    dht22_read(&readings->temperature, &readings->humidity);
    adc_read_soil(&readings->soil_moisture);
    adc_read_light(&readings->light_level);
    rain_read(&readings->rain_detected);
}
