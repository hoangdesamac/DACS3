#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdbool.h>
#include "driver/gpio.h"

/* Motor control directions */
typedef enum
{
    MOTOR_STOP = 0,
    MOTOR_FORWARD = 1, // Run forward until limit switch GPIO5 pressed
    MOTOR_REVERSE = 2  // Run reverse until limit switch GPIO17 pressed
} motor_direction_t;

/**
 * Initialize actuator GPIO pins and limit switches
 *
 * L293 motor pins:
 *   - GPIO18 -> IN1 (forward control)
 *   - GPIO19 -> IN2 (reverse control)
 *   - GPIO13 -> ENA (PWM speed control)
 *
 * DRV8833 fan pins:
 *   - GPIO25 -> IN1
 *   - GPIO26 -> IN2
 *
 * DRV8833 ultrasonic transducer pins:
 *   - GPIO27 -> IN3
 *   - GPIO12 -> IN4
 *
 * Limit switches:
 *   - GPIO5  -> Limit switch OUT (clothes fully extended)
 *   - GPIO17 -> Limit switch IN (clothes fully retracted)
 *
 * Returns: 0 on success, -1 on failure
 */
int motor_init(void);

/**
 * Get limit switch status
 *
 * Returns: 1 if button pressed (LOW), 0 if released (HIGH)
 */
int motor_read_limit_switch_out(void); // GPIO5
int motor_read_limit_switch_in(void);  // GPIO17

/**
 * Control motor direction with limit switch protection
 *
 * - MOTOR_FORWARD: Runs motor forward until GPIO5 limit pressed
 * - MOTOR_REVERSE: Runs motor reverse until GPIO17 limit pressed
 * - MOTOR_STOP: Stops motor immediately
 *
 * Returns: 0 on success, -1 on failure
 */
int motor_set_direction(motor_direction_t direction);

/**
 * Control DRV8833 fan channel
 *
 * on=true  -> IN1=1, IN2=0
 * on=false -> IN1=0, IN2=0
 */
int drv8833_fan_set_power(bool on);

/**
 * Control DRV8833 ultrasonic transducer channel
 *
 * on=true  -> IN3=1, IN4=0
 * on=false -> IN3=0, IN4=0
 */
int drv8833_ultrasonic_set_power(bool on);

/**
 * Control DRV8833 "AC" combo (fan + mist)
 *
 * on=true  -> Fan ON + Ultrasonic ON
 * on=false -> Both OFF
 */
int drv8833_ac_set_power(bool on);

/**
 * Stop the motor (both IN1 and IN2 = LOW)
 */
int motor_stop(void);

/**
 * Get current motor direction
 */
motor_direction_t motor_get_direction(void);

#endif // MOTOR_CONTROL_H
