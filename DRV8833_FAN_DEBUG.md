# Debug Quạt DRV8833 (OUT1/OUT2)

## Tình Trạng Hiện Tại

✅ **Debug task đã được triển khai** trong `esp32.c`:
- Test lúc khởi động (tổng **10 giây**): **Quạt 3s → Phun sương 3s → Cả 2 cùng 4s → Tắt**
- Quạt: GPIO25 (IN1) & GPIO26 (IN2)
- Phun sương (kênh B): GPIO27 (IN3) & GPIO12 (IN4)
- Priority 3, Stack 2048 bytes

---

## Các Bước Debug

### 1️⃣ **Kiểm Tra Kết Nối Vật Lý**

```
DRV8833 Module:
├─ GND           ──┬──> GND ESP32
├─ VCC (5V/3.3V)  ──> 3.3V/5V ESP32
├─ IN1 (GPIO25)   ──> GPIO25 ESP32
├─ IN2 (GPIO26)   ──> GPIO26 ESP32
├─ OUT1           ──> Chân + Quạt
└─ OUT2           ──> Chân - Quạt

⚠️ QUAN TRỌNG:
- OUT1/OUT2 cần được nối ngược 180° để xác định hướng quay
- VCC phải đủ nguồn (≥ 1A cho quạt)
```

### 2️⃣ **Kiểm Tra Log Monitor**

**Khi khởi động, bạn sẽ thấy:**
```
I (XXX) MOTOR: Initializing L293 motor + DRV8833 outputs...
I (XXX) MOTOR: Motor control initialized:
I (XXX) MOTOR:   - L293: GPIO18(IN1), GPIO19(IN2), GPIO13(ENA PWM)
I (XXX) MOTOR:   - DRV8833 fan: GPIO25(IN1), GPIO26(IN2)
I (XXX) MOTOR:   - DRV8833 ultrasonic: GPIO27(IN3), GPIO12(IN4)
...
I (XXX) MAIN: Creating debug fan task...
```

**Trong lúc test (10 giây):**
```
I (XXXX) MAIN: TEST1/3: FAN ON (GPIO25/26), MIST OFF (GPIO27/12)
I (XXXX) MOTOR: DRV8833 fan ON
I (XXXX) MAIN: TEST2/3: MIST ON (GPIO27/12), FAN OFF (GPIO25/26)
I (XXXX) MOTOR: DRV8833 ultrasonic ON
I (XXXX) MAIN: TEST3/3: AC ON (fan + mist)
```

### 3️⃣ **Kiểm Tra GPIO Levels Với Multimeter**

| Trạng Thái | GPIO25 | GPIO26 | OUT1 | OUT2 | Kết Quả |
|-----------|--------|--------|------|------|---------|
| **OFF**   | LOW(0) | LOW(0) | GND  | GND  | Quạt dừng |
| **ON**    | HIGH(1)| LOW(0) | VCC  | GND  | Quạt quay |
| Sai       | HIGH   | HIGH   | VCC  | VCC  | **Quạt dừng** (lỗi!) |

### 4️⃣ **Tìm Lỗi - Checklist**

- [ ] Quạt **không quay lúc ON**:
  - DRV8833 được cung cấp điện đủ? (LED trên module sáng không?)
  - OUT1/OUT2 nối đúng quạt?
  - Test GPIO25/GPIO26 với Multimeter

- [ ] **Quạt quay nhưng không dừng** lúc OFF:
  - Có cập nhật PWM? (xem motor_stop())
  - IN2 có được set LOW không?

- [ ] **Monitor không thấy log**:
  - Có tạo task không? (check xTaskCreate)
  - Priority/stack size đủ?
  - Check UART settings (115200 baud)

- [ ] **GPIO xung đột**:
  - ⚠️ PIN_FAN_MIST = 25 (trùng GPIO25!)
  - Lệnh ESP-NOW `CMD:FAN_ON` có gọi `drv8833_fan_set_power()` không?
  - Hay đang gọi `gpio_set_level(PIN_FAN_MIST, 1)` (SAI!)?

---

## 🔧 Lệnh Test Nhanh

### Via Serial Monitor (ESP-NOW):
```
CMD:FAN_ON     → Bật quạt (gửi từ remote device)
CMD:FAN_OFF    → Tắt quạt
```

### Via Code (thêm vào main):
```c
// Test pin thực tế
gpio_set_level(GPIO_NUM_25, 1);  // BẬT IN1
vTaskDelay(pdMS_TO_TICKS(2000));
gpio_set_level(GPIO_NUM_25, 0);  // TẮT IN1
```

---

## 🐛 Vấn Đề Tiềm Ẩn

### ⚠️ PIN_FAN_MIST Conflict
```c
// esp32.c line 60
#define PIN_FAN_MIST 25   // ← GPIO25, cùng DRV8833_FAN_IN1_PIN!

// esp32.c line 159
gpio_set_level(PIN_FAN_MIST, 1);  // ← BẬT GPIO25 trực tiếp!
// Vàng không dùng drv8833_fan_set_power()
```

**Khuyến cáo sửa:**
```c
// Dùng hàm wrapper thay vì gpio_set_level trực tiếp
else if (strncmp(buf, "CMD:FAN_ON", 10) == 0)
{
    state_fan_mist = true;
    drv8833_fan_set_power(true);  // ✅ Correct
    ESP_LOGI(TAG, "[ESP-NOW RX] CMD: BẬT Quạt + Phun sương");
}
```

---

## ✅ Debug Task Code (Có Sẵn)

```c
static void debug_fan_task(void *pvParameters)
{
  ESP_LOGI(TAG, "Starting DRV8833 boot self-test (10s): FAN 3s -> MIST 3s -> BOTH 4s");

  // 1) FAN only (3s)
  drv8833_fan_set_power(true);
  drv8833_ultrasonic_set_power(false);
  vTaskDelay(pdMS_TO_TICKS(3000));

  // 2) MIST only (3s)
  drv8833_fan_set_power(false);
  drv8833_ultrasonic_set_power(true);
  vTaskDelay(pdMS_TO_TICKS(3000));

  // 3) BOTH (AC) (4s)
  drv8833_fan_set_power(true);
  drv8833_ultrasonic_set_power(true);
  vTaskDelay(pdMS_TO_TICKS(4000));

  drv8833_fan_set_power(false);
  drv8833_ultrasonic_set_power(false);
  vTaskDelete(NULL);
}
```

---

## 📊 Thử Nghiệm Bước Từng Bước

1. **Flash code** (debug_fan_task đã chạy)
2. **Open Monitor** (115200 baud)
3. **Theo dõi log** mỗi 4 giây
4. **Dùng Multimeter** kiểm tra GPIO25/GPIO26
5. **Nhận xét**:
   - Quạt quay/không quay?
   - Điện áp OUT1/OUT2 đúng không?
   - Có kỳ vọng không?

---

## 🎯 Kết Luận

**Status:** Debug infrastructure đã hoàn thành  
**Next:** Chạy test và kết quả kiểm tra pin để xác định lỗi
