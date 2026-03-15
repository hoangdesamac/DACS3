/**
 * Sensor Reading Module for Environmental Monitoring
 * 
 * Supports:
 *   - DHT22: Temperature & Humidity (GPIO4) - Uses bit-banging protocol
 *   - Soil Moisture: ADC reading from GPIO36 (ADC1_CH0)
 *   - Light Level (LDR): ADC reading from GPIO39 (ADC1_CH3)
 *   - Rain Sensor: Digital GPIO input on GPIO5 (pull-up enabled)
 */

#include "sensors.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SENSORS";

/* ========== PIN CONFIGURATION ========== */
// GPIO and ADC channel assignments for each sensor
#define DHT22_PIN GPIO_NUM_4                    // DHT22 data line (single wire protocol)
#define SOIL_MOISTURE_ADC_CHANNEL ADC_CHANNEL_0 // GPIO36 on ESP32, ADC1_CH0
#define LIGHT_LDR_ADC_CHANNEL ADC_CHANNEL_3     // GPIO39 on ESP32, ADC1_CH3
#define RAIN_SENSOR_PIN GPIO_NUM_5              // Rain detection (0 = raining, 1 = dry)

/* ========== ADC CONFIGURATION ========== */
static adc_oneshot_unit_handle_t adc1_handle;  // Handle to ADC1 controller

/**
 * Read raw bits from DHT22 sensor using bit-banging
 * 
 * DHT22 Protocol:
 *   1. Master pulls low for 20ms to trigger sensor
 *   2. Sensor responds with low pulse (~80us), then high pulse (~80us)
 *   3. Master reads 40 bits of data (5 bytes total):
 *      - Byte 0-1: Humidity integer.decimal
 *      - Byte 2-3: Temperature integer.decimal (bit 7 = sign)
 *      - Byte 4: Checksum (sum of bytes 0-3)
 *   4. Bit timing: pulse < 50us = 0, pulse > 50us = 1
 * 
 * Note: This is a simplified implementation; robust code would use timers
 * 
 * Returns: 0 on success, -1 on timeout
 */
static int dht22_read_bits(uint8_t *data)
{
    uint32_t timeout;
    
    // ===== TRANSMISSION START: Pull line low for 20ms =====
    // This signals the DHT22 to start sending data
    gpio_set_direction(DHT22_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT22_PIN, 0);                 // Pull LOW
    vTaskDelay(pdMS_TO_TICKS(20));                // Hold for 20ms
    gpio_set_level(DHT22_PIN, 1);                 // Release
    
    // ===== WAIT FOR DHT22 RESPONSE =====
    // Switch pin to input to read DHT22's response
    gpio_set_direction(DHT22_PIN, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(1));                 // Wait for DHT22 to respond
    
    // Wait for DHT22 to pull line low (start of response)
    // If timeout, sensor is not responding
    timeout = 1000;
    while (gpio_get_level(DHT22_PIN) == 1 && timeout--) {
        esp_rom_delay_us(1);
    }
    if (timeout == 0) return -1;  // No response from sensor
    
    // Wait for DHT22 to release line (ready to send data)
    timeout = 1000;
    while (gpio_get_level(DHT22_PIN) == 0 && timeout--) {
        esp_rom_delay_us(1);
    }
    if (timeout == 0) return -1;  // Sensor held line too long
    
    // ===== READ 40 BITS OF DATA (5 BYTES) =====
    // Data format: [Humidity (16 bits) | Temperature (16 bits) | Checksum (8 bits)]
    for (int i = 0; i < 5; i++) {
        uint8_t byte = 0;  // Current byte being read
        
        // Read 8 bits for this byte (MSB first)
        for (int j = 0; j < 8; j++) {
            // Wait for low pulse (bit start marker)
            timeout = 1000;
            while (gpio_get_level(DHT22_PIN) == 1 && timeout--) {
                esp_rom_delay_us(1);
            }
            
            // Wait for high pulse (bit data)
            timeout = 1000;
            while (gpio_get_level(DHT22_PIN) == 0 && timeout--) {
                esp_rom_delay_us(1);
            }
            
            // Measure how long the line stays high
            // Short pulse (~26us) = 0, Long pulse (~70us) = 1
            uint32_t high_time = 0;
            timeout = 100;
            while (gpio_get_level(DHT22_PIN) == 1 && timeout--) {
                esp_rom_delay_us(1);
                high_time++;
            }
            
            // Shift previous bits left, add new bit
            byte = (byte << 1) | (high_time > 50 ? 1 : 0);
        }
        data[i] = byte;  // Store completed byte
    }
    
    return 0;  // Success
}

/**
 * Initialize all sensors
 * Sets up GPIO pins and ADC for sensor reading
 * 
 * Returns: 0 on success, -1 on failure
 */
int sensors_init(void)
{
    ESP_LOGI(TAG, "Initializing sensors...");
    
    // ===== RAIN SENSOR: GPIO INPUT =====
    // Pin reads low when raining (sensor becomes conductive)
    // We enable pull-up so idle state is HIGH (no rain)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RAIN_SENSOR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // Pull-up resistor
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,    // No interrupts needed
    };
    gpio_config(&io_conf);
    
    // ===== ADC INITIALIZATION =====
    // Configure ADC1 for reading analog sensors (soil moisture, light level)
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,                   // Use ADC1
        .ulp_mode = ADC_ULP_MODE_DISABLE,        // Not using low-power DSP
    };
    
    if (adc_oneshot_new_unit(&init_config, &adc1_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC");
        return -1;
    }
    
    // ===== CONFIGURE ADC CHANNELS =====
    // 12-bit resolution (0-4095 range)
    // 12dB attenuation = 0-2500mV measurement range (good for 3.3V sensors)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,   // 12-bit resolution (0-4095)
        .atten = ADC_ATTEN_DB_12,      // 12dB attenuation (0-2.5V input, but ESP32 is 3.3V)
    };
    
    // Configure both analog sensor channels
    adc_oneshot_config_channel(adc1_handle, SOIL_MOISTURE_ADC_CHANNEL, &config);
    adc_oneshot_config_channel(adc1_handle, LIGHT_LDR_ADC_CHANNEL, &config);
    
    ESP_LOGI(TAG, "Sensors initialized");
    return 0;
}

/**
 * Read temperature and humidity from DHT22 sensor
 * 
 * Returns: 0 on success, -1 on checksum or read failure
 */
int dht22_read(float *temp, float *humidity)
{
    uint8_t data[5];  // 40-bit data: 2 humidity bytes + 2 temp bytes + 1 checksum
    
    // Read raw bit data from sensor
    if (dht22_read_bits(data) != 0) {
        ESP_LOGE(TAG, "DHT22 read failed");
        return -1;
    }
    
    // ===== VERIFY CHECKSUM =====
    // Checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF
    uint8_t checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
    if (checksum != data[4]) {
        ESP_LOGW(TAG, "DHT22 checksum mismatch");
        return -1;  // Data corrupted
    }
    
    // ===== EXTRACT HUMIDITY =====
    // data[0] = humidity integer part
    // data[1] = humidity decimal part (in tenths)
    // Value = (data[0] * 256 + data[1]) / 10
    uint16_t humidity_raw = (data[0] << 8) | data[1];
    *humidity = humidity_raw / 10.0f;
    
    // ===== EXTRACT TEMPERATURE =====
    // data[2] bit 7 = sign (1 = negative temperature)
    // data[2] bits 6-0 + data[3] = temperature in 0.1°C units
    uint16_t temp_raw = ((data[2] & 0x7F) << 8) | data[3];
    *temp = temp_raw / 10.0f;
    
    // Apply sign if negative temperature
    if (data[2] & 0x80) *temp = -(*temp);
    
    return 0;  // Success
}

/**
 * Read soil moisture level from analog sensor via ADC
 * 
 * The soil moisture sensor outputs analog voltage proportional to moisture content:
 *   - Dry soil: High ADC reading (closer to 4095)
 *   - Wet soil: Low ADC reading (closer to 0)
 * 
 * Calibration: Adjust dry_value and wet_value based on your specific sensor
 *   dry_value: ADC reading when soil is completely dry
 *   wet_value: ADC reading when soil is in water
 * 
 * Returns: 0 on success, -1 on failure
 */
int adc_read_soil(float *percentage)
{
    int adc_reading;  // Raw ADC value (0-4095)
    
    // Read analog value from soil moisture sensor
    int err = adc_oneshot_read(adc1_handle, SOIL_MOISTURE_ADC_CHANNEL, &adc_reading);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC read soil failed");
        return -1;
    }
    
    // ===== CALIBRATION VALUES =====
    // IMPORTANT: Adjust these based on your sensor's characteristics!
    // 1. Put sensor in dry soil, note the ADC value
    // 2. Put sensor in water, note the ADC value
    // 3. Update these constants:
    int dry_value = 4095;   // ADC reading when completely dry (no moisture)
    int wet_value = 1500;   // ADC reading when completely wet (in water)
    
    // Convert to percentage using linear mapping
    // percentage = (dry_value - adc_reading) / (dry_value - wet_value) * 100
    *percentage = 100.0f * (dry_value - adc_reading) / (dry_value - wet_value);
    
    // Clamp to 0-100% range
    if (*percentage < 0) *percentage = 0;
    if (*percentage > 100) *percentage = 100;
    
    return 0;  // Success
}

/**
 * Read light level from LDR (Light Dependent Resistor) via ADC
 * 
 * An LDR's resistance changes with light intensity:
 *   - Bright light: Low resistance → Low ADC reading
 *   - Dark: High resistance → High ADC reading
 * 
 * This function returns a simple 0-100% scale
 * 
 * Returns: 0 on success, -1 on failure
 */
int adc_read_light(float *level)
{
    int adc_reading;  // Raw ADC value (0-4095)
    
    // Read analog value from light sensor
    int err = adc_oneshot_read(adc1_handle, LIGHT_LDR_ADC_CHANNEL, &adc_reading);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC read light failed");
        return -1;
    }
    
    // Convert raw ADC to percentage (0-4095 → 0-100%)
    // Lower ADC = brighter light
    *level = 100.0f * adc_reading / 4095.0f;
    
    return 0;  // Success
}

/**
 * Read rain sensor status
 * 
 * Rain sensor behavior:
 *   - Dry (no rain): GPIO reads HIGH (1) due to pull-up
 *   - Wet (raining): GPIO reads LOW (0) as water conducts
 * 
 * Returns: 0 on success, -1 on failure
 */
int rain_read(int *detected)
{
    // Read GPIO pin level
    // gpio_get_level returns 0 for LOW, 1 for HIGH
    // Convert: LOW (0) = rain detected (1), HIGH (1) = no rain (0)
    *detected = gpio_get_level(RAIN_SENSOR_PIN) == 0 ? 1 : 0;
    return 0;  // Success
}

/**
 * Read all sensors and return combined data
 * This is the main sensor reading function - call this periodically
 * 
 * If any individual sensor fails to read, its value is set to 0
 */
void sensors_read(sensor_readings_t *readings)
{
    // Zero out all readings first
    memset(readings, 0, sizeof(sensor_readings_t));
    
    // ===== READ DHT22 =====
    // Reads humidity and temperature from DHT22
    if (dht22_read(&readings->temperature, &readings->humidity) != 0) {
        // On failure, both values default to 0 (already set by memset)
        ESP_LOGW(TAG, "DHT22 read failed, using defaults");
    }
    
    // ===== READ SOIL MOISTURE =====
    // Reads soil moisture percentage from analog sensor
    if (adc_read_soil(&readings->soil_moisture) != 0) {
        ESP_LOGW(TAG, "Soil moisture read failed");
    }
    
    // ===== READ LIGHT LEVEL =====
    // Reads light level from LDR
    if (adc_read_light(&readings->light_level) != 0) {
        ESP_LOGW(TAG, "Light level read failed");
    }
    
    // ===== READ RAIN SENSOR =====
    // Detects if it's raining
    if (rain_read(&readings->rain_detected) != 0) {
        ESP_LOGW(TAG, "Rain sensor read failed");
    }
}
