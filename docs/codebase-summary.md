# DACS3 - Codebase Summary

## Project Overview

Smart Home IoT system with ESP32-S3 gateway collecting sensor data via ESP-NOW and communicating via MQTT to a Node.js backend with PostgreSQL storage.

---

## Directory Structure

```
DACS3/
├── CMakeLists.txt              # ESP-IDF project configuration
├── README.md                    # Minimal existing readme
├── sdkconfig                   # ESP-IDF SDK configuration
├── sdkconfig.old               # Backup config
├── main/                       # Main ESP-IDF application
│   ├── CMakeLists.txt
│   └── main.c                  # Entry point
├── components/                # Modular ESP-IDF components
│   ├── wifi_manager/           # WiFi station with failover
│   │   ├── CMakeLists.txt
│   │   ├── wifi_manager.c      # Implementation
│   │   └── include/
│   │       └── wifi_manager.h
│   ├── mqtt_manager/           # MQTT client (HiveMQ)
│   │   ├── CMakeLists.txt
│   │   ├── mqtt_manager.c
│   │   └── include/
│   │       └── mqtt_manager.h
│   ├── espnow_manager/         # ESP-NOW communication
│   │   ├── CMakeLists.txt
│   │   ├── espnow_manager.c
│   │   └── include/
│   │       └── espnow_manager.h
│   ├── algorithms/            # Placeholder (empty func stub)
│   │   ├── CMakeLists.txt
│   │   ├── algorithms.c
│   │   └── include/
│   │       └── algorithms.h
│   ├── fsm/                   # Placeholder (empty func stub)
│   │   ├── CMakeLists.txt
│   │   ├── fsm.c
│   │   └── include/
│   │       └── fsm.h
│   ├── memory_utils/           # Placeholder (empty func stub)
│   │   ├── CMakeLists.txt
│   │   ├── memory_utils.c
│   │   └── include/
│   │       └── memory_utils.h
│   └── design_patterns/        # Placeholder (empty func stub)
│       ├── CMakeLists.txt
│       ├── design_patterns.c
│       └── include/
│           └── design_patterns.h
├── backend/                   # Node.js backend server
│   ├── package.json           # Dependencies (express, mqtt, pg)
│   ├── package-lock.json
│   └── server.js             # Express server with MQTT & PostgreSQL
└── docs/                     # Documentation (this directory)
```

---

## Main Application (main/main.c)

**File**: `main/main.c` (49 lines)

**Responsibilities**:
- Initialize GPIO pin 2 for relay control
- Initialize WiFi station
- Initialize ESP-NOW
- Start MQTT client
- Main loop: request sensor data every 5 seconds

**Key Flow**:
```c
void app_main(void) {
    gpio_init(GPIO_PIN_RELAY);     // Pin 2
    wifi_init_sta();               // Connect WiFi
    vTaskDelay(10000ms);           // Wait for channel
    init_s3_espnow();              // Setup ESP-NOW
    mqtt_app_start();              // Start MQTT

    while(1) {
        request_data_from_node();
        vTaskDelay(5000ms);
    }
}
```

---

## Components Detail

### WiFi Manager

**Files**: `components/wifi_manager/`

**Public API** (`wifi_manager.h`):
```c
void wifi_init_sta(void);
```

**Configuration**:
- Primary: SSID "love her", PASS "123456789"
- Fallback: SSID "RedmiNote13", PASS "11223344"
- Max retry: 5

**Internal**:
- `event_handler()` - handles WIFI_EVENT_STA_START, WIFI_EVENT_STA_DISCONNECTED, IP_EVENT_STA_GOT_IP
- `current_network` - tracks which network (1 or 2)
- `s_retry_num` - retry counter

---

### MQTT Manager

**Files**: `components/mqtt_manager/`

**Public API** (`mqtt_manager.h`):
```c
void mqtt_app_start(void);
void mqtt_manager_publish_sensor_data(const void *data_ptr);
```

**Configuration**:
- Broker: mqtt://broker.hivemq.com
- Subscribe: DACS3/app_to_esp32
- Publish: DACS3/esp32_to_app

**Global Variable**:
```c
esp_mqtt_client_handle_t global_mqtt_client;
```

**Event Handler** (`mqtt_event_handler`):
- MQTT_EVENT_CONNECTED: subscribe, publish startup
- MQTT_EVENT_DISCONNECTED: log warning
- MQTT_EVENT_DATA: parse JSON, control GPIO
- MQTT_EVENT_ERROR: log error

**JSON Payload Processing**:
```c
// Expected format:
// {"id": "device_xxx", "command": "ON|OFF"}
if (strcmp(command, "ON") == 0) gpio_set_level(GPIO_PIN_RELAY, 1);
if (strcmp(command, "OFF") == 0) gpio_set_level(GPIO_PIN_RELAY, 0);
```

---

### ESP-NOW Manager

**Files**: `components/espnow_manager/`

**Public API** (`espnow_manager.h`):
```c
void init_s3_espnow(void);
void request_data_from_node(void);
```

**Peer Configuration**:
```c
static uint8_t node_mac[] = {0xB0, 0xCB, 0xD8, 0x8A, 0x82, 0xA0};
// channel = 0 (auto)
// encrypt = false
```

**Sensor Data Structure** (sensor_data_t - 33 bytes total):
```c
#pragma pack(push, 1)
typedef struct {
    char time_str[16];     // Timestamp string
    float temperature;     // Temperature in Celsius
    float humidity;        // Humidity percentage
    float light_level;     // Light level percentage (C struct)
    float soil_moisture;   // Soil moisture percentage
    uint8_t rain_detected; // Rain detection (0/1)
} sensor_data_t;
#pragma pack(pop)
```

**MQTT Payload** (sent as JSON - field name is `light` not `light_level`):
```json
{"id":"device_002","state":"ON","temp":30.5,"hum":60.0,"soil":40.0,"light":80.0,"rain":0}
```

**Note**: The C struct uses `light_level` but the MQTT JSON payload uses `light` as the field name.

**Send Callback** (`s3_on_mac_send`): logs send success/failure

**Receive Callback** (`s3_on_mac_recv`):
- Validates data_len >= 16
- Copies data to sensor_data_t
- Logs all sensor values
- Calls `mqtt_manager_publish_sensor_data()`

---

## Backend (backend/server.js)

**File**: `backend/server.js` (267 lines)

**Dependencies**:
- express: ^5.2.1
- mqtt: ^5.15.0
- pg: ^8.20.0
- bcrypt: ^5.x (password hashing)
- jsonwebtoken: ^9.x (JWT auth)

**Configuration**:
- Port: 3000
- DB: postgresql://... (Render hosted)
- MQTT: mqtt://broker.hivemq.com
- JWT Secret: hardcoded (should move to .env)

**Database Schema** (auto-created):
```sql
devices (id, name, type, is_online, last_updated)
telemetry (id, device_id, temperature, humidity, soil_moisture, light_level, rain_detected, relay_state, created_at)
users (id, email, password_hash, created_at)
```

**MQTT Handler**:
- Subscribe: DACS3/esp32_to_app
- On message: parse payload, upsert device, insert telemetry

**API Endpoints**:

| Method | Path | Handler | Auth | Description |
|--------|------|---------|------|-------------|
| POST | /api/auth/register | `register()` | No | User registration |
| POST | /api/auth/login | `login()` | No | User login, returns JWT |
| GET | /devices | `getDevices()` | No* | List all devices |
| GET | /telemetry/:deviceId | `getTelemetry()` | No* | Last 20 telemetry records |

*Note: Current implementation does not enforce auth middleware on /devices and /telemetry endpoints

---

## Placeholder Components

All contain empty `func()` stubs:

| Component | Header | Status |
|-----------|--------|--------|
| algorithms | algorithms.h | Empty |
| fsm | fsm.h | Empty |
| memory_utils | memory_utils.h | Empty |
| design_patterns | design_patterns.h | Empty |

---

## File Statistics

| Path | Lines | Purpose |
|------|-------|---------|
| main/main.c | ~49 | Main app entry |
| components/wifi_manager/wifi_manager.c | ~108 | WiFi handling |
| components/mqtt_manager/mqtt_manager.c | ~146 | MQTT client |
| components/espnow_manager/espnow_manager.c | ~73 | ESP-NOW comms |
| backend/server.js | 267 | Express server + Auth |
| **Total C/JS** | ~643 | |

---

## Communication Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                        DATA FLOW                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ESP-Node    ESP-NOW    ESP32-S3    MQTT    HiveMQ    Backend   │
│    │            │           │         │        │         │      │
│    │───────────►│         │         │        │         │      │
│    │  sensor    │         │         │        │         │      │
│    │   data     │         │         │        │         │      │
│    │            │         │         │        │         │      │
│    │            │─────────►│         │        │         │      │
│    │            │  parse   │         │        │         │      │
│    │            │         │─────────►│        │         │      │
│    │            │         │ publish  │        │         │      │
│    │            │         │          │────────►│         │      │
│    │            │         │          │  forward │        │      │
│    │            │         │          │         │────────►│      │
│    │            │         │          │         │  store  │      │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                        CONTROL FLOW                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│    App      HTTP      Backend    MQTT    ESP32-S3    GPIO      │
│     │        │          │         │         │          │        │
│     │───────►│          │         │         │          │        │
│     │  GET   │          │         │         │          │        │
│     │ devices│          │         │         │          │        │
│     │        │          │         │         │          │        │
│     │        │          │         │         │          │        │
│     │◄───────│          │         │         │          │        │
│     │  JSON  │          │         │         │          │        │
│     │        │          │         │         │          │        │
│     │        │          │         │         │          │        │
│     │───────►│          │         │         │          │        │
│     │  ON    │          │         │         │          │        │
│     │        │──────────►│        │         │          │        │
│     │        │ publish   │         │         │          │        │
│     │        │           │─────────►│        │          │        │
│     │        │           │  MQTT    │        │          │        │
│     │        │           │          │────────►│          │        │
│     │        │           │          │ receive │        │        │
│     │        │           │          │         │────────►│        │
│     │        │           │          │         │  GPIO   │        │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```
