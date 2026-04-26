#pragma once        // only compile this file once

#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus 
extern "C" {
#endif    


// event group bits for wifi state

#define WIFI_CONNECTED_BIT         BIT0 // IP address
#define WIFI_FAIL_BIT              BIT1 // Failed


// configuration

/** Fallback credentials 
 * Override via menuconfig 
  */
#ifndef WIFI_FALLBACK_SSID
#define WIFI_FALLBACK_SSID      CONFIG_WIFI_FALLBACK_SSID
#endif

#ifndef WIFI_FALLBACK_PASSWORD
#define WIFI_FALLBACK_PASSWORD      CONFIG_WIFI_FALLBACK_PASSWORD
#endif

/*Exponential backoff settings */
#define WIFI_BACKOFF_BASE_MS    1000 //intial delay 1s
#define WIFI_BACKOFF_MAX_MS     60000 // Max retry delay 60s
#define WIFI_MAX_RETRY          0 // infinte retrys but progressivly slows down time betweem each attempt to reconnect


// Callback type
typedef enum {
    WIFI_EVENT_CONNECTED,       // valid ip recieved 
    WIFI_EVENT_DISCONNECTED,    // lost connection
    WIFI_EVENT_FAILED           // exhausted retries - dependant on wifi max retries 
} wifi_manager_event_t;

typedef void (*wifi_manager_callback_t)(wifi_manager_event_t event, uint32_t ip_addr);      // function pointer for functions that react to wifi events



//--------------------------------------------------------------------------
// Public API
//---------------------------------------------------------------------------

/**
 * @brief  Initialize and start the WiFi manager.
 *
 * Reads credentials from NVS; falls back to WIFI_FALLBACK_SSID/PASSWORD.
 * Call once from app_main after nvs_flash_init().
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);
 
/**
 * @brief  Deinitialize the WiFi manager and free resources.
 */
esp_err_t wifi_manager_deinit(void);
 
/**
 * @brief  Register a callback invoked on connection state changes.
 *
 * Callbacks are invoked from the system event task — keep them short.
 * Pass NULL to clear the callback.
 */
void wifi_manager_set_callback(wifi_manager_callback_t cb);
 
/**
 * @brief  Save new credentials to NVS and reconnect.
 *
 * @param ssid      null-terminated SSID (max 32 bytes)
 * @param password  null-terminated password (max 64 bytes)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);
 
/**
 * @brief  Get the event group handle for task-based waiting.
 *
 * Bits: WIFI_CONNECTED_BIT, WIFI_FAIL_BIT
 *
 * Example:
 *   EventBits_t bits = xEventGroupWaitBits(wifi_manager_get_event_group(),
 *       WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
 */
EventGroupHandle_t wifi_manager_get_event_group(void);
 
/**
 * @brief  Returns true if currently connected with a valid IP.
 */
bool wifi_manager_is_connected(void);
 
/**
 * @brief  Returns the current IPv4 address (0 if not connected).
 */
uint32_t wifi_manager_get_ip(void);



#ifdef __cplusplus
}
#endif