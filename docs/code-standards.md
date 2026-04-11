# DACS3 - Code Standards

## Overview

ESP-IDF IoT project with C firmware (ESP32-S3) and JavaScript (Node.js backend). Standards ensure maintainability and consistency across the codebase.

---

## ESP-IDF C Code Standards

### File Organization

| Pattern | Usage |
|---------|-------|
| `*.c` | Implementation files |
| `*.h` | Header files in `include/` subdirectory |
| `CMakeLists.txt` | Each component has its own CMake |

### Component Structure

```
components/
└── component_name/
    ├── CMakeLists.txt
    ├── component_name.c
    └── include/
        └── component_name.h
```

### Header Guards

```c
#ifndef COMPONENT_NAME_H
#define COMPONENT_NAME_H

// declarations

#endif /* COMPONENT_NAME_H */
```

### Function Naming

| Type | Convention | Example |
|------|------------|--------|
| Public functions | `component_action()` | `wifi_init_sta()` |
| Private functions | `s_component_action()` | `s3_on_mac_recv()` |
| Callbacks | `s_component_on_event()` | `s3_on_mac_send()` |
| ISR handlers | `s_component_isr()` | - |

### Global Variables

- Prefix with component name: `global_mqtt_client`
- Use `static` for module-scoped globals
- Use `extern` only in headers when necessary

### Logging

Use `esp_log.h` with tag-based logging:

```c
static const char *TAG = "COMPONENT_NAME";

ESP_LOGI(TAG, "Message: %s", value);    // Info
ESP_LOGW(TAG, "Warning: %d", value);    // Warning
ESP_LOGE(TAG, "Error: %s", strerror());  // Error
```

### Error Handling

Use ESP-IDF error macros:

```c
ESP_ERROR_CHECK(ret);                    // Check and abort on error
ESP_ERROR_CHECK_WITHOUT_ABORT(ret);     // Check without abort
if (ret != ESP_OK) { handle_error(); }  // Custom handling
```

### GPIO Configuration

```c
gpio_reset_pin(GPIO_PIN_RELAY);
gpio_set_direction(GPIO_PIN_RELAY, GPIO_MODE_OUTPUT);
gpio_set_level(GPIO_PIN_RELAY, 0);
```

### Struct Packing (ESP-NOW)

Use `#pragma pack` for protocol structures:

```c
#pragma pack(push, 1)
typedef struct {
    char time_str[16];
    float temperature;
    // ...
} sensor_data_t;
#pragma pack(pop)
```

---

## Node.js Backend Standards

### File Structure

```
backend/
├── server.js        # Single-file Express app (155 lines)
└── package.json
```

### Module Pattern

CommonJS (`"type": "commonjs"` in package.json):

```javascript
const express = require('express');
const mqtt = require('mqtt');
const { Pool } = require('pg');
```

### Async/Await Pattern

Use async/await for database operations:

```javascript
// Good
const result = await pool.query('SELECT * FROM devices');

// Avoid
pool.query('SELECT...', (err, result) => { ... });
```

### Error Handling

```javascript
try {
    // async operations
} catch (err) {
    console.error("Error description:", err);
    res.status(500).json({ error: "Message" });
}
```

### Logging

Use descriptive console.log with emoji for easy scanning:

```javascript
console.log('✅ Success message');
console.log('📥 Incoming data:', payload);
console.log('❌ Error:', err.message);
```

---

## Git Conventions

### Commit Messages

Use conventional commits:

| Type | Usage |
|------|-------|
| `feat:` | New feature |
| `fix:` | Bug fix |
| `docs:` | Documentation |
| `refactor:` | Code restructuring |
| `test:` | Tests |
| `chore:` | Maintenance |

### Example

```
feat: add ESP-NOW peer configuration
fix: correct GPIO pin mapping
docs: update API documentation
```

### Files to Never Commit

- `.env` files with credentials
- `build/` directory
- `node_modules/`
- `sdkconfig` (keep sdkconfig.old)
- `*.lock` files (except package-lock.json)

---

## Code Style

### C Formatting

| Rule | Standard |
|------|----------|
| Indent | 4 spaces |
| Braces | Same line for functions, new line for control |
| Max line length | ~100 chars |
| Trailing newline | Yes |

### JavaScript Formatting

| Rule | Standard |
|------|----------|
| Indent | 4 spaces (or project standard) |
| Semicolons | Yes |
| Quotes | Single for strings |
| Max line length | 100 chars |

---

## Build & Deployment

### ESP-IDF Build

```bash
idf.py build           # Compile
idf.py flash monitor   # Flash and monitor
idf.py fullclean       # Clean build artifacts
```

### Node.js Deployment

```bash
cd backend
npm install
node server.js
```

### Environment Variables

Currently partially hardcoded in backend. Recommended: use `.env` file:

```bash
# backend/.env
DB_URL=postgresql://user:pass@host:5432/db?sslmode=require
JWT_SECRET=your-secret-key-change-in-production
PORT=3000
```

**Current State**: DB_URL and JWT_SECRET are hardcoded in `server.js`. MQTT_BROKER is hardcoded.

---

## Testing Strategy

### Current State
- No test suite exists
- Manual testing via `idf.py monitor`

### Recommended Approach

**ESP-IDF**:
- Unity test framework (built into ESP-IDF)
- Component-level unit tests
- Integration tests for WiFi/MQTT/ESP-NOW

**Node.js**:
- Jest or Mocha for backend
- Supertest for API endpoint testing

---

## Security Considerations

### Current Issues (Technical Debt)

| Issue | Risk | Status | Recommendation |
|-------|------|--------|----------------|
| Hardcoded WiFi credentials | High | Open | Move to NVS encrypted storage |
| Hardcoded DB connection string | High | Open | Use environment variables |
| No MQTT authentication | Medium | Open | Enable MQTT TLS |
| JWT API authentication | Low | **Implemented** | Add auth middleware to /devices, /telemetry |

### Best Practices

1. Never commit credentials to git
2. Use NVS for sensitive ESP config
3. Enable ESP32 secure boot
4. Use TLS for MQTT (port 8883)
5. Add authentication to REST API

---

## Documentation Requirements

### For Each Component

- Header file documents public API
- Complex logic has inline comments
- `README.md` in component directory (optional)

### For Backend

- JSDoc comments for functions
- Inline comments for complex async flows

---

## Performance Guidelines

### ESP-IDF

| Resource | Guideline |
|----------|-----------|
| Task stack | Minimum necessary size |
| Delays | Use `vTaskDelay()` not `esp_timer` for long delays |
| Memory | Avoid dynamic allocation in ISR context |

### Node.js

| Resource | Guideline |
|----------|-----------|
| DB queries | Use parameterized queries (prevent SQL injection) |
| MQTT messages | Parse JSON efficiently, handle malformed data |
| Connection pooling | Use `pg.Pool` for PostgreSQL |
