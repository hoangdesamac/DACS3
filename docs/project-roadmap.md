# DACS3 - Project Roadmap

## Overview

Living document tracking project phases, milestones, and progress for the DACS3 Smart Home system.

**Last Updated**: 2026-04-05

---

## Phase Status Summary

| Phase | Name | Status | Progress |
|-------|------|--------|----------|
| 1 | Core Infrastructure | Completed | 100% |
| 2 | Communication Layer | Completed | 100% |
| 3 | Backend Integration | Completed | 100% |
| 4 | Enhancements & hardening | In Progress | 20% |

---

## Phase 1: Core Infrastructure

**Status**: Completed

### Milestones

| ID | Milestone | Status | Date |
|----|----------|--------|------|
| 1.1 | ESP-IDF project setup | Done | - |
| 1.2 | GPIO relay control | Done | - |
| 1.3 | WiFi station mode | Done | - |
| 1.4 | Component CMake structure | Done | - |

### Deliverables

- [x] ESP-IDF project with CMakeLists.txt
- [x] Main application entry point (main.c)
- [x] GPIO pin 2 configured for relay
- [x] Component directory structure

### Notes
Basic ESP32-S3 bare metal setup with GPIO control established.

---

## Phase 2: Communication Layer

**Status**: Completed

### Milestones

| ID | Milestone | Status | Date |
|----|----------|--------|------|
| 2.1 | WiFi manager with fallback | Done | - |
| 2.2 | ESP-NOW manager | Done | - |
| 2.3 | MQTT manager (HiveMQ) | Done | - |
| 2.4 | Main loop integration | Done | - |

### Deliverables

- [x] wifi_manager: Dual WiFi support with auto-failover
- [x] espnow_manager: ESP-NOW peer communication
- [x] mqtt_manager: MQTT client with subscribe/publish
- [x] Main loop: 5-second polling cycle

### Notes
All three communication pathways operational: WiFi, ESP-NOW, MQTT.

---

## Phase 3: Backend Integration

**Status**: Completed

### Milestones

| ID | Milestone | Status | Date |
|----|----------|--------|------|
| 3.1 | Node.js Express server | Done | - |
| 3.2 | PostgreSQL connection | Done | - |
| 3.3 | MQTT subscriber (backend) | Done | - |
| 3.4 | REST API endpoints | Done | - |

### Deliverables

- [x] backend/server.js: Express.js server on port 3000
- [x] Database schema (devices, telemetry)
- [x] MQTT subscription: DACS3/esp32_to_app
- [x] API: GET /devices
- [x] API: GET /telemetry/:deviceId

### Notes
Backend successfully bridges ESP32 and PostgreSQL. Data flows from ESP-Node -> ESP32 -> HiveMQ -> Backend -> PostgreSQL.

---

## Phase 4: Enhancements & Hardening

**Status**: Pending

### Milestones

| ID | Milestone | Priority | Status | Owner |
|----|----------|----------|--------|-------|
| 4.1 | Remove hardcoded credentials | High | Pending | backend-dev |
| 4.2 | Add API authentication | High | **Done** | - |
| 4.3 | Implement placeholder components | Medium | Pending | firmware-dev |
| 4.4 | Add unit tests | Medium | Pending | QA |
| 4.5 | OTA firmware updates | Low | Pending | firmware-dev |
| 4.6 | ESP-NODE discovery | Low | Pending | firmware-dev |

### 4.1 Remove Hardcoded Credentials

**Priority**: High

**Tasks**:
- Move WiFi SSID/password to NVS
- Move ESP-NOW peer MAC to NVS
- Move DB URL to environment variable
- Add .env.example template

**Impact**: Security improvement

---

### 4.2 Add API Authentication

**Priority**: High

**Tasks**:
- Add JWT authentication to Express
- Add API key middleware
- Protect /devices and /telemetry endpoints

**Impact**: Prevents unauthorized access

---

### 4.3 Implement Placeholder Components

**Priority**: Medium

**Components**:

| Component | Purpose | Complexity |
|-----------|---------|------------|
| algorithms | Data processing, filtering | Medium |
| fsm | Device state machine | Medium |
| memory_utils | NVS flash management | Low |
| design_patterns | Common patterns | Low |

**Tasks**:
- Define FSM states (ON, OFF, AUTO, ERROR)
- Implement moving average for sensor data
- Add watchdog timer utilities

---

### 4.4 Add Unit Tests

**Priority**: Medium

**ESP-IDF Tests**:
- WiFi connection test
- MQTT publish/subscribe test
- GPIO toggle test
- ESP-NOW send/receive test

**Node.js Tests**:
- API endpoint tests (Jest + Supertest)
- MQTT message parsing tests
- Database operation tests

---

### 4.5 OTA Firmware Updates

**Priority**: Low

**Tasks**:
- Configure ESP32 OTA
- Add update endpoint to backend
- Implement rollback mechanism

---

### 4.6 ESP-NODE Discovery

**Priority**: Low

**Tasks**:
- Replace hardcoded peer MAC
- Implement broadcast peer discovery
- Add peer management (add/remove)

---

## Technical Debt

| Item | Severity | Owner | Description |
|------|----------|-------|-------------|
| Hardcoded WiFi credentials | High | backend-dev | Exposed in wifi_manager.c |
| Hardcoded DB URL | High | backend-dev | Exposed in server.js |
| No MQTT auth | Medium | backend-dev | Insecure communication |
| API auth not enforced on endpoints | Medium | backend-dev | /devices, /telemetry unprotected |
| Empty placeholder components | Low | firmware-dev | algorithms, fsm, memory_utils, design_patterns |
| No error recovery (ESP-NOW) | Medium | firmware-dev | Single failure point |
| No unit tests | Medium | QA | Quality risk |

---

## Future Considerations

### Potential Features

| Feature | Description | Complexity |
|---------|-------------|------------|
| Multi-node support | Multiple ESP-Nodes | High |
| Web dashboard | Browser-based UI | Medium |
| Mobile app | Native iOS/Android | High |
| Rules engine | IFTTT-style automation | Medium |
| Notifications | Push alerts | Low |
| Data export | CSV/JSON export | Low |

### Architecture Changes

| Change | Rationale |
|--------|-----------|
| Message queue | Decouple ESP32 from backend |
| Time-series DB | Better telemetry storage |
| WebSocket | Real-time frontend updates |
| Docker | Containerized deployment |

---

## Changelog

| Date | Change |
|------|--------|
| 2026-04-05 | Initial roadmap created |
| 2026-04-05 | Phase 1-3 marked complete |
| 2026-04-05 | Phase 4 outlined |
| 2026-04-06 | Phase 4: Auth marked done, tech debt owners assigned |
