#include "ota_manager.h"
 
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "mqtt_client.h"
#include "esp_app_desc.h"


// ----------------------------------------------------------------------------
// Private constants
// ----------------------------------------------------------------------------
static const char *TAG = "ota_manager";

#define OTA_URL_MAX_LEN     256
#define OTA_TASK_STACK      8192    // OTA needs a large stack
#define OTA_TASK_PRIORITY   5

// ----------------------------------------------------------------------------
// Private state
// ----------------------------------------------------------------------------
static esp_mqtt_client_handle_t  s_mqtt_client   = NULL;
static ota_manager_callback_t    s_callback       = NULL;
static bool                      s_initialized    = false;
static bool                      s_updating       = false;
static QueueHandle_t             s_ota_url_queue  = NULL;

// ----------------------------------------------------------------------------
// Private helpers
// ----------------------------------------------------------------------------

static void notify(ota_manager_event_t event, int progress) {
    if (s_callback) s_callback(event, progress);
}

/**
 * Performs the actual OTA download and flash.
 * Runs in its own task so it doesn't block the MQTT event loop.
 */
static void ota_update_task(void *arg) {
    char url[OTA_URL_MAX_LEN];

    // Block until a URL is pushed onto the queue by the MQTT handler
    if (xQueueReceive(s_ota_url_queue, url, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to receive URL from queue");
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting OTA from: %s", url);
    notify(OTA_EVENT_TRIGGERED, 0);

    // Get the current running partition for rollback reference
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

    // Configure HTTP client for firmware download
    esp_http_client_config_t http_cfg = {
        .url             = url,
        .timeout_ms      = OTA_DOWNLOAD_TIMEOUT_S * 1000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    // Begin OTA
    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        notify(OTA_EVENT_FAILED, 0);
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }

    // Get firmware image size for progress tracking
    int image_size = esp_https_ota_get_image_size(ota_handle);
    ESP_LOGI(TAG, "Firmware size: %d bytes", image_size);

    // Download and flash in chunks
    while (1) {
        err = esp_https_ota_perform(ota_handle);

        if (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            // Calculate and report progress
            if (image_size > 0) {
                int downloaded = esp_https_ota_get_image_len_read(ota_handle);
                int progress   = (downloaded * 100) / image_size;
                ESP_LOGI(TAG, "OTA progress: %d%%", progress);
                notify(OTA_EVENT_PROGRESS, progress);
            }
            continue;
        }

        // ESP_OK means download complete
        if (err == ESP_OK) break;

        // Any other return value is an error
        ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        notify(OTA_EVENT_FAILED, 0);
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }

    // Validate the downloaded image
    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        ESP_LOGE(TAG, "Incomplete firmware received");
        esp_https_ota_abort(ota_handle);
        notify(OTA_EVENT_FAILED, 0);
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }

    // Finalize — marks new partition as boot target
    err = esp_https_ota_finish(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
        notify(OTA_EVENT_FAILED, 0);
        s_updating = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA successful! Rebooting in 3 seconds...");
    notify(OTA_EVENT_SUCCESS, 100);

    // Give time for callback/logs to flush before reboot
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

// ----------------------------------------------------------------------------
// MQTT event handler
// ----------------------------------------------------------------------------

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            // Subscribe to OTA trigger topic
            esp_mqtt_client_subscribe(s_mqtt_client, OTA_MQTT_TOPIC, 1);
            ESP_LOGI(TAG, "Subscribed to topic: %s", OTA_MQTT_TOPIC);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected — will auto-reconnect");
            break;

        case MQTT_EVENT_DATA:
            // A message arrived on our subscribed topic
            if (event->data_len > 0 && !s_updating) {
                // Validate it looks like a URL
                if (strncmp(event->data, "http", 4) != 0) {
                    ESP_LOGW(TAG, "OTA trigger ignored — message is not a URL");
                    break;
                }

                // Copy URL (data is not null-terminated)
                char url[OTA_URL_MAX_LEN] = { 0 };
                int  len = event->data_len < OTA_URL_MAX_LEN - 1
                         ? event->data_len
                         : OTA_URL_MAX_LEN - 1;
                memcpy(url, event->data, len);

                ESP_LOGI(TAG, "OTA trigger received: %s", url);
                s_updating = true;

                // Push URL to queue — OTA task picks it up
                xQueueSend(s_ota_url_queue, url, 0);

                // Spawn OTA task (needs large stack)
                xTaskCreate(ota_update_task, "ota_task",
                            OTA_TASK_STACK, NULL,
                            OTA_TASK_PRIORITY, NULL);
            } else if (s_updating) {
                ESP_LOGW(TAG, "OTA already in progress — trigger ignored");
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;

        default:
            break;
    }
}

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

esp_err_t ota_manager_init(void) {
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    // URL queue — depth 1 is enough (one OTA at a time)
    s_ota_url_queue = xQueueCreate(1, OTA_URL_MAX_LEN);
    if (!s_ota_url_queue) return ESP_ERR_NO_MEM;

    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri      = OTA_MQTT_BROKER,
        .broker.address.port     = OTA_MQTT_PORT,
        .credentials.username    = OTA_MQTT_USERNAME,
        .credentials.authentication.password = OTA_MQTT_PASSWORD,
        .session.keepalive       = 30,
        .network.reconnect_timeout_ms = 5000,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) return ESP_FAIL;

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));

    s_initialized = true;
    ESP_LOGI(TAG, "OTA manager initialized — waiting for trigger on: %s",
             OTA_MQTT_TOPIC);
    return ESP_OK;
}

esp_err_t ota_manager_deinit(void) {
    if (!s_initialized) return ESP_OK;

    esp_mqtt_client_stop(s_mqtt_client);
    esp_mqtt_client_destroy(s_mqtt_client);
    s_mqtt_client = NULL;

    if (s_ota_url_queue) {
        vQueueDelete(s_ota_url_queue);
        s_ota_url_queue = NULL;
    }

    s_initialized = false;
    s_updating    = false;
    return ESP_OK;
}

void ota_manager_set_callback(ota_manager_callback_t cb) {
    s_callback = cb;
}

bool ota_manager_is_updating(void) {
    return s_updating;
}