# DACS3 OLED Sensor Display - Hardware Setup Guide

## Pin Configuration

### OLED Display (SSD1306 128x64 I2C)
```
ESP32 Dev Kit Pin    ->    OLED Pin
GPIO22 (SCL)         ->    SCL
GPIO21 (SDA)         ->    SDA
3.3V                 ->    VCC
GND                  ->    GND
```

### DHT11 Temperature & Humidity Sensor
```
ESP32 Dev Kit Pin    ->    DHT11 Pin
GPIO14               ->    Data (middle pin)
3.3V                 ->    VCC (+)
GND                  ->    GND (-)
```
Note: Add a 10kΩ pull-up resistor between GPIO14 and 3.3V

### Light Sensor (Digital LM393)
```
ESP32 Dev Kit Pin    ->    Sensor Pin
GPIO33               ->    Digital Output (DO)
3.3V                 ->    VCC
GND                  ->    GND
```
Logic in code: DO LOW = Bright (100%), DO HIGH = Dark (0%)

### Rain Sensor (Digital)
```
ESP32 Dev Kit Pin    ->    Sensor Pin
GPIO32               ->    Digital Output (DO)
3.3V                 ->    VCC
GND                  ->    GND
```
Logic in code: DO LOW = Wet, DO HIGH = Dry (active LOW)
Note: GPIO5 is used by the motor limit switch in this project.

## OLED I2C Address

The code uses I2C address **0x3C**. If your OLED uses a different address, update:
- `OLED_I2C_ADDR` in `components/oled_display/oled_display.c` (line ~70)

Common addresses: 0x3C, 0x3D

## Building & Flashing

```bash
cd /home/hoangdesamac/projects/DACS3

# Build the project
idf.py build

# Flash to ESP32
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor
```

## Features

✅ OLED Display (128x64)
  - Temperature & Humidity (DHT11)
  - Light Level
  - Rain Detection
  - Current Time

✅ Sensor Readings
  - DHT11: Temperature & Humidity
  - Digital GPIO: Light Level, Rain Detection

✅ ESP-NOW Communication
  - Sends sensor data to peer device periodically

## Calibration

### Light / Rain Sensor Threshold
For LM393 modules, adjust the onboard potentiometer to tune switching threshold.

1. Observe monitor logs for `DO` raw values.
2. Set target behavior:
  - Light: DO LOW when bright.
  - Rain: DO LOW when wet.
3. Turn potentiometer until raw DO transitions at desired condition.

### OLED Display Position
If text appears in wrong positions, adjust Y-coordinates in:
`components/oled_display/oled_display.c` (lines ~280-300)
- Y=0: Top line
- Y=10: Second line
- Y=20: Third line
- Y=50: Bottom line

## Troubleshooting

**OLED not showing:** 
- Check I2C address (use `i2cdetect` command if available)
- Swap SCL/SDA pins
- Add 10kΩ pull-up resistors to SCL/SDA

**No sensor readings:**
- Check GPIO pin numbers in `.c` files
- Verify ADC has power (3.3V)
- Ensure DHT11 pull-up resistor is connected

**ESP-NOW not sending:**
- Check peer MAC address matches the other device
- Verify WiFi/BLE is not interfering
