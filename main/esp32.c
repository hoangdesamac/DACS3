#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "espnow.h"

static const char *TAG = "MAIN";

// Board #1 (ESP32) MAC address
static uint8_t peer_mac[] = {0xB0, 0xCB, 0xD8, 0x8A, 0x82, 0xA0};

// Called when a message arrives from the other ESP
static void on_message(const uint8_t *src_mac, const uint8_t *data, int len)
{
    char buf[256] = {0};
    memcpy(buf, data, len < 255 ? len : 255);
    ESP_LOGI(TAG, "Got message: %s", buf);
}

void app_main(void)
{
    // 1. Init ESP-NOW (prints this device's MAC address)
    espnow_init();

    // 2. Register receive callback
    espnow_register_recv_cb(on_message);

    // 3. Add the other ESP as a peer
    espnow_add_peer(peer_mac);

    // 4. Send a message every 2 seconds
    uint32_t count = 0;
    while (1)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "Hello from DACS3 #%lu", count++);
        espnow_send(peer_mac, (uint8_t *)msg, strlen(msg));
        ESP_LOGI(TAG, "Sent: %s", msg);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
