/**
 * OLED Display Driver for SSD1306 (128x64)
 *
 * This module controls a SSD1306 OLED display over I2C.
 * Features:
 *   - SSD1306 128x64 pixel display
 *   - I2C communication (GPIO22: SCL, GPIO21: SDA)
 *   - Built-in 5x7 character font
 *   - Double buffering for flicker-free updates
 *   - Display of sensor data (temperature, humidity, soil moisture, etc.)
 */

#include "oled_display.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "OLED";

/* ========== DISPLAY CONFIGURATION ========== */
// SSD1306 OLED dimensions (pixels)
// For 0.91" OLED: 128x32 (32 pixels height = 4 pages)
#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_PAGES (OLED_HEIGHT / 8) // 32 pixels / 8 bits per page = 4 pages

/* ========== I2C CONFIGURATION ========== */
// I2C bus and pin configuration
#define I2C_MASTER_NUM I2C_NUM_0  // Use I2C port 0
#define I2C_MASTER_SCL_IO 22      // Clock signal pin
#define I2C_MASTER_SDA_IO 21      // Data signal pin
#define I2C_MASTER_FREQ_HZ 400000 // 400 kHz I2C speed
#define OLED_I2C_ADDR 0x3C        // I2C slave address (most common for SSD1306)

/* ========== SSD1306 COMMAND CODES ========== */
// These are standard SSD1306 controller commands
#define SSD1306_CMD_SET_CONTRAST 0x81
#define SSD1306_CMD_DISPLAY_ON 0xAF
#define SSD1306_CMD_DISPLAY_OFF 0xAE
#define SSD1306_CMD_NORMAL_DISPLAY 0xA6
#define SSD1306_CMD_INVERSE_DISPLAY 0xA7
#define SSD1306_CMD_SET_DISP_CLK_DIV 0xD5
#define SSD1306_CMD_SET_MUX_RATIO 0xA8
#define SSD1306_CMD_SET_DISP_OFFSET 0xD3
#define SSD1306_CMD_SET_START_LINE 0x40
#define SSD1306_CMD_CHARGE_PUMP 0x8D
#define SSD1306_CMD_MEMORY_ADDR_MODE 0x20
#define SSD1306_CMD_SEG_REMAP 0xA1
#define SSD1306_CMD_COM_OUT_DIRECTION 0xC8
#define SSD1306_CMD_SET_COM_PINS 0xDA
#define SSD1306_CMD_SET_PRECHARGE 0xD9
#define SSD1306_CMD_SET_VCOMH 0xDB
#define SSD1306_CMD_COLUMN_ADDR 0x21
#define SSD1306_CMD_PAGE_ADDR 0x22

/* ========== STATIC VARIABLES ========== */
// I2C bus and device handles
static i2c_master_bus_handle_t bus_handle; // Handle to I2C bus
static i2c_master_dev_handle_t dev_handle; // Handle to SSD1306 device

// Display buffer: stores pixel data for all 8 pages (128x64 display)
// Each page is 8 pixels tall, arranged vertically
// buffer[page][x] = one byte (8 pixels vertically) at column x, page row
static uint8_t display_buffer[OLED_PAGES][OLED_WIDTH];

// Simple 5x7 font (ASCII 32-126)
static const uint8_t font_5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x41, 0x3E}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x3A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x26, 0x49, 0x49, 0x49, 0x32}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x41, 0x22, 0x1C, 0x22, 0x41}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x7F, 0x41, 0x00, 0x00}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x00, 0x41, 0x7F, 0x00, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // }
    {0x10, 0x08, 0x08, 0x10, 0x08}, // ~
};

/**
 * Write command byte to SSD1306 over I2C
 * Command byte format: [0x00 (control byte) | command]
 * The 0x00 control byte indicates this is a command, not display data
 */
static void oled_write_cmd(uint8_t cmd)
{
    uint8_t data[2] = {0x00, cmd}; // Control byte + command byte
    i2c_master_transmit(dev_handle, data, 2, -1);
}

/**
 * Write display data bytes to SSD1306 over I2C
 * Data byte format: [0x40 (control byte) | pixel data]
 * The 0x40 control byte indicates this is display data
 * buf: pointer to pixel data buffer
 * len: number of bytes to send
 */
static void oled_write_data(uint8_t *buf, size_t len)
{
    uint8_t data[256];
    data[0] = 0x40;             // Control byte (0x40 = display data)
    memcpy(&data[1], buf, len); // Copy pixel data after control byte
    i2c_master_transmit(dev_handle, data, len + 1, -1);
}

/**
 * Draw a single character on the display buffer
 * Uses 5x7 pixel font stored in font_5x7[]
 *
 * Parameters:
 *   x: horizontal position (0-127)
 *   y: vertical position (0-63)
 *   c: character to draw (ASCII 32-126)
 *
 * The font data is 5 bytes wide per character.
 * Characters can span across page boundaries, so we handle
 * the vertical wrapping if the character crosses a page boundary.
 */
static void oled_draw_char(int x, int y, char c)
{
    // Bounds checking - character must fit within display
    if (x < 0 || x > OLED_WIDTH - 5 || y < 0 || y > OLED_HEIGHT - 8)
        return;

    // Get font index (ASCII 32-126 maps to array indices 0-94)
    int char_idx = c - 32;
    if (char_idx < 0 || char_idx >= 95)
        return; // Invalid character

    // Calculate which page and bit position within page
    int page = y / 8;    // Which 8-pixel tall page (0-7)
    int bit_pos = y % 8; // Bit position within page (0-7)

    // Draw character (5 pixels wide)
    for (int i = 0; i < 5; i++)
    {
        uint8_t char_data = font_5x7[char_idx][i]; // One column of character

        // Shift character data to align with y position and OR into buffer
        display_buffer[page][x + i] |= (char_data << bit_pos);

        // Handle characters that span across page boundaries
        if (page + 1 < OLED_PAGES && bit_pos > 0)
        {
            display_buffer[page + 1][x + i] |= (char_data >> (8 - bit_pos));
        }
    }
}

/**
 * Draw a text string on the display
 * Automatically wraps to next line if text exceeds display width
 *
 * Each character is 5 pixels wide + 1 pixel spacing = 6 pixels total
 * Lines are 8 pixels tall
 */
static void oled_draw_string(int x, int y, const char *str)
{
    while (*str)
    {
        oled_draw_char(x, y, *str); // Draw one character
        x += 6;                     // Move to next character position (5px + 1px space)

        // Wrap to next line if exceeds screen width
        if (x > OLED_WIDTH - 6)
        {
            x = 0;  // Reset to left edge
            y += 8; // Move down one line
        }
        str++;
    }
}

/**
 * Initialize SSD1306 OLED display over I2C
 *
 * Steps:
 *   1. Configure I2C bus on GPIO22 (SCL), GPIO21 (SDA)
 *   2. Add SSD1306 device to I2C bus (address 0x3C)
 *   3. Send initialization commands to SSD1306
 *   4. Clear display buffer and update OLED
 *
 * Returns: 0 on success, -1 on failure
 */
int oled_init(void)
{
    // Step 1: Configure I2C master bus
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    if (i2c_new_master_bus(&i2c_bus_config, &bus_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create I2C bus");
        return -1;
    }

    // Add device
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    if (i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add I2C device");
        return -1;
    }

    // Step 3: Send initialization commands to SSD1306
    // These commands configure display parameters and enable the display
    oled_write_cmd(SSD1306_CMD_DISPLAY_OFF);      // Turn off while configuring
    oled_write_cmd(SSD1306_CMD_SET_DISP_CLK_DIV); // Set display clock divider
    oled_write_cmd(0x80);
    oled_write_cmd(SSD1306_CMD_SET_MUX_RATIO);         // Set multiplex ratio (duty cycle)
    oled_write_cmd(0x1F);                              // 1/32 duty (for 128x32 display)
    oled_write_cmd(SSD1306_CMD_SET_DISP_OFFSET);       // Set display offset
    oled_write_cmd(0x00);                              // No offset
    oled_write_cmd(SSD1306_CMD_SET_START_LINE | 0x00); // Set start line (top of display)
    oled_write_cmd(SSD1306_CMD_CHARGE_PUMP);           // Enable charge pump for power supply
    oled_write_cmd(0x14);
    oled_write_cmd(SSD1306_CMD_MEMORY_ADDR_MODE);  // Set memory addressing mode
    oled_write_cmd(0x02);                          // Page addressing mode
    oled_write_cmd(SSD1306_CMD_SEG_REMAP | 0x01);  // Remap segment (column) address
    oled_write_cmd(SSD1306_CMD_COM_OUT_DIRECTION); // Set COM output scan direction
    oled_write_cmd(SSD1306_CMD_SET_COM_PINS);      // Set COM pins hardware config
    oled_write_cmd(0x02);                          // COM pins config for 32-pixel display
    oled_write_cmd(SSD1306_CMD_SET_CONTRAST);      // Set contrast (brightness)
    oled_write_cmd(0xCF);                          // Maximum contrast
    oled_write_cmd(SSD1306_CMD_SET_PRECHARGE);     // Set precharge period
    oled_write_cmd(0xF1);
    oled_write_cmd(SSD1306_CMD_SET_VCOMH); // Set V_COMH deselect level
    oled_write_cmd(0x40);
    oled_write_cmd(SSD1306_CMD_NORMAL_DISPLAY); // Normal display (not inverted)

    // Step 4: Clear display and turn on
    oled_clear();                           // Clear all pixels
    oled_write_cmd(SSD1306_CMD_DISPLAY_ON); // Turn on display
    ESP_LOGI(TAG, "OLED initialized successfully");
    return 0;
}

/**
 * Clear the entire display
 * Clears the display buffer and sends all zeros to OLED
 */
void oled_clear(void)
{
    // Clear buffer in RAM
    memset(display_buffer, 0, sizeof(display_buffer));

    // Send clear data to all pages on OLED
    for (int page = 0; page < OLED_PAGES; page++)
    {
        oled_write_cmd(SSD1306_CMD_PAGE_ADDR);
        oled_write_cmd(page);
        oled_write_cmd(page);
        oled_write_cmd(SSD1306_CMD_COLUMN_ADDR);
        oled_write_cmd(0);
        oled_write_cmd(127);

        oled_write_data(display_buffer[page], OLED_WIDTH);
    }
}

/**
 * Display all sensor readings on OLED screen
 * Layout (optimized for 128x32):
 *   Line 1: Temp (left) | Humidity (right)
 *   Line 2: Light Level (left) | Rain (right)
 *   Line 3: Current Time (centered)
 */
void oled_display_sensor_data(const sensor_data_t *data)
{
    // Clear display buffer
    memset(display_buffer, 0, sizeof(display_buffer));

    char line[32];

    // ===== LINE 1: DHT11 Sensor (Temperature & Humidity) =====
    // Display: "Tmp&Hm: 25.3C 60.5%"
    snprintf(line, sizeof(line), "Tmp&Hm: %5.1fC %4.1f%%", data->temperature, data->humidity);
    oled_draw_string(0, 0, line);

    // ===== LINE 2: Light Sensor =====
    // Display: "Light: Bright" or "Light: Dark"
    const char *light_status = data->light_level > 50.0f ? "Bright" : "Dark";
    snprintf(line, sizeof(line), "Light: %s", light_status);
    oled_draw_string(0, 8, line);

    // ===== LINE 3: Rain Sensor =====
    // Display: "Rain: Dry" or "Rain: Wet"
    const char *rain_status = data->rain_detected ? "Wet" : "Dry";
    snprintf(line, sizeof(line), "Rain: %s", rain_status);
    oled_draw_string(0, 16, line);

    // ===== LINE 4: Current Time (centered) =====
    // Display current time in HH:MM:SS format
    snprintf(line, sizeof(line), "%s", data->time_str);
    oled_draw_string(25, 24, line);

    // Send updated buffer to OLED display
    for (int page = 0; page < OLED_PAGES; page++)
    {
        // Set page pointer for this row of pixels
        oled_write_cmd(SSD1306_CMD_PAGE_ADDR);
        oled_write_cmd(page); // Start page
        oled_write_cmd(page); // End page

        // Set column range (full width)
        oled_write_cmd(SSD1306_CMD_COLUMN_ADDR);
        oled_write_cmd(0);   // Start column
        oled_write_cmd(127); // End column

        // Send the pixel data for this page
        oled_write_data(display_buffer[page], OLED_WIDTH);
    }
}

void oled_send_cmd(uint8_t cmd)
{
    oled_write_cmd(cmd);
}

void oled_send_data(uint8_t *data, uint16_t len)
{
    oled_write_data(data, len);
}
