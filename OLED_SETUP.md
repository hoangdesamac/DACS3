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

### DHT22 Temperature & Humidity Sensor
```
ESP32 Dev Kit Pin    ->    DHT22 Pin
GPIO4                ->    Data (middle pin)
3.3V                 ->    VCC (+)
GND                  ->    GND (-)
```
Note: Add a 10kΩ pull-up resistor between GPIO4 and 3.3V

### Soil Moisture Sensor (Analog)
```
ESP32 Dev Kit Pin    ->    Sensor Pin
GPIO36 (ADC1_CH0)    ->    AO (Analog Output)
A0                   ->    Digital Output (optional)
3.3V                 ->    VCC
GND                  ->    GND
```

### Light Level Sensor (LDR with voltage divider)
```
ESP32 Dev Kit Pin    ->    Sensor Pin
GPIO39 (ADC1_CH3)    ->    Voltage divider output
3.3V                 ->    VCC
GND                  ->    GND
```
Voltage divider: 10kΩ resistor in series with LDR to GND

### Rain Sensor (Digital)
```
ESP32 Dev Kit Pin    ->    Sensor Pin
GPIO5                ->    Digital Output (pin 4)
3.3V                 ->    VCC
GND                  ->    GND
```
Pull-up enabled in code

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
  - Temperature & Humidity (DHT22)
  - Soil Moisture Percentage
  - Light Level
  - Rain Detection
  - Current Time

✅ Sensor Readings
  - DHT22: Temperature & Humidity
  - Analog ADC: Soil Moisture, Light Level
  - Digital GPIO: Rain Detection

✅ ESP-NOW Communication
  - Sends sensor data to peer device periodically

## Calibration

### Soil Moisture Sensor
Edit values in `components/sensors/sensors.c` (lines ~120):
```c
int dry_value = 4095;   // Adjust based on your dry reading
int wet_value = 1500;   // Adjust based on your wet reading
```

1. Put sensor in dry soil → note the ADC value
2. Put sensor in water → note the ADC value
3. Update these constants

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
- Ensure DHT22 pull-up resistor is connected

**ESP-NOW not sending:**
- Check peer MAC address matches the other device
- Verify WiFi/BLE is not interfering
