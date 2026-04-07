#pragma once

#include <stdint.h>
#include "esp_now.h"

typedef void (*espnow_recv_cb_t)(const uint8_t *src_mac, const uint8_t *data, int len);

void espnow_init(void);

void espnow_add_peer(const uint8_t *peer_mac);

void espnow_send(const uint8_t *peer_mac, const uint8_t *data, uint16_t len);

void espnow_register_recv_cb(espnow_recv_cb_t cb);

void espnow_print_mac(void);
