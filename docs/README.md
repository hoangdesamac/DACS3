# DACS3 - Smart Home System

ESP-IDF IoT Smart Home project with ESP32 firmware, Node.js backend, and PostgreSQL database.

## Architecture Overview

```
┌─────────────┐     ESP-NOW     ┌─────────────┐     MQTT      ┌─────────────┐
│  ESP-Node   │◄──────────────►│  ESP32-S3   │──────────────►│  HiveMQ     │
│  (Sensor)   │                │  (Gateway)  │               │  Broker     │
└─────────────┘                └─────────────┘               └──────┬──────┘
                                    │                               │
                                    │                               ▼
                              GPIO (Relay)                  ┌─────────────┐
                                    │                       │  Node.js    │
                                    ▼                       │  Backend    │
                              ┌─────────────┐               │  (Express)  │
                              │  Physical   │               └──────┬──────┘
                              │  Devices   │                      │
                              └─────────────┘                      ▼
                                                            ┌─────────────┐
                                                            │ PostgreSQL  │
                                                            │ (Render)    │
                                                            └─────────────┘
```

## ESP-Node (Sensor Device)

ESP-Node is a remote sensor device that collects environmental data and communicates with the ESP32-S3 gateway via ESP-NOW.

**Sensor Data Collected**:
- Temperature (Celsius)
- Humidity (Percentage)
- Light level (Percentage)
- Soil moisture (Percentage)
- Rain detection (0/1)

**Communication**: ESP-NOW at 868MHz to ESP32-S3 gateway

## Project Structure

```
DACS3/
├── main/                    # ESP-IDF main application
│   └── main.c               # Entry point, GPIO init, main loop
├── components/              # Modular components
│   ├── wifi_manager/        # WiFi station mode with fallback
│   ├── mqtt_manager/        # MQTT client (HiveMQ)
│   ├── espnow_manager/      # ESP-NOW communication with nodes
│   ├── algorithms/          # Placeholder for data processing
│   ├── fsm/                 # Placeholder for state machine
│   ├── memory_utils/        # Placeholder for memory management
│   └── design_patterns/     # Placeholder for patterns
├── backend/                 # Node.js backend
│   ├── server.js            # Express server, MQTT subscriber, Auth
│   └── package.json         # Dependencies
└── docs/                    # Documentation
```

## Authentication

JWT-based authentication is implemented for the REST API.

| Endpoint | Method | Description |
|----------|--------|-------------|
| /api/auth/register | POST | Register new user (email, password) |
| /api/auth/login | POST | Login, returns JWT token |

**Protected Endpoints** (require Bearer token):
- /devices
- /telemetry/:deviceId

## Environment Variables

Create `backend/.env` with:

```bash
DB_URL=postgresql://user:password@host:5432/dbname?sslmode=require
JWT_SECRET=your-secret-key
PORT=3000
```

## Hardware Configuration

| Component | Configuration |
|-----------|----------------|
| ESP32-S3 GPIO | Pin 2 (Relay control) |
| WiFi Primary | SSID: "love her" |
| WiFi Fallback | SSID: "RedmiNote13" |
| ESP-NOW Peer | B0:CB:D8:8A:82:A0 |
| MQTT Broker | broker.hivemq.com |

## Key Features

- **WiFi Manager**: Dual-network support with automatic failover
- **ESP-NOW**: Low-latency sensor data collection from ESP-Nodes
- **MQTT**: Real-time communication via HiveMQ cloud broker
- **GPIO Control**: Relay switching via MQTT commands
- **Backend API**: Express.js REST API for device and telemetry data

## Communication Topics

| Topic | Direction | Purpose |
|-------|-----------|---------|
| DACS3/esp32_to_app | ESP32 -> Backend | Sensor data, relay state |
| DACS3/app_to_esp32 | Backend -> ESP32 | ON/OFF commands |

## API Endpoints

| Endpoint | Method | Auth | Description |
|----------|--------|------|-------------|
| /api/auth/register | POST | No | Register new user |
| /api/auth/login | POST | No | Login, get JWT token |
| /devices | GET | Yes | List all devices and current state |
| /telemetry/:deviceId | GET | Yes | Get last 20 telemetry records |

## Quick Start

### Build ESP32 Firmware

```bash
idf.py build
idf.py flash monitor
```

### Run Backend

```bash
cd backend
npm install
node server.js
```

## Dependencies

### ESP-IDF Components
- esp_wifi
- esp_event
- esp_mqtt
- esp_now
- nvs_flash

### Node.js Dependencies
- express ^5.2.1
- mqtt ^5.15.0
- pg ^8.20.0

## Database Schema

### devices
| Column | Type | Description |
|--------|------|-------------|
| id | VARCHAR(50) | Primary key |
| name | VARCHAR(100) | Device name |
| type | VARCHAR(50) | Device type |
| is_online | BOOLEAN | Online status |
| last_updated | TIMESTAMP | Last update time |

### telemetry
| Column | Type | Description |
|--------|------|-------------|
| id | SERIAL | Primary key |
| device_id | VARCHAR(50) | Foreign key to devices |
| temperature | REAL | Temperature reading |
| humidity | REAL | Humidity reading |
| soil_moisture | REAL | Soil moisture reading |
| light_level | REAL | Light level reading |
| rain_detected | SMALLINT | Rain detection flag |
| relay_state | SMALLINT | Relay state (0/1) |
| created_at | TIMESTAMP | Record timestamp |

## Sensor Data Structure (C)

```c
typedef struct {
    char time_str[16];     // Timestamp string
    float temperature;     // Temperature in Celsius
    float humidity;        // Humidity percentage
    float light_level;     // Light level percentage
    float soil_moisture;   // Soil moisture percentage
    uint8_t rain_detected; // Rain detection (0/1)
} sensor_data_t;
```

## MQTT Payload (ESP32 -> Backend)

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
