#pragma once

#include <stdint.h>
#include "esp_now.h"

/**
 * @brief Callback type for receiving ESP-NOW data
 * @param src_mac  sender MAC address (6 bytes)
 * @param data     received payload
 * @param len      payload length
 */
typedef void (*espnow_recv_cb_t)(const uint8_t *src_mac, const uint8_t *data, int len);

/**
 * @brief Initialize WiFi + ESP-NOW subsystem
 *        Call this once in app_main before send/receive
 */
void espnow_init(void);

/**
 * @brief Add a peer by MAC address
 * @param peer_mac  6-byte MAC address of the peer
 */
void espnow_add_peer(const uint8_t *peer_mac);

/**
 * @brief Send data to a peer
 * @param peer_mac  destination MAC (6 bytes)
 * @param data      payload to send
 * @param len       payload length (max 250)
 */
void espnow_send(const uint8_t *peer_mac, const uint8_t *data, uint16_t len);

/**
 * @brief Register a callback for incoming ESP-NOW messages
 * @param cb  your callback function
 */
void espnow_register_recv_cb(espnow_recv_cb_t cb);

/**
 * @brief Print this device's MAC address to the log
 */
void espnow_print_mac(void);
