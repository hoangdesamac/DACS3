/**
 * Motor Control Module for L298 Motor Driver with Limit Switches
 *
 * Controls motor direction using GPIO pins with limit switch protection:
 *   - GPIO18 (IN1): Forward control
 *   - GPIO19 (IN2): Reverse control
 *   - GPIO5  (Limit OUT): Pressed when clothes fully extended (active LOW)
 *   - GPIO17 (Limit IN): Pressed when clothes fully retracted (active LOW)
 *
 * Motor direction truth table:
 *   IN1 | IN2 | Motor
 *   ----|-----|--------
 *    0  |  0  | Stop
 *    1  |  0  | Forward (runs until GPIO5 pressed)
 *    0  |  1  | Reverse (runs until GPIO17 pressed)
 *    1  |  1  | Stop (unused)
 */

#include "motor.h"
#include "esp_log.h"

static const char *TAG = "MOTOR";

/* ========== PIN CONFIGURATION ========== */
#define MOTOR_IN1_PIN GPIO_NUM_18      // Forward control
#define MOTOR_IN2_PIN GPIO_NUM_19      // Reverse control
#define MOTOR_LIMIT_OUT_PIN GPIO_NUM_5 // Clothes OUT position (active LOW)
#define MOTOR_LIMIT_IN_PIN GPIO_NUM_17 // Clothes IN position (active LOW)

/* Track current motor state */
static motor_direction_t current_direction = MOTOR_STOP;

/**
 * Initialize motor control GPIO pins and limit switches
 */
int motor_init(void)
{
    ESP_LOGI(TAG, "Initializing motor control with limit switches...");

    // ===== MOTOR CONTROL PINS =====
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

    // ===== LIMIT SWITCH INPUT PINS =====
    gpio_config_t limit_conf = {
        .pin_bit_mask = (1ULL << MOTOR_LIMIT_OUT_PIN) | (1ULL << MOTOR_LIMIT_IN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
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
    ESP_LOGI(TAG, "  - GPIO5 (Limit OUT): Clothes fully extended");
    ESP_LOGI(TAG, "  - GPIO17 (Limit IN): Clothes fully retracted");

    return 0;
}

/**
 * Read limit switch status (active LOW - pressed = 0)
 */
int motor_read_limit_switch_out(void)
{
    return gpio_get_level(MOTOR_LIMIT_OUT_PIN) == 0 ? 1 : 0;
}

int motor_read_limit_switch_in(void)
{
    return gpio_get_level(MOTOR_LIMIT_IN_PIN) == 0 ? 1 : 0;
}

/**
 * Set motor direction
 *
 * FORWARD: Runs until limit OUT switch (GPIO5) is pressed
 * REVERSE: Runs until limit IN switch (GPIO17) is pressed
 * STOP: Stops immediately
 */
int motor_set_direction(motor_direction_t direction)
{
    motor_direction_t applied_direction = direction;

    switch (direction)
    {
    case MOTOR_STOP:
        gpio_set_level(MOTOR_IN1_PIN, 0);
        gpio_set_level(MOTOR_IN2_PIN, 0);
        ESP_LOGI(TAG, "Motor STOP");
        break;

    case MOTOR_FORWARD:
        // Check if already at limit OUT position
        if (motor_read_limit_switch_out())
        {
            ESP_LOGW(TAG, "Motor FORWARD - Already at OUT position (limit switch pressed)");
            gpio_set_level(MOTOR_IN1_PIN, 0);
            gpio_set_level(MOTOR_IN2_PIN, 0);
            applied_direction = MOTOR_STOP;
            break;
        }
        gpio_set_level(MOTOR_IN1_PIN, 1);
        gpio_set_level(MOTOR_IN2_PIN, 0);
        ESP_LOGI(TAG, "Motor FORWARD (running until limit OUT pressed)");
        break;

    case MOTOR_REVERSE:
        // Check if already at limit IN position
        if (motor_read_limit_switch_in())
        {
            ESP_LOGW(TAG, "Motor REVERSE - Already at IN position (limit switch pressed)");
            gpio_set_level(MOTOR_IN1_PIN, 0);
            gpio_set_level(MOTOR_IN2_PIN, 0);
            applied_direction = MOTOR_STOP;
            break;
        }
        gpio_set_level(MOTOR_IN1_PIN, 0);
        gpio_set_level(MOTOR_IN2_PIN, 1);
        ESP_LOGI(TAG, "Motor REVERSE (running until limit IN pressed)");
        break;

    default:
        ESP_LOGE(TAG, "Invalid motor direction: %d", direction);
        return -1;
    }

    current_direction = applied_direction;
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
