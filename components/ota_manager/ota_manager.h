#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------
// Configuration — set via menuconfig (Kconfig.projbuild)
// ----------------------------------------------------------------------------

#ifndef OTA_MQTT_BROKER
#define OTA_MQTT_BROKER         CONFIG_OTA_MQTT_BROKER
#endif

#ifndef OTA_MQTT_PORT
#define OTA_MQTT_PORT           CONFIG_OTA_MQTT_PORT
#endif

#ifndef OTA_MQTT_USERNAME
#define OTA_MQTT_USERNAME       CONFIG_OTA_MQTT_USERNAME
#endif

#ifndef OTA_MQTT_PASSWORD
#define OTA_MQTT_PASSWORD       CONFIG_OTA_MQTT_PASSWORD
#endif

#ifndef OTA_MQTT_TOPIC
#define OTA_MQTT_TOPIC          CONFIG_OTA_MQTT_TOPIC
#endif

// How long to wait for firmware download before giving up (seconds)
#define OTA_DOWNLOAD_TIMEOUT_S  120

// ----------------------------------------------------------------------------
// Events
// ----------------------------------------------------------------------------
typedef enum {
    OTA_EVENT_TRIGGERED,        // MQTT trigger received, starting download
    OTA_EVENT_PROGRESS,         // Download in progress (progress 0-100)
    OTA_EVENT_SUCCESS,          // Flashed successfully, rebooting
    OTA_EVENT_FAILED,           // Something went wrong
} ota_manager_event_t;

typedef void (*ota_manager_callback_t)(ota_manager_event_t event, int progress);

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

/**
 * @brief  Initialize OTA manager and connect to MQTT broker.
 *
 * Call after wifi_manager_init() and after WiFi is connected.
 * Subscribes to OTA_MQTT_TOPIC and waits for trigger messages.
 *
 * @return ESP_OK on success
 */
esp_err_t ota_manager_init(void);

/**
 * @brief  Deinitialize OTA manager and disconnect MQTT.
 */
esp_err_t ota_manager_deinit(void);

/**
 * @brief  Register a callback for OTA state changes.
 *
 * Optional — useful for showing update progress on a display or LED.
 * Pass NULL to clear.
 */
void ota_manager_set_callback(ota_manager_callback_t cb);

/**
 * @brief  Returns true if an OTA update is currently in progress.
 */
bool ota_manager_is_updating(void);

#ifdef __cplusplus
}
#endif