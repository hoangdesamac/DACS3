/**
 * Motor Control Module for L298 Motor Driver with Limit Switches
 *
 * Limit switch wiring (NC - Normally Closed contacts):
 *   - GPIO5  (Limit OUT): NC pin → GPIO5, COM pin → GND (clothes fully OUT when pressed HIGH)
 *   - GPIO17 (Limit IN):  NC pin → GPIO17, COM pin → GND (clothes fully IN when pressed HIGH)
 *
 * When button NOT pressed (normal): GPIO = LOW (0) - switch open
 * When button pressed (triggered): GPIO = HIGH (1) - switch contacts connected
 *
 * Motor control pins:
 *   - GPIO18 (IN1): Forward control
 *   - GPIO19 (IN2): Reverse control
 *
 * Motor direction truth table:
 *   IN1 | IN2 | Motor
 *   ----|-----|--------
 *    0  |  0  | Stop
 *    1  |  0  | Forward (PHƠI ĐỒ - clothes OUT)
 *    0  |  1  | Reverse (KÉO VÀO - clothes IN)
 *    1  |  1  | Stop (unused)
 */

#include "motor.h"
#include "esp_log.h"
#include "driver/ledc.h"

static const char *TAG = "MOTOR";

/* ========== PIN CONFIGURATION ========== */
#define MOTOR_IN1_PIN GPIO_NUM_18      // Forward control
#define MOTOR_IN2_PIN GPIO_NUM_19      // Reverse control
#define MOTOR_ENA_PIN GPIO_NUM_13      // PWM Enable control
#define MOTOR_LIMIT_OUT_PIN GPIO_NUM_5 // Clothes OUT position (active LOW)
#define MOTOR_LIMIT_IN_PIN GPIO_NUM_17 // Clothes IN position (active LOW)

/* ========== PWM CONFIGURATION ========== */
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO MOTOR_ENA_PIN // Tín hiệu kích ENA
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_8_BIT // Độ phân giải PWM (0-255)
#define LEDC_FREQUENCY 5000            // Tần số 5 kHz
#define MOTOR_SPEED_MAX 255            // Tốc độ tối đa (100% duty cycle)

/* Track current motor state */
static motor_direction_t current_direction = MOTOR_STOP;

/**
 * Initialize motor control GPIO pins, PWM and limit switches
 */
int motor_init(void)
{
    ESP_LOGI(TAG, "Initializing motor control with limit switches...");

    // ===== MOTOR CONTROL PINS (IN1, IN2) =====
    gpio_config_t motor_conf = {
        .pin_bit_mask = (1ULL << MOTOR_IN1_PIN) | (1ULL << MOTOR_IN2_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&motor_conf) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure motor GPIO pins");
        return -1;
    }

    // ===== PWM FOR ENA PIN (Tốc độ / Bật tắt motor) =====
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LEDC_OUTPUT_IO,
        .duty = 0, // Bắt đầu ở cự ly duty = 0 (tắt)
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // ===== LIMIT SWITCH INPUT PINS =====
    // NC (Normally Closed) switches: pull-down so GPIO stays LOW when not pressed
    gpio_config_t limit_conf = {
        .pin_bit_mask = (1ULL << MOTOR_LIMIT_OUT_PIN) | (1ULL << MOTOR_LIMIT_IN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // BẬT KÉO LÊN: Để khi công tắc mở ra (bị ấn), chân sẽ lên HIGH (1)
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (gpio_config(&limit_conf) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure limit switch GPIO pins");
        return -1;
    }

    // Initialize both motor pins to LOW (motor stop)
    gpio_set_level(MOTOR_IN1_PIN, 0);
    gpio_set_level(MOTOR_IN2_PIN, 0);
    current_direction = MOTOR_STOP;

    ESP_LOGI(TAG, "Motor control initialized:");
    ESP_LOGI(TAG, "  - GPIO18 (IN1): Forward control");
    ESP_LOGI(TAG, "  - GPIO19 (IN2): Reverse control");
    ESP_LOGI(TAG, "  - GPIO13 (ENA): PWM Speed control (5kHz 8-bit)");
    ESP_LOGI(TAG, "  - GPIO5 (Limit OUT): NC switch");
    ESP_LOGI(TAG, "  - GPIO17 (Limit IN): NC switch");

    return 0;
}

/**
 * Read limit switch status (NC contacts - pressed = HIGH)
 * NC (Normally Closed) switches: When button pressed → GPIO = HIGH
 */
int motor_read_limit_switch_out(void)
{
    // GPIO5: Return 1 (pressed) when HIGH, 0 (open) when LOW
    return gpio_get_level(MOTOR_LIMIT_OUT_PIN) == 1 ? 1 : 0;
}

int motor_read_limit_switch_in(void)
{
    // GPIO17: Return 1 (pressed) when HIGH, 0 (open) when LOW
    return gpio_get_level(MOTOR_LIMIT_IN_PIN) == 1 ? 1 : 0;
}

/**
 * Set motor direction - Commands motor WITHOUT checking limit switch conditions
 *
 * Limit switch handling is done in the state machine (clothes_hanger_task).
 * The motor driver simply executes the direction command.
 *
 * FORWARD: GPIO18=1, GPIO19=0 (chạy PHƠI ĐỒ)
 * REVERSE: GPIO18=0, GPIO19=1 (chạy KÉO VÀO)
 * STOP:    GPIO18=0, GPIO19=0 (dừng motor)
 */
int motor_set_direction(motor_direction_t direction)
{
    switch (direction)
    {
    case MOTOR_STOP:
        gpio_set_level(MOTOR_IN1_PIN, 0);
        gpio_set_level(MOTOR_IN2_PIN, 0);
        // Tắt xung PWM khi dừng để tiết kiệm điện và chống nhiễu
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        ESP_LOGI(TAG, "Motor STOP (dừng)");
        break;

    case MOTOR_FORWARD:
        gpio_set_level(MOTOR_IN1_PIN, 1);
        gpio_set_level(MOTOR_IN2_PIN, 0);
        // Bật PWM tốc độ tối đa mặc định (255 ~ 100%)
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, MOTOR_SPEED_MAX);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        ESP_LOGI(TAG, "Motor FORWARD (chạy ra ngoài - PHƠI ĐỒ) tại 100% PWM");
        break;

    case MOTOR_REVERSE:
        gpio_set_level(MOTOR_IN1_PIN, 0);
        gpio_set_level(MOTOR_IN2_PIN, 1);
        // Bật PWM tốc độ tối đa mặc định (255 ~ 100%)
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, MOTOR_SPEED_MAX);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        ESP_LOGI(TAG, "Motor REVERSE (chạy vào trong - KÉO VÀO) tại 100% PWM");
        break;

    default:
        ESP_LOGE(TAG, "Invalid motor direction: %d", direction);
        return -1;
    }

    current_direction = direction;
    return 0;
}

/**
 * Stop the motor
 */
int motor_stop(void)
{
    return motor_set_direction(MOTOR_STOP);
}

/**
 * Get current motor direction
 */
motor_direction_t motor_get_direction(void)
{
    return current_direction;
}
