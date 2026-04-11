# DACS3 - System Architecture

## Overview

The DACS3 Smart Home system is a 3-tier IoT architecture:

```
┌─────────────────────────────────────────────────────────────────┐
│                        TIER 1: SENSOR LAYER                     │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                      ESP-Node                             │   │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐    │   │
│  │  │ Temp/   │  │ Humidity│  │  Light  │  │  Soil   │    │   │
│  │  │ Humidity│  │  Sensor │  │  Sensor │  │Moisture │    │   │
│  │  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘    │   │
│  │       └────────────┴───────────┴───────────┘          │   │
│  │                          │                             │   │
│  │                   ┌──────┴───────┐                     │   │
│  │                   │  MCU (ESP32) │                     │   │
│  │                   └──────┬───────┘                     │   │
│  └─────────────────────────│───────────────────────────────┘   │
│                              │ ESP-NOW (868MHz)                   │
└──────────────────────────────│───────────────────────────────────┘
                               │
┌──────────────────────────────│───────────────────────────────────┐
│                        TIER 2: GATEWAY LAYER                    │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                    ESP32-S3 (Gateway)                     │   │
│  │                                                          │   │
│  │  ┌─────────────┐   ┌─────────────┐   ┌─────────────┐  │   │
│  │  │ WiFi Manager│   │ESP-NOW Mgr  │   │ MQTT Manager│  │   │
│  │  └──────┬──────┘   └──────┬──────┘   └──────┬──────┘  │   │
│  │         │                 │                 │          │   │
│  │  ┌──────┴─────────────────┴─────────────────┴──────┐    │   │
│  │  │                    MAIN APP                     │    │   │
│  │  │  • GPIO Control (Pin 2)                         │    │   │
│  │  │  • Data Aggregation                             │    │   │
│  │  │  • Command Routing                              │    │   │
│  │  └──────┬─────────────────┬─────────────────┬──────┘    │   │
│  │         │                 │                 │            │   │
│  │    ┌────┴────┐        ┌────┴────┐       ┌────┴────┐      │   │
│  │    │  WiFi  │        │ ESP-NOW │       │   MQTT  │      │   │
│  │    │Station │        │ (Recv)  │       │ (Publish│      │   │
│  │    └────┬───┘        └─────────┘       └────┬────┘      │   │
│  └─────────│───────────────────────────────────│────────────┘   │
│            │                                     │                │
└────────────│─────────────────────────────────────│────────────────┘
             │ MQTT                                  │
             │ (WiFi)                               │
┌────────────│─────────────────────────────────────│────────────────┐
│            │                        TIER 3: CLOUD LAYER            │
│            │                                     │                   │
│      ┌─────┴─────┐                     ┌───────┴───────┐           │
│      │  HiveMQ   │                     │   Node.js      │           │
│      │  Broker   │◄───────────────────│   Backend      │           │
│      │           │       MQTT         │   (Express)    │           │
│      └─────┬─────┘                     └───────┬───────┘           │
│            │                                     │                   │
│            │                                     │  REST API         │
│            │                                     │                   │
│      ┌─────┴─────┐                               │                   │
│      │  MQTT     │                               │                   │
│      │ Subscribe│                               │                   │
│      └──────────┘                               │                   │
│                                                 ▼                   │
│                                    ┌───────────────────┐             │
│                                    │   PostgreSQL      │             │
│                                    │   (Render DB)    │             │
│                                    └───────────────────┘             │
└─────────────────────────────────────────────────────────────────┘
```

---

## Component Interactions

### Data Flow: Sensor to Cloud

```
ESP-Node                          ESP32-S3                          HiveMQ
   │                                 │                                 │
   │◄─────── ESP-NOW Request ────────│                                 │
   │                                 │                                 │
   │──────── ESP-NOW Response ──────►│                                 │
   │   (sensor_data_t struct)       │                                 │
   │                                 │                                 │
   │                                 │──── MQTT Publish ──────────────►│
   │                                 │   Topic: DACS3/esp32_to_app     │
   │                                 │                                 │
   │                                 │                        Backend  │
   │                                 │                                 │
   │                                 │◄─── MQTT Subscribe ─────────────│
   │                                 │   Topic: DACS3/esp32_to_app     │
   │                                 │                                 │
   │                                 │                    ┌────────────┴───┐
   │                                 │                    │ Parse JSON     │
   │                                 │                    │ Upsert device  │
   │                                 │                    │ Insert telemetry│
   │                                 │                    └────────────┬───┘
   │                                 │                                 │
   │                                 │                         PostgreSQL
   │                                 │                                 │
   │                                 │                                 ▼
   │                                 │                          ┌─────────┐
   │                                 │                          │ devices  │
   │                                 │                          │telemetry│
   │                                 │                          └─────────┘
```

### Control Flow: Cloud to Device

```
App                          Backend                      HiveMQ                    ESP32-S3
 │                             │                            │                          │
 │─── GET /devices ───────────►│                            │                          │
 │                             │                            │                          │
 │◄── JSON device list ────────│                            │                          │
 │                             │                            │                          │
 │                             │                            │                          │
 │─── HTTP Command ───────────►│                            │                          │
 │   {"id":"device_002",       │                            │                          │
 │    "command":"ON"}           │                            │                          │
 │                             │──── MQTT Publish ──────────►│                          │
 │                             │   Topic: DACS3/app_to_esp32│                          │
 │                             │                            │──── MQTT Receive ────────►│
 │                             │                            │   Parse JSON             │
 │                             │                            │   Extract command        │
 │                             │                            │                          │
 │                             │                            │──── GPIO Toggle ─────────►│
 │                             │                            │   Pin 2: 0 → 1           │
 │                             │                            │                          │
 │                             │◄── MQTT Publish ───────────│──── Relay ON             │
 │                             │   Topic: DACS3/esp32_to_app│                          │
 │                             │   {"id":"device_002",      │                          │
 │                             │    "state":"ON"}           │                          │
 │                             │                            │                          │
 │◄── HTTP 200 ────────────────│                            │                          │
```

---

## Component Layer Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        APPLICATION LAYER                      │
├─────────────────────────────────────────────────────────────┤
│  main/main.c                                                 │
│  ├── gpio_init()           GPIO setup (Pin 2)                │
│  ├── wifi_init_sta()       WiFi connection                   │
│  ├── init_s3_espnow()       ESP-NOW setup                     │
│  ├── mqtt_app_start()       MQTT client                       │
│  └── while(1) loop          Main control loop                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                       COMPONENT LAYER                         │
├───────────────┬─────────────────┬───────────────────────────┤
│ WiFi Manager   │  MQTT Manager   │  ESP-NOW Manager          │
├───────────────┼─────────────────┼───────────────────────────┤
│ wifi_manager.c│ mqtt_manager.c  │ espnow_manager.c           │
│               │                 │                            │
│ Functions:    │ Functions:      │ Functions:                  │
│ • wifi_init   │ • mqtt_app_start│ • init_s3_espnow           │
│ • event_handler│ • mqtt_event_h  │ • request_data_from_node   │
│               │ • mqtt_publish  │ • s3_on_mac_send (cb)      │
│               │                 │ • s3_on_mac_recv (cb)      │
│               │                 │                            │
│ Dependency:   │ Dependencies:   │ Dependencies:               │
│ • esp_wifi    │ • esp_mqtt      │ • esp_now                   │
│ • nvs_flash   │ • cJSON         │ • esp_mac                   │
└───────────────┴─────────────────┴───────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    ESP-IDF HARDWARE LAYER                     │
├─────────────────────────────────────────────────────────────┤
│  WiFi Radio          ESP-NOW Radio        GPIO Subsystem     │
│  ───────────         ─────────────        ─────────────     │
│  • 802.11 b/g/n      • 868MHz             • Pin 2 (Relay)    │
│  • Station mode      • P2P comm                                   │
└─────────────────────────────────────────────────────────────┘
```

---

## Backend Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     NODE.JS BACKEND                          │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌───────────────┐   ┌───────────────┐   ┌─────────────┐  │
│  │   Express     │   │     MQTT      │   │    pg       │  │
│  │   REST API    │   │   Subscriber  │   │   Pool      │  │
│  │               │   │               │   │             │  │
│  │ GET /devices  │   │ Subscribe:     │   │ PostgreSQL  │  │
│  │ GET /telemetry│   │ esp32_to_app   │   │ Connection  │  │
│  └───────┬───────┘   └───────┬───────┘   └──────┬──────┘  │
│          │                   │                   │          │
│          └───────────────────┴───────────────────┘          │
│                              │                               │
│                    ┌─────────┴─────────┐                    │
│                    │   Message Handler   │                    │
│                    │   • Parse JSON      │                    │
│                    │   • Upsert device   │                    │
│                    │   • Insert telemetry│                    │
│                    └─────────────────────┘                    │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     POSTGRESQL DATABASE                       │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│   devices                      telemetry                    │
│   ───────                      ──────────                    │
│   id (PK)                      id (PK)                       │
│   name                         device_id (FK)               │
│   type                         temperature                   │
│   is_online                    humidity                       │
│   last_updated                 soil_moisture                 │
│                                rain_detected                 │
│                                relay_state                   │
│                                created_at                    │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## Hardware Configuration

### ESP32-S3 Gateway

| Pin | Function |备注 |
|-----|----------|-----|
| GPIO 2 | Relay Control | Output, active high |
| WiFi | Station Mode | 802.11 b/g/n |
| ESP-NOW | Peer Communication | 868MHz, B0:CB:D8:8A:82:A0 |

### WiFi Configuration

| Parameter | Primary | Fallback |
|-----------|---------|----------|
| SSID | "love her" | "RedmiNote13" |
| Password | "123456789" | "11223344" |
| Max Retry | 5 | - |

**Note**: WiFi credentials are hardcoded in `components/wifi_manager/wifi_manager.c`. Should move to NVS encrypted storage.

### MQTT Configuration

| Parameter | Value |
|-----------|-------|
| Broker | mqtt://broker.hivemq.com |
| Subscribe Topic | DACS3/app_to_esp32 |
| Publish Topic | DACS3/esp32_to_app |
| QoS | 0 |

---

## Data Structures

### sensor_data_t (ESP-NOW Protocol)

```c
#pragma pack(push, 1)
typedef struct {
    char     time_str[16];     // "YYYY-MM-DD HH:MM:SS"
    float    temperature;     // Celsius
    float    humidity;         // Percentage
    float    light_level;      // Percentage
    float    soil_moisture;    // Percentage
    uint8_t  rain_detected;    // 0 or 1
} sensor_data_t;
#pragma pack(pop)
// Total: 16 + 4*4 + 1 = 33 bytes
```

### MQTT Payloads

**ESP32 -> Backend (sensor data)**:
```json
{
  "id": "device_002",
  "state": "ON",
  "temp": 30.5,
  "hum": 60.0,
  "soil": 40.0,
  "light": 80.0,
  "rain": 0
}
```

**Note**: The `light` field in MQTT JSON maps to `light_level` in the C struct and database column.

**Backend -> ESP32 (command)**:
```json
{
  "id": "device_002",
  "command": "ON"
}
```

---

## Database Schema

```sql
-- Device registry
CREATE TABLE devices (
    id VARCHAR(50) PRIMARY KEY,
    name VARCHAR(100),
    type VARCHAR(50),
    is_online BOOLEAN DEFAULT false,
    last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Time-series telemetry
CREATE TABLE telemetry (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(50) REFERENCES devices(id),
    temperature REAL,
    humidity REAL,
    soil_moisture REAL,
    light_level REAL,
    rain_detected SMALLINT,
    relay_state SMALLINT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

**Note**: The ESP32 C struct uses `light_level` but MQTT JSON payload uses `light` as the field name. The backend maps `payload.light` to the `light_level` column.

---

## Timing Specifications

| Event | Interval | Notes |
|-------|----------|-------|
| WiFi init delay | 10 seconds | Wait for channel acquisition |
| ESP-NOW data request | 5 seconds | Continuous polling loop |
| MQTT reconnect | Auto | ESP-IDF MQTT library handles |
| DB upsert | On message | Real-time sync |
