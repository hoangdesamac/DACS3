#include "esp_rom_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_struct.h"
#include <stdio.h>

// Nếu dùng board N16R8 Freenove/Espressif: Thường là GPIO 2 (LED xanh trên
// board) hoặc 48
#define LED_PIN 2

void app_main(void) {
  printf("--- LOW LEVEL BLINK START (FIXED) ---\n");

  // BƯỚC 0: Cấu hình IOMUX
  esp_rom_gpio_pad_select_gpio(LED_PIN);

  // BƯỚC 1: Cấu hình ENABLE (Output)
  // SỬA LỖI 1: Bỏ ".data", gán trực tiếp vào thanh ghi
  if (LED_PIN < 32) {
    GPIO.enable_w1ts = (1 << LED_PIN);
  } else {
    // Nếu chân > 31, ta trừ đi 32 để lấy vị trí bit trong Bank 1
    GPIO.enable1_w1ts.data =
        (1 << (LED_PIN - 32)); // Lưu ý: Bank 1 đôi khi vẫn dùng struct .data
                               // tùy version, nếu lỗi cứ bỏ .data
  }

  while (1) {
    // --- BẬT ĐÈN ---
    if (LED_PIN < 32) {
      GPIO.out_w1ts = (1 << LED_PIN); // Sửa: Bỏ .data
    } else {
      // Mẹo để compiler không báo lỗi "negative shift" nếu LED_PIN < 32
      // Ta ép kiểu hoặc tính toán runtime để compiler không tính trước
      uint32_t pin_mask = (1 << (LED_PIN - 32));
      GPIO.out1_w1ts.data = pin_mask;
    }
    printf("LED ON (REG)\n");
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // --- TẮT ĐÈN ---
    if (LED_PIN < 32) {
      GPIO.out_w1tc = (1 << LED_PIN); // Sửa: Bỏ .data
    } else {
      uint32_t pin_mask = (1 << (LED_PIN - 32));
      GPIO.out1_w1tc.data = pin_mask;
    }
    printf("LED OFF (REG)\n");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
