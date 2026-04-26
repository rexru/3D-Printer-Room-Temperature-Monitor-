#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "wifi_manager.h"
 
static const char *TAG = "main";

static void my_wifi_callback(wifi_manager_event_t event, uint32_t ip_addr) {
    switch (event) {
        case WIFI_EVENT_CONNECTED:
            ESP_LOGI(TAG, "[CB] Connected! IP: %"PRIu32".%"PRIu32".%"PRIu32".%"PRIu32,
                (ip_addr) & 0xFF,
                (ip_addr >> 8) & 0xFF,
                (ip_addr >> 16) & 0xFF,
                (ip_addr >> 24) & 0xFF);
            // Safe to start MQTT, HTTP client, etc. here
            break;
        
        case WIFI_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "[CB] Disconnected — retrying...");
            break;
            
        case WIFI_EVENT_FAILED:
            ESP_LOGE(TAG, "[CB] Gave up connecting");
            break;
    }
}


static void network_task(void *arg) {
    ESP_LOGI(TAG, "Network task waiting for WiFi...");
    
    EventBits_t bits = xEventGroupWaitBits(
        wifi_manager_get_event_group(),
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, // don't clear bits on exit
        pdFALSE, // wait for ANY bit (not all)
        portMAX_DELAY
    );
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "[EG] WiFi ready — starting network work");
        // e.g. start HTTP request, MQTT connect, OTA check...
        } else {
            ESP_LOGE(TAG, "[EG] WiFi failed — check credentials");
    }   
    
    vTaskDelete(NULL);
}


void app_main(void) {
    // 1. NVS must be initialized before wifi_manager_init()
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
    }
    
    ESP_ERROR_CHECK(ret);
    
    // 2. Register callback BEFORE init so you don't miss the first event
    wifi_manager_set_callback(my_wifi_callback);
    
    // 3. Start the WiFi manager
    ESP_ERROR_CHECK(wifi_manager_init());
    
    // 4. Spawn a task that blocks on the EventGroup (Option B)
    xTaskCreate(network_task, "network_task", 4096, NULL, 5, NULL);
    
    // 5. Optionally update credentials at runtime (saves to NVS, reconnects)
    // wifi_manager_set_credentials("new_ssid", "new_password");
    
    // Main loop — your application logic
    while (1) {
        if (wifi_manager_is_connected()) {
            ESP_LOGD(TAG, "Tick — IP: 0x%"PRIx32, wifi_manager_get_ip());
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}