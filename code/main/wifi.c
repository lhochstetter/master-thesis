#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"

#include "nvs.h"
#include "nvs_flash.h"

extern const char *TAG;

/* FreeRTOS event group to signal when we are connected & ready to make a request */
EventGroupHandle_t g_wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

static esp_err_t event_handler(void *ctx, system_event_t * event)
{
	switch (event->event_id) {
	case SYSTEM_EVENT_STA_START:
		esp_wifi_connect();
		break;
	case SYSTEM_EVENT_STA_GOT_IP:
		xEventGroupSetBits(g_wifi_event_group, CONNECTED_BIT);
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		/* This is a workaround as ESP32 WiFi libs don't currently
		   auto-reassociate. */
		esp_wifi_connect();
		xEventGroupClearBits(g_wifi_event_group, CONNECTED_BIT);
		break;
	default:
		break;
	}
	return ESP_OK;
}

void wifi_init(void)
{
	tcpip_adapter_init();
	g_wifi_event_group = xEventGroupCreate();
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

	nvs_handle handle;
	size_t size;
	wifi_config_t wifi_config;

	nvs_open("config", NVS_READONLY, &handle);

	nvs_get_str(handle, "SSID", NULL, &size);
	nvs_get_str(handle, "SSID", (char *) wifi_config.sta.ssid, &size);

	nvs_get_str(handle, "PASS", NULL, &size);
	nvs_get_str(handle, "PASS", (char *) wifi_config.sta.password,
		    &size);

	nvs_close(handle);

	ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...",
		 wifi_config.sta.ssid);
	ESP_LOGI(TAG, "Setting WiFi configuration PASS %s...",
		 wifi_config.sta.password);

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config
			(ESP_IF_WIFI_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());
}

