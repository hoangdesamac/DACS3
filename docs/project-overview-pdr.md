# DACS3 Smart Home - Product Development Requirements

## 1. Project Overview

### Project Name
DACS3 Smart Home System

### Core Functionality
An IoT-based Smart Home system where ESP32-S3 serves as a gateway that collects sensor data from ESP-Nodes via ESP-NOW, controls a relay via GPIO, and communicates with a Node.js backend via MQTT for data persistence and remote control.

### Target Users
- Home users seeking automated sensor monitoring (temperature, humidity, soil moisture, light, rain)
- Smart garden/plant monitoring applications
- IoT learning and prototyping

---

## 2. System Components

### 2.1 ESP32-S3 Gateway (main/)

**Purpose**: Central hub for sensor data collection and device control

**Key Responsibilities**:
- Initialize and manage WiFi connection with dual-network fallback
- Collect sensor data from ESP-Nodes via ESP-NOW
- Control relay via GPIO pin 2
- Publish sensor data and receive commands via MQTT

**Main Flow**:
1. Initialize GPIO (pin 2 for relay)
2. Connect to WiFi (10s delay to obtain channel)
3. Initialize ESP-NOW
4. Start MQTT client
5. Every 5 seconds: request sensor data from node

### 2.2 WiFi Manager (components/wifi_manager/)

**Purpose**: Manage WiFi station mode with automatic failover

**Specifications**:
| Parameter | Primary Network | Fallback Network |
|-----------|-----------------|------------------|
| SSID | "love her" | "RedmiNote13" |
| Password | "123456789" | "11223344" |

- Maximum retry attempts: 5
- Automatic switch to fallback on disconnect
- Event-based connection handling

### 2.3 ESP-NOW Manager (components/espnow_manager/)

**Purpose**: Low-latency communication with ESP-Nodes

**Peer Configuration**:
| Parameter | Value |
|-----------|-------|
| MAC Address | B0:CB:D8:8A:82:A0 |
| Channel | 0 (auto) |
| Encryption | Disabled |

**Sensor Data Structure** (sensor_data_t):
| Field | Type | Description |
|-------|------|-------------|
| time_str | char[16] | Timestamp |
| temperature | float | Celsius |
| humidity | float | Percentage |
| light_level | float | Percentage |
| soil_moisture | float | Percentage |
| rain_detected | uint8_t | 0/1 flag |

### 2.4 MQTT Manager (components/mqtt_manager/)

**Purpose**: Cloud communication via HiveMQ broker

**Broker Configuration**:
- URI: mqtt://broker.hivemq.com

**Topics**:
| Topic | Direction | Payload |
|-------|-----------|---------|
| DACS3/esp32_to_app | Publish | Sensor data, relay state |
| DACS3/app_to_esp32 | Subscribe | Commands (ON/OFF) |

**Startup Message**:
```json
{"id": "esp32_startup", "isOnline": true}
```

**Sensor Data Payload**:
```json
{
  "id": "device_002",
  "state": "ON|OFF",
  "temp": 30.5,
  "hum": 60.0,
  "soil": 40.0,
  "light": 80.0,
  "rain": 0
}
```

### 2.5 Node.js Backend (backend/)

**Purpose**: Bridge between ESP32 and PostgreSQL, provide REST API

**Server Configuration**:
| Parameter | Value |
|-----------|-------|
| Framework | Express.js 5.2.1 |
| Port | 3000 |

**API Endpoints**:

| Endpoint | Method | Description |
|----------|--------|-------------|
| /devices | GET | List all devices with current state |
| /telemetry/:deviceId | GET | Get last 20 telemetry records |

**MQTT Subscription**: DACS3/esp32_to_app

### 2.6 PostgreSQL Database

**Purpose**: Persistent storage for device states and telemetry history

**Tables**:

```sql
-- devices: Device registry and online status
CREATE TABLE devices (
    id VARCHAR(50) PRIMARY KEY,
    name VARCHAR(100),
    type VARCHAR(50),
    is_online BOOLEAN DEFAULT false,
    last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- telemetry: Time-series sensor data
CREATE TABLE telemetry (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(50) REFERENCES devices(id),
    temperature REAL,
    humidity REAL,
    soil_moisture REAL,
    rain_detected SMALLINT,
    relay_state SMALLINT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

**Default Devices**:
| ID | Name | Type |
|----|------|------|
| device_001 | Đèn phòng khách | LIGHT |
| device_002 | Máy bơm vườn | PUMP |

---

## 3. Functional Requirements

### 3.1 Core Features

| ID | Requirement | Priority |
|----|-------------|----------|
| F1 | ESP32 connects to WiFi automatically | High |
| F2 | ESP32 falls back to secondary WiFi on primary failure | High |
| F3 | ESP32 collects sensor data via ESP-NOW every 5 seconds | High |
| F4 | ESP32 publishes sensor data to MQTT | High |
| F5 | ESP32 controls relay based on MQTT commands | High |
| F6 | Backend subscribes to MQTT and stores data in PostgreSQL | High |
| F7 | REST API provides device list and telemetry history | High |
| F8 | Backend publishes ON/OFF commands to MQTT | Medium |

### 3.2 Data Flow

```
ESP-Node --[ESP-NOW]--> ESP32-S3 --[MQTT]--> HiveMQ
                                              │
                                              ▼
                                        Node.js Backend
                                              │
                                              ▼
                                        PostgreSQL
```

### 3.3 Control Flow

```
App --[HTTP]--> Backend --[MQTT]--> ESP32-S3 --[GPIO]--> Relay
```

---

## 4. Non-Functional Requirements

### 4.1 Performance
- ESP-NOW data request interval: 5 seconds
- WiFi connection timeout: 5 retries
- MQTT reconnection: Automatic

### 4.2 Reliability
- WiFi failover mechanism
- MQTT auto-reconnect
- Graceful handling of missing sensor data

### 4.3 Constraints
- ESP-NOW peer MAC is hardcoded
- PostgreSQL connection string is hardcoded (backend)
- JWT authentication implemented on REST API (token expires 7d)

---

## 5. Placeholder Components

These components exist but contain only empty stubs (Phase 4 - not yet implemented):

| Component | Purpose | Status |
|-----------|---------|--------|
| algorithms | Data processing algorithms | Placeholder |
| fsm | State machine logic | Placeholder |
| memory_utils | Memory management utilities | Placeholder |
| design_patterns | Common design patterns | Placeholder | |

---

## 6. Acceptance Criteria

### 6.1 WiFi Manager
- [ ] Connects to primary WiFi within retry limit
- [ ] Switches to fallback WiFi after 5 failed attempts
- [ ] Logs connection events

### 6.2 ESP-NOW Manager
- [ ] Initializes successfully with peer B0:CB:D8:8A:82:A0
- [ ] Sends GET_DATA command on request
- [ ] Receives and logs sensor data

### 6.3 MQTT Manager
- [ ] Connects to HiveMQ broker
- [ ] Subscribes to DACS3/app_to_esp32
- [ ] Publishes startup message on connect
- [ ] Publishes sensor data on receive
- [ ] Controls GPIO relay based on commands

### 6.4 Backend
- [x] Connects to PostgreSQL
- [x] Creates tables on startup
- [x] Subscribes to MQTT topic
- [x] Serves /devices endpoint
- [x] Serves /telemetry/:deviceId endpoint
- [x] Implements JWT authentication (/api/auth/register, /api/auth/login)

---

## 7. Known Issues / Technical Debt

1. **Hardcoded credentials**: WiFi SSIDs/passwords, peer MAC, DB connection string in backend
2. **JWT implemented**: REST API has JWT auth (7d expiry), MQTT still has no auth
3. **Placeholder components**: algorithms, fsm, memory_utils, design_patterns are empty (Phase 4)
4. **No error recovery**: ESP-NOW failure does not trigger retry
5. **No unit tests**: No test suite exists

---

## 8. Future Considerations

- Add authentication (JWT, MQTT TLS)
- Implement OTA firmware updates
- Add device discovery mechanism
- Implement command queuing for offline ESP32
- Add data validation and edge detection
