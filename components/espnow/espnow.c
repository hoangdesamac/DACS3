#include <string.h>
#include "espnow.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "ESPNOW";

static espnow_recv_cb_t s_user_recv_cb = NULL;

/* ---- internal callbacks ---- */

static void on_send_cb(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    const uint8_t *mac_addr = info->des_addr;
    ESP_LOGI(TAG, "Send to %02X:%02X:%02X:%02X:%02X:%02X → %s",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5],
             status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

static void on_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    ESP_LOGI(TAG, "Recv from %02X:%02X:%02X:%02X:%02X:%02X (%d bytes)",
             info->src_addr[0], info->src_addr[1], info->src_addr[2],
             info->src_addr[3], info->src_addr[4], info->src_addr[5],
             len);

    if (s_user_recv_cb)
    {
        s_user_recv_cb(info->src_addr, data, len);
    }
}

/* ---- public API ---- */

void espnow_init(void)
{
    // NVS (required by WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Network interface & event loop
    esp_netif_init();
    esp_event_loop_create_default();

    // WiFi in STA mode
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    // ESP-NOW
    esp_now_init();
    esp_now_register_send_cb(on_send_cb);
    esp_now_register_recv_cb(on_recv_cb);

    ESP_LOGI(TAG, "ESP-NOW initialized");
    espnow_print_mac();
}

void espnow_add_peer(const uint8_t *peer_mac)
{
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, peer_mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    ESP_LOGI(TAG, "Added peer %02X:%02X:%02X:%02X:%02X:%02X",
             peer_mac[0], peer_mac[1], peer_mac[2],
             peer_mac[3], peer_mac[4], peer_mac[5]);
}

void espnow_send(const uint8_t *peer_mac, const uint8_t *data, uint16_t len)
{
    esp_now_send(peer_mac, data, len);
}

void espnow_register_recv_cb(espnow_recv_cb_t cb)
{
    s_user_recv_cb = cb;
}

void espnow_print_mac(void)
{
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "MY MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
