#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "led.h"
#include "button.h"
#include "wifi.h"
#include "ota.h"

#define HASH_LEN 32		/* SHA-256 digest length */

const char *TAG = "ota_update";

void print_sha256(const uint8_t * image_hash, const char *label)
{
	char hash_print[HASH_LEN * 2 + 1];
	hash_print[HASH_LEN * 2] = 0;
	for (int i = 0; i < HASH_LEN; ++i) {
		sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
	}
	ESP_LOGI(TAG, "%s: %s", label, hash_print);
}

void app_main(void)
{
	/* mark partition as valid */
	esp_ota_mark_app_valid_cancel_rollback();

	uint8_t sha_256[HASH_LEN] = { 0 };
	esp_partition_t partition;

	// get sha256 digest for the partition table
	partition.address = ESP_PARTITION_TABLE_OFFSET;
	partition.size = ESP_PARTITION_TABLE_MAX_LEN;
	partition.type = ESP_PARTITION_TYPE_DATA;
	esp_partition_get_sha256(&partition, sha_256);
	print_sha256(sha_256, "SHA-256 for the partition table: ");

	// get sha256 digest for bootloader
	partition.address = ESP_BOOTLOADER_OFFSET;
	partition.size = ESP_PARTITION_TABLE_OFFSET;
	partition.type = ESP_PARTITION_TYPE_APP;
	esp_partition_get_sha256(&partition, sha_256);
	print_sha256(sha_256, "SHA-256 for bootloader: ");

	// get sha256 digest for running partition
	esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
	print_sha256(sha_256, "SHA-256 for current firmware: ");

	// Initialize NVS.
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES
	    || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		// OTA app partition table has a smaller NVS partition size than the non-OTA
		// partition table. This size mismatch may cause NVS initialization to fail.
		// If this happens, we erase NVS partition and initialize NVS again.
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	button_init();
	led_init();
	wifi_init();
	ota_init();
}
