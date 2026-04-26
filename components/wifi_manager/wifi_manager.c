#include "wifi_manager.h"
 
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
 
// ----------------------------------------------------------------------------
// Private constants
// ----------------------------------------------------------------------------
static const char *TAG = "wifi_manager";
 
#define NVS_NAMESPACE "wifi_mgr"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "password"
 
// ----------------------------------------------------------------------------
// Private state
// ----------------------------------------------------------------------------
static EventGroupHandle_t s_event_group = NULL;
static esp_netif_t *s_netif_sta = NULL;
static wifi_manager_callback_t s_callback = NULL;
static bool s_initialized = false;
static bool s_connected = false;
static uint32_t s_ip_addr = 0;
 
// Retry / backoff state
static int s_retry_count = 0;
static uint32_t s_backoff_ms = WIFI_BACKOFF_BASE_MS;
 
// ----------------------------------------------------------------------------
// Private helpers
// ----------------------------------------------------------------------------
 
/** Double the backoff delay, clamped to WIFI_BACKOFF_MAX_MS */
static void backoff_increase(void) {
    s_backoff_ms *= 2;
    if (s_backoff_ms > WIFI_BACKOFF_MAX_MS) {
        s_backoff_ms = WIFI_BACKOFF_MAX_MS;
    }
}
 
/** Reset retry counters (call after a successful connection) */
static void backoff_reset(void) {
    s_retry_count = 0;
    s_backoff_ms = WIFI_BACKOFF_BASE_MS;
}
 
/**
 * Read credentials from NVS into out_ssid / out_pass.
 * Returns ESP_OK if both keys exist; ESP_ERR_NVS_NOT_FOUND otherwise.
 */
static esp_err_t load_credentials_from_nvs(char *out_ssid, size_t ssid_len,
    char *out_pass, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_get_str(handle, NVS_KEY_SSID, out_ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, NVS_KEY_PASS, out_pass, &pass_len);
 }
 
 nvs_close(handle);
 return err;
}
 
/** Apply wifi_config and call esp_wifi_connect() */
static esp_err_t do_connect(const char *ssid, const char *password) {
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));

    // Require PMF capable for better security on WPA2/WPA3 networks
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    return esp_wifi_connect();
}
 
// ----------------------------------------------------------------------------
// Event handlers
// ----------------------------------------------------------------------------
 
static void on_wifi_event(void *arg, esp_event_base_t base,
    int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        // WiFi stack ready — kick off first connection attempt
        ESP_LOGI(TAG, "STA started, connecting...");
        
        char ssid[33] = { 0 };
        char pass[65] = { 0 };
        
        if (load_credentials_from_nvs(ssid, sizeof(ssid),
        pass, sizeof(pass)) == ESP_OK) {
            ESP_LOGI(TAG, "Using credentials from NVS (SSID: %s)", ssid);
        } else {
            ESP_LOGW(TAG, "NVS empty, using fallback credentials");
            strlcpy(ssid, WIFI_FALLBACK_SSID, sizeof(ssid));
            strlcpy(pass, WIFI_FALLBACK_PASSWORD, sizeof(pass));
        }
        do_connect(ssid, pass);
    
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip_addr = 0;
        
        wifi_event_sta_disconnected_t *disc =
        (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Disconnected (reason: %d)", disc->reason);
        
        // Check retry limit (0 = unlimited)
        if (WIFI_MAX_RETRY > 0 && s_retry_count >= WIFI_MAX_RETRY) {
            ESP_LOGE(TAG, "Max retries (%d) reached — giving up", WIFI_MAX_RETRY);
            xEventGroupSetBits(s_event_group, WIFI_FAIL_BIT);
            xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT);
            if (s_callback) s_callback(WIFI_EVENT_FAILED, 0);
            return;
        }
        
        ESP_LOGI(TAG, "Retry %d/%s in %"PRIu32" ms...",
            s_retry_count + 1,
            WIFI_MAX_RETRY ? "?" : "∞", // show ∞ for unlimited
            s_backoff_ms);
        
        // Block in a one-shot timer task so we don't stall the event loop
        uint32_t delay_ms = s_backoff_ms;
        backoff_increase();
        s_retry_count++;
            
        // Notify app of disconnection before sleeping
        if (s_callback) s_callback(WIFI_EVENT_DISCONNECTED, 0);
        xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT);
            
        // Use a small anonymous task to sleep then reconnect
        // (vTaskDelay in event handler would block the system event task)
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        esp_wifi_connect();
    }
}
 
static void on_ip_event(void *arg, esp_event_base_t base,
    int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_ip_addr = event->ip_info.ip.addr;
        s_connected = true;
        backoff_reset();
        
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
        xEventGroupClearBits(s_event_group, WIFI_FAIL_BIT);
        
        if (s_callback) s_callback(WIFI_EVENT_CONNECTED, s_ip_addr);
    }
}
 
// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------
 
esp_err_t wifi_manager_init(void) {
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    // Event group
    s_event_group = xEventGroupCreate();
    if (!s_event_group) return ESP_ERR_NO_MEM;
    
    // TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif_sta = esp_netif_create_default_wifi_sta();
    
    // WiFi driver with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL, NULL));
            
    // STA mode, then start — triggers WIFI_EVENT_STA_START
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    s_initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}
 
esp_err_t wifi_manager_deinit(void) {
    if (!s_initialized) return ESP_OK;
    
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy(s_netif_sta);
 
    if (s_event_group) {
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
    }
    
    s_initialized = false;
    s_connected = false;
    s_ip_addr = 0;
    s_callback = NULL;
    
    ESP_LOGI(TAG, "WiFi manager deinitialized");
    return ESP_OK;
}
 
void wifi_manager_set_callback(wifi_manager_callback_t cb) {
    s_callback = cb;
}
 
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password) {
    if (!ssid || !password) return ESP_ERR_INVALID_ARG;
    
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_str(handle, NVS_KEY_SSID, ssid));
    ESP_ERROR_CHECK(nvs_set_str(handle, NVS_KEY_PASS, password));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Credentials saved to NVS (SSID: %s)", ssid);
    
    // Reconnect with new credentials if already running
    if (s_initialized) {
        backoff_reset();
        esp_wifi_disconnect(); // triggers WIFI_EVENT_STA_DISCONNECTED
        do_connect(ssid, password);
    }
    
    return ESP_OK;
}
 
EventGroupHandle_t wifi_manager_get_event_group(void) {
    return s_event_group;
}
 
bool wifi_manager_is_connected(void) {
    return s_connected;
}
 
uint32_t wifi_manager_get_ip(void) {
    return s_ip_addr;
}
